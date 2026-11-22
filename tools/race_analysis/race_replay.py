"""Time-aligned race replay and gaps; independent from course drawing.

Lap windows follow the player's recorded lap transitions. A leading opponent
remains visible in that same window even when already on the following lap.
Only drawing interpolates adjacent valid samples; archival coordinates stay raw.
"""
import bisect
import math

from horse_cards import describe


def finite(value):
    return isinstance(value, (float, int)) and not isinstance(value, bool) and math.isfinite(value)


def restore_stimulant_timeline(record):
    """Recover timed doses from this recording's paired log, not speed guesses.

    New events use game_ms. Second-resolution legacy wall clocks are accepted
    only when their result anchor agrees with the game's elapsed time; pauses
    or missing log anchors remain unknown rather than shifting the highlight.
    """
    metadata = record["metadata"]
    evidence = {**metadata.get("evidence", {}), **metadata.get("telemetry_evidence", {})}
    samples = record.get("actors", {}).get("player", {}).get("samples", [])
    anchor = next((s for s in samples if finite(s.get("t")) and finite(s.get("game_ms"))), None)
    if not anchor:
        return
    game_origin = anchor["game_ms"] - anchor["t"] * 1000
    result = next((s for s in samples if s.get("race_window") and not s.get("racing") and finite(s.get("t"))), None)
    wall_end = evidence.get("outcome_wall_s")
    wall_aligned = bool(result and finite(wall_end) and abs(result["t"] - wall_end) <= 1.1)
    events = evidence.get("stimulant_events", [])
    mapped = []
    for event in events:
        exact = finite(event.get("game_ms"))
        start = (event["game_ms"] - game_origin) / 1000 if exact else event.get("wall_offset_s") if wall_aligned else None
        if finite(start):
            mapped.append({**event, "t": start, "precision": "game" if exact else "second"})
    # If a legacy event cannot be aligned, do not falsely label its interval OFF.
    if len(mapped) != len(events) or (not mapped and not wall_aligned):
        return
    mapped.sort(key=lambda e: e["t"])
    until, index = -1, 0
    precision = "second" if not mapped or any(e["precision"] == "second" for e in mapped) else "game"
    for sample in samples:
        t = sample.get("t")
        if not finite(t):
            continue
        while index < len(mapped) and mapped[index]["t"] <= t:
            event = mapped[index]
            until = event["t"] + event.get("duration_s", 0) if event["active"] else -1
            index += 1
        if sample.get("stimulant") is None:
            sample["stimulant"] = t < until
            sample["stimulant_precision"] = precision
    metadata["stimulant_timing"] = precision


def restore_entrant_timeline(record):
    """Use explicit paired retirement events; a lost projection alone is not OUT."""
    metadata = record["metadata"]
    evidence = {**metadata.get("evidence", {}), **metadata.get("telemetry_evidence", {})}
    actors = record.get("actors", {})
    samples = actors.get("player", {}).get("samples", [])
    anchor = next((s for s in samples if finite(s.get("t")) and finite(s.get("game_ms"))), None)
    if not anchor:
        return
    result = next((s for s in samples if s.get("race_window") and not s.get("racing") and finite(s.get("t"))), None)
    wall_end = evidence.get("outcome_wall_s")
    aligned = bool(result and finite(wall_end) and abs(result["t"] - wall_end) <= 1.1)
    for event in evidence.get("entrant_out_events", []):
        actor = actors.get(event.get("actor"), {})
        actor_samples = actor.get("samples", [])
        hit = next((s for s in actor_samples if finite(s.get("game_ms")) and s["game_ms"] >= event["game_ms"]), None) if finite(event.get("game_ms")) else None
        start = hit["t"] if hit else None
        precision = "game"
        if start is None and not finite(event.get("game_ms")) and finite(event.get("wall_offset_s")):
            wall = event["wall_offset_s"]
            if event.get("reason") in ("left-corridor", "skipped-section"):
                # Legacy Export conflated lost projection and disqualification.
                # Only the paired explicit OUT event permits this recovery.
                transitions = [s for i,s in enumerate(actor_samples) if s.get("continuity_lost") and
                               i>0 and not actor_samples[i-1].get("continuity_lost") and
                               finite(s.get("t")) and wall - 1.1 <= s["t"] <= wall + 1.1]
                start = transitions[0]["t"] if len(transitions)==1 else None
                precision = "paired event and recorded transition"
            elif aligned:
                start, precision = wall + 1, "second"
        if start is None:
            continue
        for sample in actor.get("samples", []):
            if finite(sample.get("t")) and sample["t"] >= start:
                sample.update(out=True, out_reason=event.get("reason"), out_precision=precision)


