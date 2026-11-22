"""Local race clubhouse. Heavy work is serialized outside RDR2.

Recordings, surveys and game saves are read-only. The explicit settings endpoint
edits only global INI configuration; recordings outlive the game's retention.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import gzip
import hashlib
import json
import logging
from logging.handlers import RotatingFileHandler
import math
import maintenance
from storage_retention import StorageRetention
from output_policy import OutputCapacityError, checked_directory, _identity, _regular
import os
from pathlib import Path
import re
import secrets
import socket
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from datetime import datetime
from event_names import race_names
from global_settings import GlobalSettings, SettingsConflict
from clubhouse_runtime import (APPLICATION, InstallationLease, InstalledLayout, default_state, discovery_file,
                               installation_id, installed_game_root, payload_root, remove_owned_discovery)

HERE = Path(__file__).resolve().parent
DEFAULT_GAME = Path(os.environ.get("RDR2_GAME_DIR", Path.cwd()))
LOG = logging.getLogger("race-dashboard")


class ByteBoundedLogHandler(RotatingFileHandler):
    """UTF-8 byte limits, including a single unexpectedly large diagnostic."""
    def record_bytes(self, record):
        text = self.format(record) + self.terminator
        return len(text.encode('utf-8')) + (text.count('\n') if os.name == 'nt' else 0)

    def emit(self, record):
        if self.record_bytes(record) > min(64 * 1024, self.maxBytes or 64 * 1024):
            return
        super().emit(record)

    def shouldRollover(self, record):
        if self.stream is None:
            self.stream = self._open()
        self.stream.seek(0, 2)
        size = os.fstat(self.stream.fileno()).st_size
        incoming = self.record_bytes(record)
        return self.maxBytes > 0 and size + incoming > self.maxBytes


STATUSES = {"finished": "正常完赛", "cancelled": "中途终止", "settled": "已结算 · 完赛未确认", "incomplete": "记录不完整"}
HOST_NAMES = {"PLAYER_ZERO": "Arthur", "PLAYER_THREE": "John", "CS_JOHNMARSTON": "John",
              "CS_SADIEADLER": "Sadie", "CS_UNCLE": "Uncle", "CS_KAREN": "Karen", "CS_MARYBETH": "Mary-Beth",
              "CS_PEARSON": "Pearson", "CS_CHARLESSMITH": "Charles", "CS_JAVIERESCUELLA": "Javier",
              "CS_BILLWILLIAMSON": "Bill", "CS_TILLY": "Tilly", "CS_SEAN": "Sean", "CS_LENNY": "Lenny"}
TELEMETRY_KEYS = ("player_bonding_rank", "stimulant_events", "outcome_wall_s", "entrant_out_events", "winner")


def json_bytes(value):
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")


def atomic_json(path, value):
    path = Path(path)
    payload = json_bytes(value)
    if len(payload) > StorageRetention.INDEX_BYTES:
        raise OutputCapacityError("JSON output capacity reached; existing file preserved")
    checked_directory(path.parent)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    identity = None
    try:
        with temporary.open('xb') as output:
            identity = _identity(os.fstat(output.fileno()))
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        for attempt in range(21):
            try:
                checked_directory(path.parent)
                if (path.exists() or path.is_symlink()) and not maintenance.safe_path(path.parent, path):
                    raise OutputCapacityError("JSON destination is unsafe; existing file preserved")
                staged = temporary.lstat()
                if not _regular(staged) or _identity(staged) != identity or staged.st_size != len(payload):
                    raise OutputCapacityError("JSON stage changed; existing file preserved")
                os.replace(temporary, path)
                break
            except PermissionError:
                if attempt == 20:
                    raise
                time.sleep(.01)
    finally:
        if identity is not None:
            try:
                staged = temporary.lstat()
                if _regular(staged) and _identity(staged) == identity:
                    temporary.unlink()
            except FileNotFoundError:
                pass


def source_id(path):
    return Path(path).name.removesuffix(".mszip").removesuffix(".jsonl")


class EvidenceIndex:
    """Only associate outcomes between the recorder's explicit paired markers.

    Initial two-file scan is followed by tail reads. Rotation starts a new scan;
    menu/host spawn failures outside a recording never contaminate the last race.
    """
    marker = "paired native motion recording:"

    def __init__(self, game):
        logs = InstalledLayout(game).logs
        self.paths = [logs / "TheJockeyClub.log.1", logs / "TheJockeyClub.log"]
        self.states = {}
        self.active = None
        self.records = {}
        self.recent_host = None
        self.hosts_by_site = {}

    def _line(self, line):
        model = re.search(r"\[Race\] fixed host=([A-Za-z0-9_]+)", line)
        if model:
            self.recent_host = {"model": model[1].upper(), "line": line.strip()}
        reveal = re.search(r"\[Race\] fixed host pair revealed: site=(\d+)", line)
        if reveal and self.recent_host:
            self.hosts_by_site[int(reveal[1])] = dict(self.recent_host)
        if self.marker in line:
            filename = line.split(self.marker, 1)[1].strip().replace("\\", "/")
            self.active = source_id(filename) if filename else None
            if self.active:
                stamp = re.match(r"\[(\d\d:\d\d:\d\d)\]", line)
                self.records.setdefault(self.active, {})["started_at"] = stamp[1] if stamp else ""
                self.records[self.active]["hosts_by_site"] = dict(self.hosts_by_site)
            return
        if not self.active:
            return
        inventory = re.search(r"player stimulant inventory:.*\(inventory id \d+, bonding ([1-5])\)", line)
        if inventory:
            self.records.setdefault(self.active, {}).setdefault("player_bonding_rank", int(inventory[1]) - 1)
        evidence = self.records.setdefault(self.active, {})
        stamp = re.match(r"\[(\d\d:\d\d:\d\d)\]", line)
        def elapsed():
            if not stamp or not evidence.get("started_at"):
                return None
            seconds = lambda text: sum(int(v) * m for v, m in zip(text.split(":"), (3600, 60, 1)))
            return (seconds(stamp[1]) - seconds(evidence["started_at"])) % 86400
        taken = re.search(r"player stimulant (\w+) taken: burst ([\d.]+) s at bonding ([1-5])", line)
        stopped = "player stimulant burst over" in line or "player stimulant burst released:" in line
        if taken or stopped:
            clock = re.search(r"game_ms=(\d+)", line)
            event = {"active": bool(taken), "wall_offset_s": elapsed()}
            if clock:
                event["game_ms"] = int(clock[1])
            if taken:
                event.update(duration_s=float(taken[2]), tier=taken[1], bonding_rank=int(taken[3])-1)
            events = evidence.setdefault("stimulant_events", [])
            if event not in events:
                events.append(event)
        if "[Race] " not in line:
            return
        message = line.split("[Race] ", 1)[1].strip()
        eliminated = re.search(r"^Field host out rider=(\d+) reason=([\w-]+)", message)
        legacy = message in ("horse rank lease restored/released: host left circuit bounds",
                             "horse rank lease restored/released: host skipped a section",
                             "horse rank lease restored/released: Uncle eliminated from multi-racer field")
        if eliminated or legacy:
            clock = re.search(r"game_ms=(\d+)", message)
            event = {"actor": "host", "wall_offset_s": elapsed(), "source": line.strip()}
            if eliminated:
                event.update(rider=int(eliminated[1]), reason=eliminated[2])
            else:
                event["reason"] = "left-corridor" if message.endswith("bounds") else "skipped-section" if message.endswith("section") else "accident"
            if clock:
                event["game_ms"] = int(clock[1])
            events = evidence.setdefault("entrant_out_events", [])
            # The new exact event precedes the old rank-restoration message.
            if event not in events and not (legacy and any(e.get("game_ms") is not None and e.get("wall_offset_s") == event["wall_offset_s"] for e in events)):
                events.append(event)
            return
        winner = re.fullmatch(r"race winner actor=(player|host|jockey-\d+) game_ms=(\d+)", message)
        if winner:
            evidence["winner"] = {"actor": winner[1], "game_ms": int(winner[2]), "source": line.strip()}
            return
        if not message.startswith(("result:", "circuit disqualification:", "aborted:")):
            return
        evidence = self.records.setdefault(self.active, {})
        if "status" in evidence:
            return
        accident_win = message.startswith("result: player won after opponent accident: ")
        status = "finished" if accident_win or message in ("result: player won", "result: opponent won") else "cancelled"
        evidence.update(status=status, reason=message, log_lines=[line.strip()], outcome_wall_s=elapsed())

    def refresh(self):
        snapshots = {}
        for path in self.paths:
            try:
                st = path.stat()
                snapshots[path] = (st.st_ino, st.st_size)
            except FileNotFoundError:
                snapshots[path] = (None, 0)
        rebuild = not self.states or any(
            p not in self.states or ino != self.states[p][0] or size < self.states[p][1]
            for p, (ino, size) in snapshots.items())
        if rebuild:
            self.states = {}
            self.active = None
            self.recent_host, self.hosts_by_site = None, {}
        for path in self.paths:
            ino, size = snapshots[path]
            offset = self.states.get(path, (ino, 0))[1]
            if ino is not None and size > offset:
                with path.open("rb") as stream:
                    stream.seek(offset)
                    data = stream.read(size - offset)
                end = data.rfind(b"\n") + 1
                for raw in data[:end].splitlines():
                    self._line(raw.decode("utf-8", "replace"))
                offset += end
            self.states[path] = (ino, offset)
        return self.records


class ClubhouseServer(ThreadingHTTPServer):
    # HTTPServer enables SO_REUSEADDR, which on Windows can let two HTTP servers
    # bind the same loopback port. Exclusive binding makes fallback deterministic
    # and prevents traffic for two game installations from sharing a listener.
    allow_reuse_address = False

    def server_bind(self):
        if os.name == "nt":
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        super().server_bind()


def _finite(value):
    return isinstance(value, (float, int)) and not isinstance(value, bool) and math.isfinite(value)


def display_record(record, course, *, host_name="赛事 Host", note=""):
    """Adapt raw samples to display laps without resampling or inventing endpoints.

    Current route geometry is shown only if its recorded length is compatible.
    A repeated SingleLap uses native per-lap progress and explicit lap counters.
    """
    from race_replay import restore_stimulant_timeline, restore_entrant_timeline
    restore_stimulant_timeline(record)
    restore_entrant_timeline(record)
    metadata = record["metadata"]
    total = max(1, int(metadata.get("total_laps") or 1))
    if course and not course.get("profiles"):
        note += " " + course.get("source_note", "自由选路没有固定赛道里程；速度按开赛后时间对比。")
        course = None
    profiles = course.get("profiles", []) if course else []
    params = course.get("parameters", {}) if course else {}
    offsets = course.get("lap_offsets", []) if course else []
    single = bool(course and (course.get("single_loop") or course.get("meta", {}).get("single_loop")))
    if len(offsets) == 2 and total > 1:
        single = True
    by_lap = {}
    for profile in profiles:
        for observation in profile.get("observations", []):
            by_lap[observation["lap"]] = (profile, observation)
    length = metadata.get("course_length_m")
    if not _finite(length):
        length = metadata.get("course_length")
    if not _finite(length):
        lengths = metadata.get("course_lengths_m", [])
        length = max(lengths) if lengths else None
    if course and _finite(length) and length > 0 and abs(course.get("length", 0) - length) > max(.5, length * .00001):
        note += " 当前路线长度与本场记录不符，隐藏其边界，保留真实轨迹。"
        course, profiles, offsets, by_lap, single = None, [], [], {}, False
    laps = []
    unknown = {"lap": None, "profile": "unlocated", "start": 0, "end": None, "length": None,
               "path": [], "rows": [], "parameters": {}, "player": [], "host": [], "speed_axis": "time"}
    warnings = {"unknown_lap_samples": 0, "lap_progress_conflicts": 0, "unlocated_speed_samples": 0}
    for number in range(1, total + 1):
        profile, observation = by_lap.get(1 if single else number, ({}, {}))
        start = 0 if single else observation.get("global_start", offsets[number - 1] if number < len(offsets) else 0)
        end = observation.get("global_end", offsets[number] if number < len(offsets) else length)
        laps.append({"lap": number, "profile": profile.get("id", "course"), "start": start, "end": end,
                     "length": (end - start) if _finite(end) else None, "path": profile.get("path", []),
                     "rows": observation.get("rows", []), "parameters": params, "player": [], "host": [],
                     "speed_axis": "distance" if offsets else "time"})
    bookmaker = metadata.get("kind") == "bookmaker"
    mapped_actors = ["player", "host"]
    if bookmaker:
        mapped_actors += [key for key in record.get("actors", {}) if key.startswith("jockey-")]
    for actor in mapped_actors:
        samples = record.get("actors", {}).get(actor, {}).get("samples", [])
        for sample in samples:
            if not sample.get("race_window", sample.get("racing", False)):
                continue
            if sample.get("chunk") is None:
                continue
            xyz = sample.get("xyz")
            if not xyz or len(xyz) < 3 or not all(_finite(v) for v in xyz[:3]):
                continue
            along = sample.get("along_m")
            reported = sample.get("reported_lap")
            # Explicit lap counters remain authoritative; no even division of a
            # recorded first/regular/final course with unequal profile lengths.
            if _finite(reported) and reported == int(reported) and 1 <= reported <= total:
                number = int(reported)
            elif bookmaker and single and len(offsets) == 2 and _finite(along) and offsets[-1] > offsets[0] and 0 <= along <= (offsets[-1] - offsets[0]) * total:
                number = min(total, int(along / (offsets[-1] - offsets[0])) + 1)
            elif offsets and _finite(along) and not single and offsets[0] <= along <= offsets[-1]:
                import bisect
                number = min(total, max(1, bisect.bisect_right(offsets, along)))
            else:
                number = 1 if total == 1 else None
            lap = laps[number - 1] if number else unknown
            if number is None:
                warnings["unknown_lap_samples"] += 1
            qualified = not sample.get("continuity_lost", False)
            local = along - lap["start"] if offsets and _finite(along) and qualified and number else None
            if bookmaker and single and local is not None and len(offsets) == 2:
                local = along - (number - 1) * (offsets[-1] - offsets[0])
            conflict = bool(_finite(local) and _finite(lap["length"]) and (local < -.5 or local > lap["length"] + .5))
            if conflict:
                warnings["lap_progress_conflicts"] += 1
            speed = sample.get("speed_kmh")
            if local is None and lap["speed_axis"] == "distance" and speed is not None:
                warnings["unlocated_speed_samples"] += 1
            target = lap[actor] if actor in ("player", "host") else lap.setdefault("riders", {}).setdefault(actor, [])
            target.append({"t": sample.get("t"), "s": local, "x": xyz[0], "y": xyz[1], "z": xyz[2],
                               "speed": speed, "chunk": sample.get("chunk"), "source_line": sample.get("source_line"),
                               "index": sample.get("index"), "lap_progress_conflict": conflict,
                               "speed_x": sample.get("t") if lap["speed_axis"] == "time" else local,
                               "boost": sample.get("boost"), "stimulant": sample.get("stimulant"),
                               "break_before": sample.get("break_before", []), "game_ms": sample.get("game_ms")})
    if unknown["player"] or unknown["host"] or any(unknown.get("riders", {}).values()):
        laps.append(unknown)
    from race_replay import enrich_display
    return enrich_display({"metadata": metadata, "course": course, "laps": laps,
                           "host_name": host_name, "source_note": note, "warnings": warnings}, record)


class Dashboard:
    def __init__(self, game, state, *, poll=3, course_module=None, record_module=None):
        if course_module is None:
            import live_courses as course_module
        if record_module is None:
            import live_records as record_module
        self.courses_module, self.record_module = course_module, record_module
        self.game, self.state = Path(game).resolve(), Path(state).resolve()
        self.layout = InstalledLayout(self.game)
        self.settings = GlobalSettings(self.game, self.state)
        self.state.mkdir(parents=True, exist_ok=True)
        self.archive = self.state / "recordings"
        self.archive.mkdir(exist_ok=True)
        self.poll = poll
        self.lock = threading.RLock()
        self.clearing = False
        self.executor = concurrent.futures.ThreadPoolExecutor(max_workers=1, thread_name_prefix="race-diagnostics")
        self.planner_executor = concurrent.futures.ThreadPoolExecutor(max_workers=1, thread_name_prefix="race-planner")
        self.stop = threading.Event()
        self.index_file = self.state / "recordings.json"
        self.storage = StorageRetention(self.game, self.state)
        self.index_blocked = False
        self.index_error = None
        try:
            self.index_file.lstat()
            if not maintenance.safe_path(self.state, self.index_file):
                raise OSError("分析记录索引路径不安全。")
            if self.index_file.stat().st_size > 32 * 1024 * 1024:
                raise OSError("分析记录索引超过读取上限；原文件已保留，可使用记录维护清理。")
            self.races = json.loads(self.index_file.read_text(encoding="utf-8"))
            if not isinstance(self.races, dict) or not all(isinstance(k, str) and isinstance(v, dict) for k, v in self.races.items()):
                raise ValueError("分析记录索引格式无效。")
        except FileNotFoundError:
            self.races = {}
        except (OSError, ValueError) as error:
            # Never silently replace an inaccessible/corrupt/oversized index.
            self.races = {}
            self.index_blocked, self.index_error = True, str(error)
        self.revision = 0
        self.jobs = {}
        self.job_number = 0
        self.planning_job = None
        self.scanner = {"busy": False, "error": None, "last_scan": None}
        self.next_retention = 0.0
        self.evidence = EvidenceIndex(self.game)
        self.record_index = record_module.RecordingIndex(self.layout.recordings)
        if hasattr(record_module, "relocate_metadata_sources"):
            for metadata in self.races.values():
                record_module.relocate_metadata_sources(metadata, self.layout.recordings)
        if hasattr(self.record_index, "seed"):
            self.record_index.seed([value for key, value in self.races.items() if self.storage.archive_present(key)])
        self.course_catalog = self.courses_module.catalog(self.game)
        self._catalog_sig = None
        self._load_metadata()

    def _load_metadata(self):
        for race in self.races.values():
            self.decorate(race)

    def decorate(self, metadata):
        courses = self.course_catalog.get("courses", [])
        site = metadata.get("site")
        bookmaker = metadata.get("bookmaker") if metadata.get("kind") == "bookmaker" else None
        entry = (next((c for c in courses if c.get("id") == "bookmaker/" + str(bookmaker.get("venue", ""))), None)
                 if isinstance(bookmaker, dict) else next((c for c in courses if site in c.get("site_ids", [])), None))
        if entry:
            metadata["course_id"] = entry["id"]
            metadata["title"] = entry.get("title", entry["id"])
            metadata["title_en"] = entry.get("title_en", metadata["title"])
            # A current INI's Host field is not evidence of the historical rider.
            metadata.setdefault("host_name", metadata.get("host_label") if bookmaker else "赛事 Host")
            if entry.get("event_kind") in ("duel", "challenge"):
                metadata.update(race_names(metadata["host_name"], entry.get("breed", "")))
        else:
            metadata.update(course_id="", title=f"未识别场次 · 站点 {site}", host_name="未确认 Host",
                            comparison_available=False)
        metadata["status_label"] = STATUSES.get(metadata.get("status"), "结局未确认")
        return metadata

    def _save_record(self, record):
        if self.index_blocked:
            raise RuntimeError(self.index_error or "Recording index is unavailable")
        metadata = dict(record["metadata"])
        identifier = metadata.get("id") or metadata.get("record_id") or source_id(metadata.get("path", ""))
        if not identifier or not re.fullmatch(r"[A-Za-z0-9_.-]+", identifier):
            raise ValueError("recording has no safe stable ID")
        metadata["id"] = identifier
        # A v1 archive upgrade must not lose the earlier paired log evidence
        # merely because the game has since rotated those log lines away.
        previous = self.races.get(identifier, {})
        retained_keys = ("started_at", "date") if metadata.get("kind") == "bookmaker" else (
            "host_name", "host_evidence", "started_at", "date", "outcome_evidence", "telemetry_evidence")
        for key in retained_keys:
            if previous.get(key) is not None:
                metadata[key] = previous[key]
        if metadata.get("kind") != "bookmaker" and previous.get("status") in ("finished", "cancelled"):
            metadata["status"] = previous["status"]
        evidence = self.evidence.records.get(identifier, {})
        telemetry = {k: evidence[k] for k in TELEMETRY_KEYS if k in evidence}
        if telemetry:
            metadata["telemetry_evidence"] = {**metadata.get("telemetry_evidence", {}), **telemetry}
        if metadata.get("kind") != "bookmaker" and evidence.get("status"):
            metadata.update(status=evidence["status"], outcome_evidence=evidence)
        if evidence.get("started_at"):
            metadata["started_at"] = evidence["started_at"]
        header = metadata.get("header", {})
        metadata.setdefault("site", header.get("site"))
        metadata.setdefault("build", header.get("build"))
        source = metadata.get("source", {})
        metadata.setdefault("modified", source.get("mtime_ns", 0) / 1e9)
        metadata.setdefault("date", datetime.fromtimestamp(metadata["modified"]).strftime("%Y-%m-%d"))
        host = None if metadata.get("kind") == "bookmaker" else evidence.get("hosts_by_site", {}).get(metadata["site"])
        if host and host.get("model") in HOST_NAMES:
            metadata.update(host_name=HOST_NAMES[host["model"]], host_evidence=host["line"])
        self.decorate(metadata)
        record["metadata"] = metadata
        with self.lock:
            prospective = {**self.races, identifier: metadata}
            self.storage.check_index(json_bytes(prospective))
            self.storage.publish_archive(identifier, gzip.compress(json_bytes(record), compresslevel=3, mtime=0))
            atomic_json(self.index_file, prospective)
            self.races = prospective
            self.revision += 1
        LOG.info("Archived %s: %s", identifier, metadata.get("status"))

    def _retain_storage(self):
        # Run before importing: a refused over-quota import must not prevent
        # existing retention from making room after the game has closed.
        now = time.monotonic()
        if (now < self.next_retention or self.planning_job or
                any(job.get("status") in ("queued", "running") for job in self.jobs.values()) or
                maintenance.game_running()):
            return
        self.next_retention = now + 60
        may_delete = lambda: not maintenance.game_running()
        result = self.storage.prune_archives(self.races, may_delete=may_delete)
        if result.get("removed") or result.get("repaired"):
            try:
                maintenance.write_index(self.state, self.index_file, json_bytes(self.races))
            except OSError as error:
                self.index_blocked, self.index_error = True, str(error)
                raise
            if hasattr(self.record_index, "forget"):
                self.record_index.forget(result.get("ids", []))
            self.revision += 1
        if not result.get("stopped"):
            self.storage.prune_caches(may_delete=may_delete)
        if result.get("blocked"):
            self.scanner["error"] = "部分记录正在使用或无法安全访问，暂时无法达到存储上限。"

    def _scan(self):
        with self.lock:
            if self.clearing:
                return
            self.scanner.update(busy=True, error=None)
        try:
            if self.index_blocked:
                raise RuntimeError(self.index_error or "Recording index is unavailable")
            with self.lock:
                self._retain_storage()
            self.evidence.refresh()
            # Recover new diagnostic fields for existing archives without
            # reparsing raw motion or losing them when the game log rotates.
            with self.lock:
                changed = False
                for identifier, metadata in self.races.items():
                    evidence = self.evidence.records.get(identifier, {})
                    # Paired outcome evidence survives game-log rotation. The
                    # old parser stored this explicit victory as cancelled.
                    retained_reason = (metadata.get("outcome_evidence") or {}).get("reason") or metadata.get("reason")
                    if not evidence.get("status") and retained_reason and retained_reason.startswith(
                            "result: player won after opponent accident: "):
                        evidence = {**evidence, "status": "finished", "reason": retained_reason}
                    # Correct old outcome classifications from paired logs too;
                    # already archived recordings are not decoded a second time.
                    outcome = {k: evidence[k] for k in ("status", "reason", "log_lines", "outcome_wall_s") if k in evidence}
                    previous_outcome = metadata.get("outcome_evidence") or {}
                    if evidence.get("status") and (metadata.get("status") != evidence["status"] or
                                                   any(previous_outcome.get(k) != v for k, v in outcome.items())):
                        metadata.update(status=evidence["status"], reason=evidence["reason"],
                                        outcome_evidence={**previous_outcome, **outcome})
                        self.decorate(metadata)
                        changed = True
                    supplement = {k: evidence[k] for k in TELEMETRY_KEYS if k in evidence}
                    previous = metadata.get("telemetry_evidence", {})
                    merged = {**previous, **supplement}
                    if merged != previous:
                        metadata["telemetry_evidence"] = merged
                        changed = True
                if changed:
                    self.storage.check_index(json_bytes(self.races))
                    atomic_json(self.index_file, self.races)
                    self.revision += 1
            # Catalog discovery is cheap; it never solves a line.
            catalog = self.courses_module.catalog(self.game)
            signature = hashlib.sha256(json_bytes(catalog)).hexdigest()
            with self.lock:
                if signature != self._catalog_sig:
                    self.course_catalog, self._catalog_sig = catalog, signature
                    self._load_metadata()
                    self.revision += 1
            entries = self.record_index.scan(on_record=self._save_record, evidence_by_id=self.evidence.records)
            with self.lock:
                self.scanner["pending"] = sum(not e.get("ready") and e.get("state") != "invalid" for e in entries)
                self.scanner["invalid"] = [e for e in entries if e.get("state") == "invalid"]
        except Exception as error:
            LOG.exception("Recording scan failed")
            with self.lock:
                self.scanner["error"] = str(error)
        finally:
            with self.lock:
                self.scanner.update(busy=False, last_scan=time.time())

    def start(self):
        def loop():
            pending = None
            while not self.stop.is_set():
                if pending is None or pending.done():
                    pending = self.executor.submit(self._scan)
                self.stop.wait(self.poll)
        self.thread = threading.Thread(target=loop, daemon=True, name="race-catalog-watcher")
        self.thread.start()

    def close(self):
        self.stop.set()
        if getattr(self, "thread", None):
            self.thread.join()
        self.executor.shutdown(wait=True, cancel_futures=True)
        self.planner_executor.shutdown(wait=True, cancel_futures=True)

    def clear_analysis(self):
        # The flag and both job queues are checked under the same lock used to
        # start scans and submit work. Never delete while a writer is active.
        with self.lock:
            if self.clearing or self.scanner.get("busy") or self.planning_job or any(
                    job.get("status") in ("queued", "running") for job in self.jobs.values()):
                raise maintenance.Busy("分析任务正在运行，请稍后再试。")
            self.clearing = True
        try:
            if maintenance.game_running():
                raise maintenance.Busy("游戏正在运行，请退出游戏后再清理。")
            # An index that is a reparse point is never replaced.
            if (self.index_file.exists() or self.index_file.is_symlink()) and not maintenance.safe_path(self.state, self.index_file):
                raise OSError("分析索引路径不安全，未清理。")
            result, retained = maintenance.clear_files(self.game, self.state)
            with self.lock:
                kept = {key: value for key, value in self.races.items()
                        if key in retained or not re.fullmatch(r"race-\d+-\d+-\d+", key)}
                # Publish the index before changing in-memory state; a failure
                # is reported, and retained source files are never removed.
                try:
                    if self.index_blocked and retained:
                        raise OSError("索引无法读取，且部分原始或归档记录未能清理；原索引已保留。")
                    maintenance.write_index(self.state, self.index_file, json_bytes(kept))
                except OSError as error:
                    result["ok"] = False
                    result["skipped_files"].append({"path": str(self.index_file), "reason": str(error)})
                    self.index_blocked, self.index_error = True, str(error)
                else:
                    self.index_blocked, self.index_error = False, None
                self.races = kept
                self.evidence = EvidenceIndex(self.game)
                self.record_index = self.record_module.RecordingIndex(self.layout.recordings)
                if hasattr(self.record_index, "seed"):
                    self.record_index.seed(list(kept.values()))
                self.jobs.clear(); self.planning_job = None
                self.scanner.update(busy=False, pending=0, invalid=[], error=None, last_scan=None)
                self.revision += 1
            return result
        finally:
            with self.lock:
                self.clearing = False

    def snapshot(self):
        from candidate_planners import algorithm_catalogue
        algorithms = algorithm_catalogue()
        with self.lock:
            races = sorted(self.races.values(), key=lambda r: r.get("modified", r.get("mtime", 0)), reverse=True)
            courses = [dict(c) for c in self.course_catalog.get("courses", [])]
            for course in courses:
                if course.get("kind") == "bookmaker":
                    course["record_note"] = "该场地可查看勘探与计算走线。现有赌马日志没有可核实的场地标识，也没有参赛玩家／固定 host，因此不把陪跑骑手冒充 host 作对比。"
            return {"revision": self.revision, "courses": courses, "races": races, "algorithms": algorithms,
                    "scanner": dict(self.scanner), "planner": {"busy": bool(self.planning_job), "job_id": self.planning_job},
                    "precision": {"trajectory": "原始约 0.1 秒采样 · 不分箱、不抽稀", "survey": "缓存原始截面 · 当前存储间距约 3 米",
                                  "speed": "相邻有效 XY 位移 / 游戏时间 · km/h"},
                    "source": str(self.game), "archive": str(self.archive)}

    def submit(self, kind, key, algorithms=None):
        if kind not in ("course", "plan", "compare", "race"):
            raise ValueError("invalid job type")
        if kind == "compare":
            from candidate_planners import selection
            algorithms = selection(algorithms)
        with self.lock:
            if self.clearing:
                raise maintenance.Busy("正在清理分析记录，请稍后再试。")
            if kind in ("plan", "compare") and self.planning_job:
                active = self.jobs[self.planning_job]
                if active["key"] != key or active["kind"] != kind or active.get("algorithms") != algorithms:
                    raise ValueError("另一项走线任务正在计算，请等该任务完成。")
                return {"job_id": self.planning_job, "status": active["status"]}
            # A double-click or polling retry must not enqueue duplicate loads.
            for identifier, job in self.jobs.items():
                if job["kind"] == kind and job["key"] == key and job["status"] in ("queued", "running"):
                    return {"job_id": identifier, "status": job["status"]}
            if kind in ("course", "plan", "compare") and key not in {c["id"] for c in self.course_catalog.get("courses", [])}:
                raise ValueError("unknown course")
            if kind == "race" and key not in self.races:
                raise ValueError("unknown recording")
            self.job_number += 1
            identifier = str(self.job_number)
            self.jobs[identifier] = {"kind": kind, "key": key, "status": "queued", "created": time.time()}
            if kind == "compare":
                self.jobs[identifier]["algorithms"] = algorithms
            if kind in ("plan", "compare"):
                self.planning_job = identifier
            # Do not retain megabytes for every historical request.
            for old in list(self.jobs):
                if len(self.jobs) <= 16:
                    break
                if self.jobs[old]["status"] in ("done", "error"):
                    del self.jobs[old]
        worker = self.planner_executor if kind in ("plan", "compare") else self.executor
        worker.submit(self._run_job, identifier)
        return {"job_id": identifier, "status": "queued"}

    def job(self, identifier):
        return json.loads(self.job_bytes(identifier))

    def job_bytes(self, identifier, after=0):
        with self.lock:
            if identifier not in self.jobs:
                raise KeyError(identifier)
            job = self.jobs[identifier]
            if "response" in job:
                return job["response"]
            events = list(job.get("events", []))
            metadata = {k: v for k, v in job.items() if k != "events"}
        # Events are pre-encoded once by the worker, not megabytes re-encoded on
        # every poll under the server lock. Clients request only unseen events.
        return json_bytes(metadata)[:-1] + b',"events":[' + b','.join(events[max(0, after):]) + b']}'

    def _course(self, course_id, *, plan=False, total_laps=None):
        prepared = self.courses_module.prepare(self.game, course_id, total_laps=total_laps)
        if not prepared.get("plannable", True):
            return {"profiles": [], "path": [], "lap_offsets": [], "survey_available": False,
                    "meta": prepared, "source_note": prepared.get("reason", "此赛事没有固定录制赛道。")}
        output = self.state / "exports" / (course_id + ("-plan" if plan else "-survey") + ".json")
        result = self.courses_module.export_course(prepared, output, plan=plan)
        result["meta"] = {k: v for k, v in prepared.items() if k not in ("boundary", "route_text", "pins_text")}
        result["single_loop"] = prepared.get("single_loop", result.get("single_loop", False))
        result.setdefault("source_note", "按当前缓存离线计算；不是历史开赛时的走线快照。" if plan else "当前原生几何派生通道与实际物理修正。")
        if not result.get("survey_available"):
            result["source_note"] += " 当前没有与该路线匹配的有效通道；未覆盖部分保持未知，规划使用生产回退规则。"
        return result

    def _race(self, identifier):
        with gzip.open(self.archive / (identifier + ".json.gz"), "rt", encoding="utf-8") as stream:
            record = json.load(stream)
        with self.lock:
            record["metadata"].update(self.races[identifier])
        metadata = record["metadata"]
        course = None
        note = "赛道边界为当前原生基底与实际物理修正结果，不是历史开赛快照。"
        if metadata.get("course_id"):
            try:
                course = self._course(metadata["course_id"], total_laps=max(1, int(metadata.get("total_laps") or 1)))
            except Exception as error:
                note += " 当前路线无法匹配本场记录：" + str(error)
                LOG.warning("Record geometry unavailable %s: %s", identifier, error)
        return display_record(record, course, host_name=metadata.get("host_name", "赛事 Host"), note=note)

    def _run_job(self, identifier):
        with self.lock:
            job = self.jobs[identifier]
            job["status"] = "running"
        try:
            if job["kind"] == "compare":
                from candidate_planners import compare
                prepared = self.courses_module.prepare(self.game, job["key"])
                def progress(message):
                    with self.lock:
                        job["progress"] = message
                def notify(event):
                    encoded = json_bytes(event)
                    with self.lock:
                        job.setdefault("events", []).append(encoded)
                        job["revision"] = len(job["events"])
                result = compare(prepared, job["algorithms"], self.state / "comparisons", progress, notify)
            else:
                result = self._race(job["key"]) if job["kind"] == "race" else self._course(job["key"], plan=job["kind"] == "plan")
            # Keep compact JSON, not tens of thousands of Python point dicts
            # per completed request. HTTP polling reuses these exact bytes.
            response = json_bytes({**{k: v for k, v in job.items() if k != "events"}, "status": "done", "result": result})
            with self.lock:
                job.pop("events", None)
                job.update(status="done", response=response)
        except Exception as error:
            LOG.exception("%s job failed", job["kind"])
            with self.lock:
                job.update(status="error", error=str(error))
        finally:
            with self.lock:
                if self.planning_job == identifier:
                    self.planning_job = None


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        LOG.debug(fmt, *args)

    def reply(self, value, code=200):
        self.send_bytes(json_bytes(value), "application/json; charset=utf-8", code)

    def send_bytes(self, data, content_type, code=200):
        if len(data) > 4096 and "gzip" in self.headers.get("Accept-Encoding", ""):
            data, compressed = gzip.compress(data, compresslevel=3), True
        else:
            compressed = False
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; object-src 'none'; frame-ancestors 'self'")
        if compressed:
            self.send_header("Content-Encoding", "gzip")
            self.send_header("Vary", "Accept-Encoding")
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def _local_request(self):
        host = self.headers.get("Host", "").split(":")[0].lower()
        origin = self.headers.get("Origin")
        if host not in ("127.0.0.1", "localhost"):
            return False
        if origin and origin != f"http://{self.headers.get('Host')}":
            return False
        return True

    def do_GET(self):
        if not self._local_request():
            return self.reply({"error": "Local dashboard access only"}, 403)
        parsed = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(parsed.query)
        app = self.server.app
        try:
            if parsed.path == "/api/health":
                return self.reply({"application": APPLICATION, "pid": os.getpid(),
                                   "installation_id": installation_id(app.game), "game_root": str(app.game),
                                   "session_id": getattr(self.server, "session_id", ""),
                                   "control_enabled": bool(getattr(self.server, "control_token", "")),
                                   "status": "stopping" if app.stop.is_set() else "ready",
                                   "capabilities": ["global-settings", "windowless-analysis", "portable-lifecycle"]})
            if parsed.path == "/api/catalog":
                return self.reply(app.snapshot())
            if parsed.path == "/api/settings":
                return self.reply(app.settings.snapshot(query.get("course_id", [""])[0]))
            if parsed.path == "/api/course":
                return self.reply(app.submit("course", query.get("id", [""])[0]), 202)
            if parsed.path.startswith("/api/jobs/"):
                return self.send_bytes(app.job_bytes(parsed.path.rsplit("/", 1)[1], int(query.get("after", [0])[0])), "application/json; charset=utf-8")
            if parsed.path.startswith("/api/races/"):
                return self.reply(app.submit("race", parsed.path.rsplit("/", 1)[1]), 202)
            files = {"/": ("index.html", "text/html; charset=utf-8"), "/index.html": ("index.html", "text/html; charset=utf-8"),
                     "/app.js": ("app.js", "text/javascript; charset=utf-8"), "/style.css": ("style.css", "text/css; charset=utf-8"),
                     "/i18n.js": ("i18n.js", "text/javascript; charset=utf-8"),
                     "/settings-ui.js": ("settings-ui.js", "text/javascript; charset=utf-8"),
                     "/race-ui.js": ("race-ui.js", "text/javascript; charset=utf-8"),
                     "/horse-ui.js": ("horse-ui.js", "text/javascript; charset=utf-8"),
                     "/horse-ui.css": ("horse-ui.css", "text/css; charset=utf-8"),
                     "/assets/itemtype_horse_health.png": ("assets/itemtype_horse_health.png", "image/png"),
                     "/assets/itemtype_horse_stamina.png": ("assets/itemtype_horse_stamina.png", "image/png"),
                     "/assets/transaction_horse_bond.png": ("assets/transaction_horse_bond.png", "image/png"),
                     "/fonts/Rye-Regular.ttf": ("fonts/Rye-Regular.ttf", "font/ttf"),
                     "/fonts/RDR-Lino.woff2": ("fonts/RDR-Lino.woff2", "font/woff2"),
                     "/fonts/Hapna-Slab-Serif.woff": ("fonts/Hapna-Slab-Serif.woff", "font/woff"),
                     "/fonts/Hapna-Slab-Serif-Bold.woff": ("fonts/Hapna-Slab-Serif-Bold.woff", "font/woff"),
                     "/assets/brush-menu.svg": ("assets/brush-menu.svg", "image/svg+xml"),
                     "/assets/brush-cta.svg": ("assets/brush-cta.svg", "image/svg+xml")}
            for asset in ("selection_box_bg_1a", "selection_box_bg_1b", "selection_arrow_right", "list_item_h_line_narrow"):
                filename = f"assets/native-{asset}.png"
                files["/" + filename] = (filename, "image/png")
            if parsed.path not in files:
                return self.reply({"error": "Not found"}, 404)
            filename, content_type = files[parsed.path]
            try:
                content = (HERE / "dashboard" / filename).read_bytes()
            except FileNotFoundError:
                # Optional locally retained fonts are absent on a fresh public
                # installation. Let CSS fall back without logging a server fault.
                return self.reply({"error": "Not found"}, 404)
            return self.send_bytes(content, content_type)
        except (SettingsConflict, maintenance.Busy) as error:
            self.reply({"error": str(error)}, 409)
        except (KeyError, ValueError) as error:
            self.reply({"error": str(error)}, 400 if parsed.path == "/api/settings" else 404)
        except Exception as error:
            LOG.exception("Request failed")
            self.reply({"error": str(error)}, 500)

    def do_POST(self):
        if not self._local_request():
            return self.reply({"error": "Local dashboard access only"}, 403)
        if self.path in ("/api/launcher/shutdown", "/api/launcher/claim"):
            self.close_connection = True
            token = getattr(self.server, "control_token", "")
            if not token or not secrets.compare_digest(self.headers.get("X-Jockey-Club-Control", ""), token):
                return self.reply({"error": "This launcher does not own the backend"}, 403)
            if self.path == "/api/launcher/claim":
                return self.reply({"session_id": getattr(self.server, "session_id", ""), "pid": os.getpid()})
            self.server.app.stop.set()
            LOG.info("Owning launcher requested shutdown for session %s", getattr(self.server, "session_id", ""))
            self.reply({"status": "stopping"})
            threading.Thread(target=self.server.shutdown, daemon=True, name="clubhouse-shutdown").start()
            return
        if self.path not in ("/api/plan", "/api/compare", "/api/settings", "/api/maintenance/clear"):
            return self.reply({"error": "Not found"}, 404)
        try:
            if self.path in ("/api/settings", "/api/maintenance/clear") and self.headers.get("Content-Type", "").split(";")[0].strip() != "application/json":
                return self.reply({"error": "JSON content required"}, 415)
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= (32768 if self.path == "/api/settings" else 4096):
                raise ValueError("Invalid request length")
            body = json.loads(self.rfile.read(length))
            if not isinstance(body, dict):
                raise ValueError("Invalid request body")
            if self.path == "/api/settings":
                return self.reply(self.server.app.settings.apply(body.get("course_id", ""), body.get("revision"), body.get("changes")))
            if self.path == "/api/maintenance/clear":
                if body:
                    raise ValueError("Invalid maintenance request")
                result = self.server.app.clear_analysis()
                details = result.pop("skipped_files")
                result["partial"] = bool(details) or not result["ok"]
                result["ok"] = True
                result["skipped_files"] = len(details)
                result["skipped_details"] = details
                return self.reply(result)
            kind = "compare" if self.path == "/api/compare" else "plan"
            self.reply(self.server.app.submit(kind, str(body.get("course_id", "")), body.get("algorithms")), 202)
        except (SettingsConflict, maintenance.Busy) as error:
            self.reply({"error": str(error)}, 409)
        except (KeyError, ValueError, TypeError) as error:
            self.reply({"error": str(error)}, 400)
        except Exception as error:
            LOG.exception("Settings/request failed")
            self.reply({"error": str(error)}, 500)


class _StartupOutputBudget:
    """Shared Python-stream budget; low-level descriptor writes bypass it."""
    def __init__(self, remaining):
        self.remaining = max(0, remaining)
        self.lock = threading.Lock()


class _LimitedStartupBytes:
    def __init__(self, stream, budget):
        self._stream, self._budget = stream, budget

    def write(self, data):
        with self._budget.lock:
            length = min(len(data), self._budget.remaining)
            if length:
                written = self._stream.write(data[:length])
                self._budget.remaining -= written
            return len(data)

    def __getattr__(self, name):
        return getattr(self._stream, name)


class _LimitedStartupText:
    def __init__(self, stream, budget):
        self._stream, self._budget = stream, budget
        if hasattr(stream, "buffer"):
            self.buffer = _LimitedStartupBytes(stream.buffer, budget)

    def write(self, text):
        with self._budget.lock:
            # Account for encoding and Windows newline conversion.
            encoding = self._stream.encoding or "utf-8"
            permitted = []
            available = self._budget.remaining
            for char in text:
                cost = len(char.encode(encoding, errors="replace")) + (1 if os.name == "nt" and char == "\n" else 0)
                if cost > available:
                    break
                permitted.append(char)
                available -= cost
            if permitted:
                self._stream.write("".join(permitted))
                self._budget.remaining = available
            return len(text)

    def __getattr__(self, name):
        return getattr(self._stream, name)


def limit_launcher_output(state, max_bytes=4 * 1024 * 1024):
    """Cap Python writes only when both streams target launcher startup log."""
    path = state / "backend-startup.log"
    try:
        expected = path.stat()
        streams = (sys.stdout, sys.stderr)
        if not all((lambda info: (info.st_dev, info.st_ino) == (expected.st_dev, expected.st_ino))(
                os.fstat(stream.fileno())) for stream in streams):
            return
        budget = _StartupOutputBudget(max_bytes - expected.st_size)
        sys.stdout, sys.stderr = (_LimitedStartupText(stream, budget) for stream in streams)
    except (OSError, AttributeError, ValueError):
        # Normal terminal output or absent startup log is intentionally left alone.
        return


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", type=Path, default=installed_game_root() or (None if payload_root() else DEFAULT_GAME))
    parser.add_argument("--state", type=Path)
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--port-fallback", action="store_true", help="Use an OS-assigned port if the preferred port is busy")
    parser.add_argument("--control-token-file", type=Path, help="Enable the owning launcher's authenticated shutdown")
    parser.add_argument("--session-id", default=None)
    parser.add_argument("--poll", type=float, default=3)
    args = parser.parse_args()
    if args.game_root is None:
        parser.error("Cannot find RDR2.exe above this portable package; use JockeyClub.exe or --game-root.")
    args.game_root = args.game_root.resolve()
    args.state = (args.state or default_state(args.game_root)).resolve()
    args.state.mkdir(parents=True, exist_ok=True)
    limit_launcher_output(args.state)
    # A failed rotation drops the message instead of allowing unbounded log
    # growth; BaseRotatingHandler.emit does not append after rollover failure.
    logging.raiseExceptions = False
    handler = ByteBoundedLogHandler(args.state / "dashboard.log", maxBytes=4 * 1024 * 1024,
                                  backupCount=1, encoding="utf-8")
    logging.basicConfig(handlers=[handler], level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    # Below-normal CPU scheduling affects this helper and its children only.
    if os.name == "nt":
        import ctypes
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
    lease = InstallationLease(args.game_root)
    app, server = None, None
    session = args.session_id or secrets.token_hex(16)
    discovery = discovery_file(args.game_root)
    metadata = None
    try:
        try:
            server = ClubhouseServer(("127.0.0.1", args.port), Handler)
        except OSError:
            if not args.port_fallback:
                raise
            server = ClubhouseServer(("127.0.0.1", 0), Handler)
        server.daemon_threads = True
        server.session_id = session
        server.control_token = args.control_token_file.read_text(encoding="ascii").strip() if args.control_token_file else ""
        if args.control_token_file and not re.fullmatch("[0-9a-f]{64}", server.control_token):
            raise ValueError("Invalid launcher control token")
        app = Dashboard(args.game_root, args.state, poll=max(1, args.poll))
        server.app = app
        app.start()
        port = server.server_port
        metadata = {"application": APPLICATION, "pid": os.getpid(), "port": port,
                    "url": f"http://127.0.0.1:{port}/", "game_root": str(args.game_root),
                    "installation_id": installation_id(args.game_root), "session_id": session,
                    "state": str(args.state), "status": "ready", "python": sys.executable,
                    "payload": str(payload_root() or ""), "control_enabled": bool(server.control_token)}
        if os.name == "nt":
            metadata["console_window"] = int(ctypes.windll.kernel32.GetConsoleWindow() or 0)
        atomic_json(args.state / "server.json", metadata)
        atomic_json(discovery, metadata)
        LOG.info("Serving %s for %s", metadata["url"], args.game_root)
        server.serve_forever(poll_interval=.5)
    except KeyboardInterrupt:
        pass
    finally:
        if server:
            server.server_close()
        if app:
            app.close()
        if metadata:
            atomic_json(args.state / "server.json", {**metadata, "status": "stopped"})
        remove_owned_discovery(discovery, session)
        lease.close()


if __name__ == "__main__":
    main()
