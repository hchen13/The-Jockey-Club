"""Closed-file motion recordings for the local diagnostics server.

Player, main host and every identified field entrant are extracted.
Numbers are the JSON values as recorded, without rounding or distance bins.
``speed_kmh`` belongs to the preceding adjacent XY/time interval, or is None
at a discontinuity. ``chunks`` are half-open [start,end) sample-index ranges.

read_recording(path, course=None, evidence=None) returns {metadata, actors}.
split_by_laps(record, course_or_offsets) adds lap views without rereading or
copying samples. course accepts lap_offsets and optional single_loop=True.
RecordingIndex.scan(on_record=archive_callback) decodes each stable version
once, delivers its small extracted record, and retains metadata only. Supply
the callback on the FIRST scan if an archive is required. The caller owns
archive persistence and the evidence/log index; no installed file is written.

Current v2 headers contain site/race_token but no outcome or rider model.
phase=11/racing=False does not prove a normal finish. Evidence can contain
{status: finished|cancelled|settled|incomplete, reason, log_lines, started_at}.
A specific host label additionally requires host_name plus host_evidence.
The exact 'paired native motion recording: <filename>' log marker associates
events with the file; result messages do not themselves contain race_token.
"""
from __future__ import annotations

import bisect
import contextlib
import copy
import ctypes
import importlib.util
import io
import math
import os
from pathlib import Path
import re
import struct
import time
from typing import Callable, Mapping
import json


MAX_RECORDING_BYTES = 256 * 1024 * 1024  # Producer caps uncompressed files at 128 MB.
ARCHIVE_VERSION = 4  # Retain boost, stimulant, bonding and native facing vectors.
RECORD_NAME = re.compile(r"^(race-(\d+)-(\d+)-(\d+))\.jsonl(?:\.mszip)?$")
STATIONS = {
    11: "emerald-valentine", 12: "adler-strawberry", 13: "valentine-circuit",
    14: "rhodes-vanhorn", 15: "veteran-trail", 16: "blackwater", 17: "new-austin",
    19: "arthur-camp", 20: "arthur-camp-duel", 21: "john-stakes",
    22: "arabian-stakes", 23: "appaloosa-stakes", 24: "standardbred-stakes",
    25: "paint-stakes", 26: "tennessee-stakes", 27: "new-austin-javier-duel",
    28: "scarlett-meadows-bill-duel",
}


class RecordingPending(OSError):
    """An active writer, compression, replacement, or short stability wait."""


class InvalidRecording(ValueError):
    """A closed recording cannot be decoded or has no supported header."""


def fingerprint(path: str | Path) -> dict:
    stat = Path(path).stat()
    return {"size": stat.st_size, "mtime_ns": stat.st_mtime_ns,
            "ctime_ns": stat.st_ctime_ns, "inode": stat.st_ino}