def _progress(sample, course, total, bookmaker=False):
    """Return cumulative progress only when the recorded lap agrees with it."""
    along = sample.get("along_m")
    if not course or not finite(along) or sample.get("continuity_lost"):
        return None
    offsets = course.get("lap_offsets") or []
    if len(offsets) < 2:
        return None
    single = bool(course.get("single_loop") or course.get("meta", {}).get("single_loop") or
                  (len(offsets) == 2 and total > 1))
    number = sample.get("reported_lap")
    if single:
        width = offsets[-1] - offsets[0]
        if bookmaker:
            if not 0 <= along <= width * total + .5:
                return None
            if isinstance(number, int) and 1 <= number <= total and not (number - 1) * width - .5 <= along <= number * width + .5:
                return None
            return along
        if not isinstance(number, int) or isinstance(number, bool) or not 1 <= number <= total:
            return None
        if not -.5 <= along <= width + .5:
            return None
        return (number - 1) * width + along
    if not offsets[0] - .5 <= along <= offsets[-1] + .5:
        return None
    if isinstance(number, int) and 1 <= number < len(offsets):
        if not offsets[number - 1] - .5 <= along <= offsets[number] + .5:
            return None
    return along


def _points(actor, course, total, bookmaker=False):
    result = []
    for sample in actor.get("samples", []):
        xyz = sample.get("xyz")
        if not sample.get("race_window", sample.get("racing", False)) or sample.get("chunk") is None:
            continue
        if not finite(sample.get("t")) or not isinstance(xyz, list) or len(xyz) < 3 or not all(finite(x) for x in xyz[:3]):
            continue
        identity = sample.get("identity") or {}
        forward = sample.get("forward")
        heading = math.atan2(forward[1], forward[0]) if isinstance(forward, list) and len(forward) >= 2 and all(finite(v) for v in forward[:2]) and math.hypot(*forward[:2]) > .001 else None
        result.append({"t": sample["t"], "x": xyz[0], "y": xyz[1], "z": xyz[2],
                       "chunk": sample["chunk"], "entity_id": ":".join(str(identity.get(k, "")) for k in ("horse", "model", "rider")),
                       "s": _progress(sample, course, total, bookmaker), "speed": sample.get("speed_kmh"),
                       "out": sample.get("out", False), "dt_s": sample.get("dt_s"),
                       "boost": sample.get("boost"), "stimulant": sample.get("stimulant"),
                       "heading": heading,
                       "racing": sample.get("racing", sample.get("race_window", False))})
    return result


def continuous(a, b, max_gap=.3):
    return (a["chunk"] == b["chunk"] and a["entity_id"] == b["entity_id"] and
            0 < b["t"] - a["t"] <= max_gap + 1e-9)


def _at(points, times, t, max_gap):
    index = bisect.bisect_left(times, t)
    if index < len(points) and times[index] == t:
        return points[index]
    if index == 0 or index == len(points):
        return None
    a, b = points[index - 1:index + 1]
    if not continuous(a, b, max_gap) or not finite(a["s"]) or not finite(b["s"]):
        return None
    factor = (t - a["t"]) / (b["t"] - a["t"])
    return {**a, "t": t, "s": a["s"] + factor * (b["s"] - a["s"])}


def _statistics(points, start, end, max_gap):
    distance = duration = 0
    peak = None
    for a, b in zip(points, points[1:]):
        if not continuous(a, b, max_gap) or not finite(b["speed"]):
            continue
        overlap = max(0, min(b["t"], end) - max(a["t"], start))
        if overlap <= 0:
            continue
        duration += overlap
        distance += overlap * b["speed"] / 3.6
        peak = max(peak or 0, b["speed"])
    return {"duration_s": max(0, end - start), "peak_speed_kmh": peak,
            "average_speed_kmh": distance / duration * 3.6 if duration else None}


