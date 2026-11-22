"""Explicit, read-only comparisons on one frozen course snapshot.

Each child uses the production corridor preparation and final verification.
Only the optimizer varies. Work is serialized by the dashboard, below normal
priority, never run by RDR2. Failed candidates cannot replace successful ones.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
from functools import lru_cache
import subprocess
import time

import live_courses
from analysis_process import run_analysis
from clubhouse_runtime import helper_path, planner_python, payload_root
from output_policy import bounded_workspace
from storage_retention import StorageRetention

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ALGORITHMS = (
    {"id": "current", "name": "K1999 · 正式", "color": "#edc447", "description": "游戏使用的多尺度曲率插值；每层最多 100 次迭代，保留统一边界校验。", "default": True},
    {"id": "legacy", "name": "旧版 · 四次曲率", "color": "#ece8df", "description": "0.28.117 及以前的正式算法，保留用于对照。", "default": True},
    {"id": "hybrid", "name": "K1999 + 四次曲率", "color": "#71b4c4", "description": "以 K1999 为初始线，继续执行旧版四次曲率优化。", "default": True},
    {"id": "sqp2", "name": "SQP · 二次曲率", "color": "#b69bdd", "description": "独立几何 SQP 实验：最小化沿线曲率平方积分，计算较慢。", "default": False},
    {"id": "sqp4", "name": "SQP · 四次曲率", "color": "#ed9277", "description": "独立几何 SQP 实验：更重视高曲率峰值，计算较慢。", "default": False},
)


def selection(values=None):
    if values is None:
        values = [a["id"] for a in ALGORITHMS if a["default"]]
    if not isinstance(values, list) or not values or not all(isinstance(v, str) for v in values):
        raise ValueError("至少选择一个候选算法。")
    if len(values) != len(set(values)) or set(values) - {a["id"] for a in ALGORITHMS}:
        raise ValueError("算法选择无效。")
    if any(v.startswith("sqp") for v in values):
        available, reason = sqp_capability()
        if not available:
            raise ValueError(reason)
    return [a["id"] for a in ALGORITHMS if a["id"] in values]


def executable():
    path = helper_path("JOCKEY_CANDIDATE_EXPORTER", "race_analysis_candidates.exe")
    if not path.is_file():
        raise ValueError("请先构建 race_analysis_candidates 离线工具。")
    return path


def python_runtime():
    path = planner_python()
    if not path.is_file():
        raise ValueError("SQP 依赖尚未安装；参见赛事分析室 README 的候选算法安装步骤。")
    return path


@lru_cache(maxsize=1)
def sqp_capability():
    if not planner_python().is_file():
        return False, ("便携精简版未包含 SQP 的 NumPy / SciPy 依赖。" if payload_root() else
                       "SQP 依赖尚未安装；参见赛事分析室 README 的候选算法安装步骤。")
    try:
        probe = run_analysis([str(planner_python()), "-I", "-c", "import numpy, scipy.optimize"],
                             capture_output=True, timeout=15)
        if probe.returncode == 0:
            return True, ""
    except (OSError, subprocess.SubprocessError):
        pass
    return False, "SQP 的 NumPy / SciPy 运行时不可用，请修复依赖后重新打开 Clubhouse。"


def algorithm_catalogue():
    available, reason = sqp_capability()
    return [{**a, "available": available if a["id"].startswith("sqp") else True,
             "unavailable_reason": reason if a["id"].startswith("sqp") and not available else ""}
            for a in ALGORITHMS]


def metrics(planning):
    points = planning["points"]
    length = 0.0
    peak, energy = 0.0, 0.0
    for a, b in zip(points, points[1:]):
        length += math.hypot(b[1] - a[1], b[2] - a[2])
    for a, b, c in zip(points, points[1:], points[2:]):
        ux, uy, vx, vy = b[1]-a[1], b[2]-a[2], c[1]-b[1], c[2]-b[2]
        h1, h2, h3 = math.hypot(ux, uy), math.hypot(vx, vy), math.hypot(c[1]-a[1], c[2]-a[2])
        k = abs(2 * (ux*vy - uy*vx)) / max(1e-12, h1*h2*h3)
        peak = max(peak, k)
        energy += (h1+h2)*.5*k*k
    return {"length_m": length, "peak_curvature": peak, "curvature_energy": energy}


def snapshot_id(prepared, exporter):
    # A cache can only be reused for identical content, parameters and binaries.
    identity = {k: v for k, v in prepared.items() if k not in ("boundary", "input_files")}
    identity["input_hashes"] = {p: info["sha256"] for p, info in prepared["input_files"].items()}
    identity["solver_hashes"] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in (exporter, live_courses.exporter_path(), Path(__file__), HERE / "candidate_sqp.py", HERE / "requirements-planners.txt")}
    return hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()


def compare(prepared, values, cache, progress=lambda text: None, notify=lambda event: None):
    values = selection(values)
    corridor = live_courses.freeze_inputs(prepared)
    exporter = executable()
    snapshot = snapshot_id(prepared, exporter)
    cache_root = Path(cache)
    cache = cache_root / snapshot
    results = []
    started = time.perf_counter()
    base = json.loads(json.dumps(prepared["boundary"]))
    base["meta"] = {k: v for k, v in prepared.items() if k not in ("boundary", "route_text", "pins_text")}
    base["single_loop"] = prepared.get("single_loop", base.get("single_loop", False))
    base["comparison"] = {"snapshot": snapshot, "created": time.time(),
                          "algorithms": [{**a, "status": "queued"} for a in ALGORITHMS if a["id"] in values],
                          "input_files": prepared["input_files"]}
    base["source_note"] = "同一份通道、余量与边界校验；候选只用于离线比较。"
    notify({"type": "start", "course": base})
    expected = {'constraints.json', 'sqp2.txt', 'sqp2.json', 'sqp4.txt', 'sqp4.json'}
    expected.update(kind + '/' + name for kind in ('current', 'legacy', 'hybrid', 'sqp-start', 'sqp2', 'sqp4')
                    for name in live_courses.workspace_outputs(corridor))
    with bounded_workspace(cache_root, 'comparison', expected=expected) as folder:
        folder = Path(folder)
        constraints = folder / "constraints.json"
        k_ms = None

        def native(kind, name, extra=()):
            target = folder / name
            target.mkdir(exist_ok=True)
            return live_courses._native(prepared, target, plan=True, corridor_bytes=corridor,
                executable=exporter, solver_args=["--algorithm", kind, *extra])["planning"]

        for index, key in enumerate(values):
            descriptor = next(a for a in ALGORITHMS if a["id"] == key)
            progress(f"{index+1}/{len(values)} · 正在计算 {descriptor['name']}")
            notify({"type": "status", "id": key, "status": "running"})
            output = cache / (key + ".json")
            try:
                cached = json.loads(output.read_text(encoding="utf-8"))
                if cached.get("snapshot") == snapshot and cached.get("id") == key and cached.get("status") == "done":
                    result = {**cached, "cached": True}
                    results.append(result)
                    notify({"type": "candidate", "candidate": result})
                    continue
            except (OSError, ValueError):
                pass
            t = time.perf_counter()
            try:
                extra = {}
                if key in ("current", "legacy", "hybrid"):
                    planning = native(key, key)
                    solve_ms = planning["milliseconds"]
                    if key == "current":
                        k_ms = solve_ms
                else:
                    if not constraints.exists():
                        seed_plan = native("k1999", "sqp-start", ["--constraints", str(constraints)])
                        k_ms = seed_plan["milliseconds"]
                    if not constraints.exists():
                        raise ValueError("该通道直接采用原录制线，没有可优化的自由变量。")
                    seed = folder / (key + ".txt")
                    env = dict(os.environ, OPENBLAS_NUM_THREADS="1", OMP_NUM_THREADS="1")
                    try:
                        proc = run_analysis([str(python_runtime()), str(HERE / "candidate_sqp.py"),
                            str(constraints), str(seed), "--power", key[-1]], env=env,
                            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
                    except subprocess.TimeoutExpired as error:
                        raise ValueError("SQP 超过 60 秒计算预算；本轮未绘制此候选。") from error
                    if proc.returncode:
                        raise ValueError((proc.stderr.strip() or proc.stdout.strip()).splitlines()[-1])
                    extra = json.loads(seed.with_suffix(".json").read_text(encoding="utf-8"))
                    planning = native("seed", key, ["--seed", str(seed)])
                    solve_ms = (k_ms or 0) + extra["solve_ms"] + planning["milliseconds"]
                if not planning.get("verified"):
                    raise ValueError("候选未通过统一走廊校验，未绘制。")
                result = {**descriptor, "status": "done", "snapshot": snapshot, "cached": False,
                          "planning": planning, "metrics": metrics(planning), "solver_ms": solve_ms,
                          "wall_ms": (time.perf_counter()-t)*1000, "detail": extra}
                encoded = json.dumps(result, ensure_ascii=False, allow_nan=False, separators=(",", ":"))
                StorageRetention.publish_comparison(cache_root, output, encoded.encode('utf-8'))
                results.append(result)
            except Exception as error:
                results.append({**descriptor, "status": "error", "error": str(error),
                                "wall_ms": (time.perf_counter()-t)*1000})
            notify({"type": "candidate", "candidate": results[-1]})
    base["comparison"].update(algorithms=results, wall_ms=(time.perf_counter()-started)*1000)
    return base