@contextlib.contextmanager
def _exclusive_binary(path: Path):
    if os.name != "nt":
        # Offline portability; on the supported Windows host CreateFile below
        # is the authoritative check, not an assumption from a quiet mtime.
        with path.open("rb") as stream:
            yield stream
        return
    from ctypes import wintypes
    import msvcrt
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    create = kernel.CreateFileW
    create.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                       wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    create.restype = wintypes.HANDLE
    handle = create(str(path), 0x80000000, 0, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        error = ctypes.get_last_error()
        if error in (32, 33):
            raise RecordingPending("recording is still open by a writer/reader")
        raise OSError(error, ctypes.FormatError(error), str(path))
    try:
        fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except BaseException:
        kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
        kernel.CloseHandle(handle)
        raise
    with os.fdopen(fd, "rb") as stream:  # fd owns the native handle now.
        yield stream


def _compressed_partial(path: Path) -> bool:
    target = path if path.name.endswith(".mszip") else Path(str(path) + ".mszip")
    return Path(str(target) + ".part").exists()


def _snapshot(path: Path) -> tuple[bytes, dict]:
    if _compressed_partial(path):
        raise RecordingPending("compression is still in progress")
    with _exclusive_binary(path) as stream:
        before = os.fstat(stream.fileno())
        if before.st_size > MAX_RECORDING_BYTES:
            raise InvalidRecording("recording exceeds the extraction size bound")
        raw = stream.read(MAX_RECORDING_BYTES + 1)
        after = os.fstat(stream.fileno())
        if len(raw) != before.st_size or (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            raise RecordingPending("recording changed while being read")
    return raw, {"size": before.st_size, "mtime_ns": before.st_mtime_ns,
                 "ctime_ns": before.st_ctime_ns, "inode": before.st_ino}


def _expand(raw: bytes) -> bytes:
    if len(raw) < 16 or raw[:8] != b"TJCMSZ1\0":
        raise InvalidRecording("invalid MSZIP recording header")
    if struct.unpack_from("<Q", raw, 8)[0] > MAX_RECORDING_BYTES:
        raise InvalidRecording("expanded recording exceeds the extraction size bound")
    # Reuse the project's Windows decoder without requiring a particular cwd.
    spec = importlib.util.spec_from_file_location("_tjc_telemetry_read", Path(__file__).resolve().parents[1] / "telemetry_read.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    try:
        return module.expand(raw)
    except (OSError, ValueError, struct.error) as error:
        raise InvalidRecording(f"MSZIP decode failed: {error}") from error


def _number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _identity(frame: Mapping) -> dict:
    return {key: frame.get(key) for key in ("horse", "model", "rider")}


def _identity_valid(value: dict) -> bool:
    return all(isinstance(x, int) and not isinstance(x, bool) and x != 0 for x in value.values())


def _clock(sample: dict) -> tuple[str | None, int | float | None]:
    if _number(sample["game_ms"]):
        return "game_ms", sample["game_ms"]
    if _number(sample["t"]):
        return "t", sample["t"]
    return None, None


def _sample(row: dict, actor: str, index: int, source_line: int, race_window: bool,
            *, entrant=None) -> dict:
    frame = entrant if actor == "entrant" else row.get(actor)
    navigation = row.get("navigation") or {}
    if not isinstance(navigation, dict):
        navigation = {}
    frame = frame if isinstance(frame, dict) else {}
    xyz = frame.get("xyz")
    # Retain invalid/null source coordinates too, but never join them.
    return {
        "index": index, "source_line": source_line, "t": row.get("t"),
        "game_ms": row.get("game_ms"), "frame": row.get("frame"), "xyz": xyz,
        "identity": _identity(frame), "along_m": frame.get("along_m") if actor == "entrant" else navigation.get(actor + "_along_m"),
        "forward": frame.get("forward"),
        "reported_lap": navigation.get(actor + "_lap"), "phase": row.get("phase"),
        "racing": row.get("racing") is True, "race_window": race_window,
        "continuity_lost": navigation.get(actor + "_continuity_lost") is True,
        "speed_kmh": None, "observed_speed_kmh": None, "dt_s": None,
        "break_before": [], "chunk": None,
        "out": frame.get("out") is True,
        "boost": (row.get("host_boost") or {}).get("active") if actor == "npc" else frame.get("boost_active"),
        "stimulant": (row.get("player_stimulant") or {}).get("active") if actor == "player" else None,
    }


def _horse_snapshot(frame, t):
    """The first racing instance observation, in displayed 1..10 ranks."""
    if not isinstance(frame, dict) or not _identity_valid(_identity(frame)):
        return None
    attributes = {}
    def rank(values, index, offset=0):
        if isinstance(values, list) and len(values) > index:
            value = values[index]
            if isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= 9:
                return value + offset
        return None
    for i, name in enumerate(("speed", "acceleration")):
        value = rank(frame.get("raw_current"), i, 1)
        if value is not None:
            attributes[name] = {"value": value, "max": 10,
                                "base": rank(frame.get("raw_base"), i, 1),
                                "bonus": rank(frame.get("raw_bonus"), i)}
    status = frame.get("status") or {}
    health, maximum = status.get("health"), status.get("max_health")
    if _number(health) and _number(maximum) and maximum > 0 and 0 <= health <= maximum:
        attributes["health"] = {"value": health, "max": maximum}
    stamina = frame.get("normalized_stamina_native", status.get("normalized_stamina_native"))
    if _number(stamina) and 0 <= stamina <= 1:
        attributes["stamina"] = {"value": stamina, "max": 1}
    bond = status.get("bonding_rank")
    if isinstance(bond, int) and not isinstance(bond, bool) and 0 <= bond <= 4:
        attributes["bonding"] = {"value": bond, "max": 4}
    return {"identity": _identity(frame), "t": t, "attributes": attributes}


def _append(actor: dict, sample: dict, malformed_before: bool, max_gap: float, max_speed: float):
    samples, chunks = actor["samples"], actor["chunks"]
    reasons = sample["break_before"]
    xyz = sample["xyz"]
    valid = isinstance(xyz, list) and len(xyz) >= 3 and all(_number(v) for v in xyz[:3])
    if not valid:
        reasons.append("missing_or_invalid_position")
    if not _identity_valid(sample["identity"]):
        reasons.append("missing_identity")
        valid = False
    if _clock(sample)[0] is None:
        reasons.append("missing_time")
        valid = False
    previous = samples[-1] if samples else None
    if malformed_before:
        reasons.append("malformed_record")
    if previous is None:
        reasons.append("first_sample")
    elif previous["chunk"] is None:
        reasons.append("previous_sample_missing")
    else:
        if sample["identity"] != previous["identity"]:
            reasons.append("identity_changed")
        clock, now = _clock(sample)
        old_clock, then = _clock(previous)
        if clock != old_clock or now is None:
            reasons.append("clock_changed")
        else:
            dt = (now - then) / (1000 if clock == "game_ms" else 1)
            sample["dt_s"] = dt
            within_gap = dt <= max_gap or math.isclose(dt, max_gap, rel_tol=0, abs_tol=1e-12)
            if not dt > 0 or not within_gap:
                reasons.append("clock_reversed_or_duplicate" if dt <= 0 else "sample_gap")
            if valid and dt > 0:
                speed = math.hypot(xyz[0] - previous["xyz"][0], xyz[1] - previous["xyz"][1]) / dt * 3.6
                sample["observed_speed_kmh"] = speed
                if speed > max_speed:
                    reasons.append("teleport_or_implausible_speed")
    if valid:
        if not reasons:
            sample["speed_kmh"] = sample["observed_speed_kmh"]
            sample["chunk"] = previous["chunk"]
            chunks[-1]["end"] = sample["index"] + 1
        else:
            sample["chunk"] = len(chunks)
            chunks.append({"start": sample["index"], "end": sample["index"] + 1})
    samples.append(sample)


def read_recording(path: str | Path, course=None, evidence: Mapping | None = None,
                   *, max_gap: float = .3, max_speed_kmh: float = 100) -> dict:
    """Read one closed snapshot. Raises RecordingPending rather than polling.

    Only compact replay samples and an initial horse snapshot survive parsing.
    Host names are accepted only with explicit matched host_evidence.
    """
    path = Path(path).resolve()
    match = RECORD_NAME.fullmatch(path.name)
    if not match:
        raise InvalidRecording("expected race-<pid>-<tick>-<serial>.jsonl[.mszip]")
    if not _number(max_gap) or not _number(max_speed_kmh) or not 0 < max_gap or not 0 < max_speed_kmh:
        raise ValueError("invalid continuity limits")
    raw, stamp = _snapshot(path)
    if path.name.endswith(".mszip"):
        raw = _expand(raw)
    actors = {key: {"samples": [], "chunks": [], "laps": []} for key in ("player", "host")}
    header = None
    bookmaker_result = None
    malformed = 0
    broken_before = False
    racing_seen = False
    settlement_anchor_added = False
    total_laps = 0
    lengths = set()
    for line_number, line in enumerate(io.BytesIO(raw), 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except (ValueError, UnicodeError):
            if header is None:
                raise InvalidRecording("invalid recording header")
            malformed += 1; broken_before = True
            continue
        if header is None:
            if not isinstance(row, dict) or not str(row.get("schema", "")).startswith("HorseSpeedLab.motion."):
                raise InvalidRecording("unsupported recording header")
            header = {key: row.get(key) for key in ("schema", "build", "site", "race_token")}
            if isinstance(row.get("bookmaker"), dict):
                header["bookmaker"] = row["bookmaker"]
            continue
        if not isinstance(row, dict) or "schema" in row or "racing" not in row:
            malformed += 1; broken_before = True
            continue
        if isinstance(row.get("bookmaker_result"), dict):
            bookmaker_result = row["bookmaker_result"]
        racing = row.get("racing") is True
        first_result = racing_seen and not racing and not settlement_anchor_added
        race_window = racing or first_result
        if first_result:
            settlement_anchor_added = True
        racing_seen |= racing
        navigation = row.get("navigation")
        if isinstance(navigation, dict) and race_window:
            laps = navigation.get("total_laps")
            if isinstance(laps, int) and not isinstance(laps, bool) and 0 < laps <= 1000:
                total_laps = max(total_laps, laps)
            length = navigation.get("course_length_m")
            if _number(length) and length > 0:
                lengths.add(length)
        for label, source in (("player", "player"), ("host", "npc")):
            index = len(actors[label]["samples"])
            _append(actors[label], _sample(row, source, index, line_number, race_window),
                    broken_before, max_gap, max_speed_kmh)
            if racing and not actors[label].get("horse"):
                actors[label]["horse"] = _horse_snapshot(row.get(source), row.get("t"))
        extras = {}
        for entrant in row.get("extras") or []:
            if not isinstance(entrant, dict):
                continue
            number = entrant.get("index")
            if isinstance(number, int) and not isinstance(number, bool) and 0 <= number < 128:
                extras[f"jockey-{number}"] = entrant
        for label in extras:
            actors.setdefault(label, {"samples": [], "chunks": [], "laps": [],
                                      "label": f"Jockey {int(label.split('-')[1]) + 1}"})
        for label, actor in actors.items():
            if not label.startswith("jockey-"):
                continue
            entrant = extras.get(label)
            _append(actor, _sample(row, "entrant", len(actor["samples"]), line_number,
                                  race_window, entrant=entrant), broken_before, max_gap, max_speed_kmh)
            if racing and not actor.get("horse"):
                actor["horse"] = _horse_snapshot(entrant, row.get("t"))
        broken_before = False
    if header is None:
        raise InvalidRecording("empty recording")
    evidence = dict(evidence or {})
    status = evidence.get("status")
    if status not in ("finished", "cancelled", "settled", "incomplete"):
        status = "settled" if settlement_anchor_added else "incomplete"
    bookmaker = header.get("bookmaker") if isinstance(header.get("bookmaker"), dict) else None
    entrants = bookmaker.get("entrants", []) if bookmaker else []
    names = {e.get("index"): e.get("name_zh") or e.get("name") for e in entrants
             if isinstance(e, dict) and isinstance(e.get("index"), int)}
    host_name = names.get(0) if bookmaker else (evidence.get("host_name") if evidence.get("host_evidence") else None)
    if bookmaker_result:
        status = "finished" if bookmaker_result.get("completed") is True else "cancelled"
    metadata = {
        "id": match[1], "record_id": match[1], "path": str(path),
        "source_size": stamp["size"], "source_mtime_ns": stamp["mtime_ns"],
        "modified": stamp["mtime_ns"] / 1e9,
        "process_id": int(match[2]), "start_tick_ms": int(match[3]),
        "serial": int(match[4]), "source": {"path": str(path), **stamp,
                                               "compressed": path.name.endswith(".mszip")},
        "header": header, "site": header.get("site"), "build": header.get("build"),
        "race_token": header.get("race_token"), "station_key": STATIONS.get(header.get("site")),
        "status": status, "reason": evidence.get("reason") or (
            "settlement observed; outcome is unverified" if settlement_anchor_added else "no settlement event observed"),
        "evidence": evidence, "host_label": host_name or "赛事 host", "host_identified": bool(host_name),
        "total_laps": total_laps or (bookmaker.get("laps") if bookmaker else None), "course_lengths_m": sorted(lengths),
        "sample_count": len(actors["player"]["samples"]), "malformed_lines": malformed,
        "recording_integrity": "gaps_from_malformed_lines" if malformed else "parsed_closed_snapshot",
        "speed_source": "adjacent raw XY / game_ms delta; t only when game_ms unavailable",
        "max_gap_s": max_gap, "max_speed_kmh": max_speed_kmh,
        "archive_version": ARCHIVE_VERSION,
    }
    if bookmaker:
        metadata.update(kind="bookmaker", bookmaker=bookmaker, bookmaker_result=bookmaker_result,
                        station_key=bookmaker.get("venue"),
                        reason=(bookmaker_result or {}).get("reason") or
                               ("bookmaker exhibition completed" if status == "finished" else "bookmaker exhibition incomplete"))
    for name, actor in actors.items():
        if name in ("player", "host"):
            actor["label"] = ("观赛玩家" if bookmaker else "玩家") if name == "player" else metadata["host_label"]
        elif bookmaker and name.startswith("jockey-"):
            actor["label"] = names.get(int(name.split("-")[1])) or actor.get("label", name)
        metadata.setdefault("actors", {})[name] = {
            "samples": len(actor["samples"]), "chunks": len(actor["chunks"]),
            "racing_samples": sum(s["racing"] for s in actor["samples"]),
            "valid_speed_intervals": sum(s["speed_kmh"] is not None for s in actor["samples"]),
        }
    result = {"metadata": metadata, "actors": actors}
    return split_by_laps(result, course) if course is not None else result


def split_by_laps(record: dict, course_or_offsets) -> dict:
    """Lap views reference the original samples; no interpolation/resampling.

    Single-loop recordings use the raw one-based reported_lap. Their along_m
    may reset legitimately; it never influences physical interval continuity.
    """
    course = course_or_offsets if isinstance(course_or_offsets, Mapping) else {"lap_offsets": course_or_offsets}
    offsets = list(course.get("lap_offsets") or [])
    single = bool(course.get("single_loop"))
    total = record["metadata"].get("total_laps")
    if single and total and len(offsets) == 2:
        width = offsets[1] - offsets[0]
        offsets = [offsets[0] + i * width for i in range(total + 1)]
    valid = len(offsets) >= 2 and all(_number(v) for v in offsets) and all(b > a for a, b in zip(offsets, offsets[1:]))
    reason = None if valid else "invalid_course_offsets"
    if valid and total and len(offsets) - 1 != total:
        reason = "recorded_lap_count_mismatch"
    result = {**record, "metadata": {**record["metadata"], "lap_split": {
        "status": reason or "mapped", "single_loop": single, "offsets": offsets}}, "actors": {}}
    for name, actor in record["actors"].items():
        laps = []
        if reason is None:
            groups = [[] for _ in range(len(offsets) - 1)]
            runs = []
            prior_qualified = None
            maximum = -math.inf
            highest_reported = 0
            per_lap_maximum = [-math.inf] * len(groups)
            for sample in actor["samples"]:
                s = sample["along_m"]
                if not sample["race_window"] or not _number(s) or sample["chunk"] is None:
                    prior_qualified = None
                    continue
                assigned = sample["reported_lap"] if single else min(len(groups), bisect.bisect_right(offsets, s))
                mapped = isinstance(assigned, int) and not isinstance(assigned, bool) and 1 <= assigned <= len(groups)
                if mapped and (single or offsets[0] <= s <= offsets[-1]):
                    groups[assigned - 1].append(sample["index"])
                if sample["continuity_lost"] or (single and not mapped):
                    prior_qualified = None
                    continue
                maximum = max(maximum, s)
                if single:
                    highest_reported = max(highest_reported, assigned)
                    per_lap_maximum[assigned - 1] = max(per_lap_maximum[assigned - 1], s)
                run_key = (sample["chunk"], assigned if single else None)
                if prior_qualified == run_key:
                    runs[-1]["lo"] = min(runs[-1]["lo"], s)
                    runs[-1]["hi"] = max(runs[-1]["hi"], s)
                else:
                    runs.append({"lo": s, "hi": s, "lap": assigned if single else None})
                prior_qualified = run_key
            for lap in range(len(offsets) - 1):
                lo, hi = offsets[lap:lap + 2]
                indices = groups[lap]
                ranges = []
                for i in indices:
                    if ranges and ranges[-1]["end"] == i and actor["samples"][i]["chunk"] == actor["samples"][i-1]["chunk"]:
                        ranges[-1]["end"] = i + 1
                    else:
                        ranges.append({"start": i, "end": i + 1})
                target_lo, target_hi = (0, hi - lo) if single else (lo, hi)
                reached = (per_lap_maximum[lap] >= target_hi or highest_reported > lap + 1) if single else maximum >= target_hi
                fully = any(run["lo"] <= target_lo and run["hi"] >= target_hi and
                            (not single or run["lap"] == lap + 1) for run in runs)
                laps.append({"lap": lap + 1, "start_m": lo, "end_m": hi,
                             "chunks": ranges, "sample_count": len(indices),
                             "reached_end": reached, "fully_observed": fully})
        result["actors"][name] = {**actor, "laps": laps}
    return result


def relocated_source_path(path, directory):
    """Map only the exact same installation's old recording directory.

    Historical archives stay byte-for-byte intact. No file at the stored
    absolute path is opened, and unrelated same-named sources never match.
    """
    old, directory = Path(path), Path(directory)
    if (directory.name.casefold() == "recordings" and directory.parent.name.casefold() == "thejockeyclub" and
            RECORD_NAME.fullmatch(old.name) and
            os.path.normcase(str(old.parent.resolve())) == os.path.normcase(
                str((directory.parent.parent / "TheJockeyClub-motion").resolve()))):
        return directory / old.name
    return old


def relocate_metadata_sources(metadata, directory):
    """Rebase an index view, retaining provenance and every recorded value."""
    source = metadata.get("source")
    if isinstance(source, dict) and isinstance(source.get("path"), str):
        previous = source["path"]
        current = str(relocated_source_path(previous, directory))
        if current != previous:
            metadata["source"] = {**source, "path": current, "relocated_from": source.get("relocated_from", previous)}
    elif isinstance(source, str):
        current = str(relocated_source_path(source, directory))
        if current != source:
            metadata["source"] = {"path": current, "relocated_from": source}
    if isinstance(metadata.get("path"), str):
        metadata["path"] = str(relocated_source_path(metadata["path"], directory))


class RecordingIndex:
    """Stat-only refreshes after one extraction per stable file version."""
    def __init__(self, directory: str | Path, *, stable_seconds: float = .75,
                 clock: Callable[[], float] = time.monotonic, reader: Callable = read_recording):
        self.directory = Path(directory)
        self.stable_seconds = stable_seconds
        self.clock, self.reader = clock, reader
        self._seen = {}
        self._metadata = {}
        self._seeded = {}

    def seed(self, metadata_list) -> None:
        """Restore small archive metadata, without loading archived samples.

        Matching source path/size/mtime suppresses decoding after restart.
        A producer's jsonl -> atomic mszip representation change retains its
        unique recording id, so an already archived source is not re-decoded.
        """
        for value in metadata_list:
            metadata = value.get("metadata", value)
            key = metadata.get("record_id") or metadata.get("id")
            if key:
                self._seeded[key] = copy.deepcopy(metadata)

    def forget(self, identifiers) -> None:
        """Discard cache entries when the owning archive is safely removed."""
        for key in identifiers:
            self._seeded.pop(key, None)
            self._metadata.pop(key, None)
            self._seen.pop(key, None)

    @staticmethod
    def _archive_matches(metadata: dict, path: Path, stamp: dict) -> bool:
        if metadata.get("archive_version", 1) < ARCHIVE_VERSION:
            return False
        source = metadata.get("source") or {}
        source = source if isinstance(source, dict) else {"path": source}
        old_path = source.get("path") or metadata.get("path")
        if not old_path:
            return False
        old_path = relocated_source_path(old_path, path.parent)
        same_path = os.path.normcase(str(old_path.resolve())) == os.path.normcase(str(path.resolve()))
        if same_path:
            return (source.get("size", metadata.get("source_size")) == stamp["size"] and
                    source.get("mtime_ns", metadata.get("source_mtime_ns")) == stamp["mtime_ns"])
        # CompressFile closes .part and atomically renames before deleting
        # the original; it cannot contain a later race under the same id.
        return os.path.normcase(str(path.resolve())) == os.path.normcase(str(Path(str(old_path) + ".mszip").resolve()))

    def scan(self, on_record: Callable[[dict], None] | None = None,
             evidence_by_id: Mapping | None = None) -> list[dict]:
        now = self.clock()
        candidates = {}
        try:
            files = list(self.directory.iterdir())
        except FileNotFoundError:
            return []
        for path in files:
            match = RECORD_NAME.fullmatch(path.name)
            if not match or not path.is_file():
                continue
            key = match[1]
            if key not in candidates or path.name.endswith(".mszip"):
                candidates[key] = path
        output = []
        for key, path in candidates.items():
            try:
                stamp = fingerprint(path)
            except OSError:
                continue
            signature = (str(path), tuple(stamp.items()))
            item = {"record_id": key, "path": str(path.resolve()), "fingerprint": stamp, "ready": False}
            previous = self._seen.get(key)
            if previous is None or previous[0] != signature:
                self._seen[key] = (signature, now)
                previous = self._seen[key]
            cached = self._metadata.get(key)
            if _compressed_partial(path):
                item.update(state="pending", reason="compression_in_progress")
            elif cached and cached[0] == signature:
                item.update(copy.deepcopy(cached[1]))
            elif (key in self._seeded and self._archive_matches(self._seeded[key], path, stamp)):
                state = {"state": "ready", "ready": True, "metadata": copy.deepcopy(self._seeded[key])}
                self._metadata[key] = (signature, state)
                item.update(copy.deepcopy(state))
            elif now - previous[1] < self.stable_seconds:
                item.update(state="pending", reason="waiting_for_stable_file")
            else:
                try:
                    record = self.reader(path, evidence=(evidence_by_id or {}).get(key))
                except (RecordingPending, FileNotFoundError) as error:
                    item.update(state="pending", reason=str(error))
                except (InvalidRecording, OSError) as error:
                    state = {"state": "invalid", "reason": str(error)}
                    self._metadata[key] = (signature, state)
                    item.update(state)
                else:
                    # Archive failures must propagate; never cache a disk-full
                    # callback as an invalid source and silently lose the race.
                    if on_record is not None:
                        on_record(record)
                    metadata = copy.deepcopy(record["metadata"])
                    state = {"state": "ready", "ready": True, "metadata": metadata}
                    self._metadata[key] = (signature, state)
                    self._seeded[key] = copy.deepcopy(metadata)
                    item.update(copy.deepcopy(state))
            output.append(item)
        return sorted(output, key=lambda item: item["fingerprint"]["mtime_ns"], reverse=True)