def enrich_display(display, record):
    course = display.get("course")
    total = max(1, record["metadata"].get("total_laps") or 1)
    max_gap = record["metadata"].get("max_gap_s", .3)
    actors = record.get("actors", {})
    bookmaker = record["metadata"].get("kind") == "bookmaker"
    tracks = {key: _points(actor, course, total, bookmaker) for key, actor in actors.items()}
    # A broken clock is never silently sorted into a plausible connected path.
    for points in tracks.values():
        if any(b["t"] <= a["t"] for a, b in zip(points, points[1:])):
            for i, point in enumerate(points):
                point["chunk"] = f"broken-clock-{i}"
            points.sort(key=lambda p: p["t"])
    times = {key: [p["t"] for p in points] for key, points in tracks.items()}
    labels = {key: ("观赛玩家" if bookmaker else "玩家") if key == "player" else display["host_name"] if key == "host" else actor.get("label", key)
              for key, actor in actors.items()}
    roles = {key: key if key in ("player", "host") else "jockey" for key in actors}
    def horse_snapshot(actor):
        if actor.get("horse"):
            return actor["horse"]
        # v1 retained entity/model identity even though it omitted attributes.
        return next((s for s in actor.get("samples", []) if s.get("identity") and s.get("race_window")), None)
    display["horses"] = [describe(horse_snapshot(actor), key, labels[key], roles[key]) for key, actor in actors.items()
                         if tracks[key]]
    # Legacy motion did not contain bonding. Only the first inventory snapshot
    # paired to this recording may supply it, never another race/current horse.
    evidence = record["metadata"].get("telemetry_evidence") or record["metadata"].get("evidence") or {}
    bond = evidence.get("player_bonding_rank")
    if isinstance(bond, int) and not isinstance(bond, bool) and 0 <= bond <= 4:
        for horse in display["horses"]:
            if horse["role"] == "player":
                horse["attributes"] = dict(horse["attributes"])
                horse["attributes"].setdefault("bonding", {"value": bond, "max": 4, "source": "paired start log"})
    outcome = record["metadata"].get("outcome_evidence") or record["metadata"].get("evidence") or {}
    reason = outcome.get("reason") or record["metadata"].get("reason")
    accident_win = bool(reason and reason.startswith("result: player won after opponent accident: "))
    if not bookmaker and (accident_win or reason in ("result: player won", "result: opponent won")):
        winner = evidence.get("winner", {}).get("actor")
        if accident_win or reason == "result: player won":
            text, winner = "玩家获胜（对手意外出局，提前完赛）" if accident_win else "玩家获胜", "player"
        elif winner in labels and winner != "player":
            text = labels[winner] + " 获胜"
        elif any(key.startswith("jockey-") for key in tracks) or any(p["out"] for p in tracks.get("host", [])):
            text = "其他骑手获胜" if any(p["out"] for p in tracks.get("host", [])) else "对手获胜（获胜骑手未记录）"
            winner = None
        else:
            text, winner = display["host_name"] + " 获胜", "host"
        display["metadata"] = {**display["metadata"], "outcome": text, "winner_id": winner}
    if bookmaker:
        result = record["metadata"].get("bookmaker_result") or {}
        winner_row = next((r for r in result.get("entrants", []) if isinstance(r, dict) and r.get("place") == 1), None)
        if result.get("completed") is True and winner_row and isinstance(winner_row.get("index"), int):
            index = winner_row["index"]
            winner = "host" if index == 0 else f"jockey-{index}"
            entrants = (record["metadata"].get("bookmaker") or {}).get("entrants", [])
            entry = next((e for e in entrants if isinstance(e, dict) and e.get("index") == index), {})
            name = labels.get(winner) or entry.get("name_zh") or entry.get("name") or winner
            outcome_text = name + (" 获胜（最后在场选手）" if winner_row.get("survival_win") is True else " 获胜")
            display["metadata"] = {**display["metadata"], "outcome": outcome_text, "winner_id": winner}
    player, host = tracks.get("player", []), tracks.get("host", [])
    participants = sum(bool(points) for points in tracks.values())
    display["statistics"] = {"participant_count": participants}
    if player:
        display["statistics"].update(_statistics(player, player[0]["t"], player[-1]["t"], max_gap))
    lap_views = display["laps"]
    offsets = course.get("lap_offsets", []) if course else []
    single = bool(course and (course.get("single_loop") or course.get("meta", {}).get("single_loop") or
                             (len(offsets) == 2 and total > 1)))
    for i, lap in enumerate(lap_views):
        lap["replay"] = None
        lap["gap_series"] = []
        rider_samples = {"host": lap["host"], **lap.get("riders", {})} if bookmaker else {"player": lap["player"]}
        samples = [s for items in rider_samples.values() for s in items if finite(s.get("t"))]
        if not samples:
            continue
        start = min(s["t"] for s in samples) if bookmaker else samples[0]["t"]
        end = max(s["t"] for s in samples) if bookmaker else samples[-1]["t"]
        complete = False
        # The transition sample belongs to the next lap, and closes this one.
        if bookmaker and i + 1 < len(lap_views) and lap.get("lap") is not None and lap_views[i + 1].get("lap") == lap["lap"] + 1:
            following = {"host": lap_views[i + 1]["host"], **lap_views[i + 1].get("riders", {})}
            for key, current in rider_samples.items():
                later = following.get(key, [])
                if current and later and finite(current[-1].get("t")) and finite(later[0].get("t")):
                    last, transition = current[-1]["t"], later[0]["t"]
                    if 0 <= transition - last <= max_gap + 1e-9:
                        end, complete = max(end, transition), True
        elif not bookmaker and i + 1 < len(lap_views) and lap_views[i + 1]["player"] and lap.get("lap") is not None:
            transition = lap_views[i + 1]["player"][0]["t"]
            if finite(transition) and transition >= end and lap_views[i + 1].get("lap") == lap["lap"] + 1:
                end, complete = transition, True
        if bookmaker and not complete and lap.get("lap") == total and finite(lap.get("length")):
            result = record["metadata"].get("bookmaker_result") or {}
            if result.get("completed") is True:
                for rider in result.get("entrants", []):
                    if not isinstance(rider, dict) or rider.get("status") != "finished" or rider.get("survival_win") is True:
                        continue
                    index = rider.get("index")
                    if not isinstance(index, int) or isinstance(index, bool):
                        continue
                    key = "host" if index == 0 else f"jockey-{index}"
                    current = rider_samples.get(key, [])
                    if current and finite(current[-1].get("s")) and current[-1]["s"] >= lap["length"] - .5:
                        complete = True
                        break
        if not bookmaker and not complete and lap.get("lap") == total and player:
            last = player[-1]
            length = offsets[-1] * total if single else offsets[-1] if offsets else None
            complete = bool(finite(length) and finite(last["s"]) and last["s"] >= length - .5)
            outcome = record["metadata"].get("outcome_evidence") or record["metadata"].get("evidence") or {}
            complete |= outcome.get("reason") == "result: player won"
        origin = ((lap["lap"] - 1) * offsets[-1] if single and lap.get("lap") else lap["start"])
        replay_actors = []
        for key, points in tracks.items():
            clock = times[key]
            if bookmaker:
                inside_lo, inside_hi = bisect.bisect_left(clock, start), bisect.bisect_right(clock, end)
                if inside_lo == inside_hi and not (inside_lo > 0 and inside_hi < len(clock) and
                    continuous(points[inside_lo - 1], points[inside_hi], max_gap)):
                    continue
            lo, hi = max(0, bisect.bisect_left(clock, start) - 1), min(len(clock), bisect.bisect_right(clock, end) + 1)
            selected = [{k: v for k, v in p.items() if k not in ("dt_s", "racing")} for p in points[lo:hi]]
            for point in selected:
                point["s"] = point["s"] - origin if finite(point["s"]) else None
            if selected:
                replay_actors.append({"id": key, "role": roles[key], "name": labels[key], "points": selected})
        lap["replay"] = {"start_t": start, "end_t": end, "complete": complete,
                         "actors": replay_actors, "max_gap_s": max_gap}
        lap["statistics"] = {} if bookmaker else _statistics(player, start, end, max_gap)
        gap_chunk, previous_pair = 0, None
        p_lo, p_hi = bisect.bisect_left(times.get("player", []), start), bisect.bisect_right(times.get("player", []), end)
        for p in player[p_lo:p_hi]:
            h = _at(host, times.get("host", []), p["t"], max_gap)
            valid = h is not None and finite(p["s"]) and finite(h["s"]) and not h["out"] and not p["out"]
            pair = (p["chunk"], h["chunk"]) if valid else None
            if pair != previous_pair or not valid:
                gap_chunk += 1
            previous_pair = pair
            lap["gap_series"].append({"t": p["t"], "s": p["s"] - origin if finite(p["s"]) else None,
                                      "gap": p["s"] - h["s"] if valid else None, "chunk": gap_chunk})
    return display
