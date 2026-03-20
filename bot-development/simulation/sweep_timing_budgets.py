#!/usr/bin/env python3
import argparse
import csv
import json
import random
import re
import subprocess
import sys
import time
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Dict, List, Optional, Tuple


CONST_KEYS = [
    "TURN_PHASE_FORWARD_BFS_PCT",
    "TURN_PHASE_BACKWARD_BFS_PCT",
    "TURN_PHASE_LOCAL_COMBAT_PCT",
    "TURN_PHASE_POWER_PLANNER_PCT",
    "TURN_PHASE_PATH_AND_SCORING_PCT",
    "TURN_WRAPUP_BUFFER_PCT",
]

DEFAULT_CONFIG = {
    "TURN_PHASE_FORWARD_BFS_PCT": 15,
    "TURN_PHASE_BACKWARD_BFS_PCT": 20,
    "TURN_PHASE_LOCAL_COMBAT_PCT": 20,
    "TURN_PHASE_POWER_PLANNER_PCT": 25,
    "TURN_PHASE_PATH_AND_SCORING_PCT": 15,
    "TURN_WRAPUP_BUFFER_PCT": 5,
}

UTIL_RE = re.compile(
    r"Turn\s+(\d+)\s+elapsed:\s+(\d+)ys, output:.*?"
    r"util:\s*([0-9.]+)%.*?"
    r"phase_ms:\s*"
    r"forward_bfs=([0-9.]+)/([0-9]+),"
    r"backward_bfs=([0-9.]+)/([0-9]+),"
    r"local_combat=([0-9.]+)/([0-9]+),"
    r"power_planner=([0-9.]+)/([0-9]+),"
    r"path_scoring=([0-9.]+)/([0-9]+),\s*"
    r"wrapup_pct=([0-9]+)"
)

SCORE_RE = re.compile(r"Scores \(P1, P2\): \((-?\d+),\s*(-?\d+)\)")


@dataclass
class RunResult:
    index: int
    config: Dict[str, int]
    p1_score: int
    p2_score: int
    turns: int
    avg_util_pct: float
    min_util_pct: float
    max_util_pct: float
    in_band_ratio_pct: float
    avg_elapsed_ms: float
    phase_forward_ms_per_turn: float
    phase_backward_ms_per_turn: float
    phase_combat_ms_per_turn: float
    phase_planner_ms_per_turn: float
    phase_path_ms_per_turn: float
    phase_forward_share_pct: float
    phase_backward_share_pct: float
    phase_combat_share_pct: float
    phase_planner_share_pct: float
    phase_path_share_pct: float
    log_file: str
    score_metric: float


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def write_text(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8")


def update_constants(source_text: str, config: Dict[str, int]) -> str:
    updated = source_text
    for key, value in config.items():
        pattern = rf"(static constexpr int\s+{re.escape(key)}\s*=\s*)\d+(;)"
        updated, count = re.subn(pattern, rf"\g<1>{value}\2", updated)
        if count != 1:
            raise RuntimeError(f"Expected to update exactly one constant for {key}, updated {count}")
    return updated


def split_random(total: int, parts: int, min_each: int, rng: random.Random) -> List[int]:
    if total < min_each * parts:
        raise ValueError("total too small for min_each constraint")
    remaining = total - min_each * parts
    cuts = sorted(rng.randint(0, remaining) for _ in range(parts - 1))
    bins = []
    prev = 0
    for cut in cuts:
        bins.append(cut - prev)
        prev = cut
    bins.append(remaining - prev)
    return [min_each + b for b in bins]


def generate_configs(trials: int, seed: int, include_default: bool) -> List[Dict[str, int]]:
    rng = random.Random(seed)
    configs: List[Dict[str, int]] = []

    if include_default:
        configs.append(dict(DEFAULT_CONFIG))

    while len(configs) < trials:
        wrapup = rng.randint(3, 15)
        remaining = 100 - wrapup
        fwd, bwd, combat, planner, path = split_random(remaining, 5, 5, rng)

        cfg = {
            "TURN_PHASE_FORWARD_BFS_PCT": fwd,
            "TURN_PHASE_BACKWARD_BFS_PCT": bwd,
            "TURN_PHASE_LOCAL_COMBAT_PCT": combat,
            "TURN_PHASE_POWER_PLANNER_PCT": planner,
            "TURN_PHASE_PATH_AND_SCORING_PCT": path,
            "TURN_WRAPUP_BUFFER_PCT": wrapup,
        }
        if sum(cfg[k] for k in CONST_KEYS) != 100:
            continue
        if cfg in configs:
            continue
        configs.append(cfg)

    return configs


def load_configs_file(configs_file: Path) -> List[Dict[str, int]]:
    payload = json.loads(configs_file.read_text(encoding="utf-8"))
    if not isinstance(payload, list) or not payload:
        raise RuntimeError("configs file must be a non-empty JSON array")

    configs: List[Dict[str, int]] = []
    for idx, item in enumerate(payload, start=1):
        if not isinstance(item, dict):
            raise RuntimeError(f"config #{idx} must be an object")
        cfg: Dict[str, int] = {}
        for key in CONST_KEYS:
            if key not in item:
                raise RuntimeError(f"config #{idx} missing key: {key}")
            val = item[key]
            if not isinstance(val, int):
                raise RuntimeError(f"config #{idx} key {key} must be int")
            cfg[key] = val
        if sum(cfg[k] for k in CONST_KEYS) != 100:
            raise RuntimeError(f"config #{idx} has invalid sum: {sum(cfg[k] for k in CONST_KEYS)}")
        configs.append(cfg)

    return configs


def newest_matching_log(winter_dir: Path, log_glob: str, not_before: float) -> Optional[Path]:
    candidates = sorted(winter_dir.glob(log_glob), key=lambda p: p.stat().st_mtime)
    for path in reversed(candidates):
        if path.stat().st_mtime >= not_before:
            return path
    return candidates[-1] if candidates else None


def parse_log_metrics(log_file: Path) -> Dict[str, float]:
    turns = 0
    util_values: List[float] = []
    elapsed_ms_values: List[float] = []

    forward_used = backward_used = combat_used = planner_used = path_used = 0.0

    with log_file.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = UTIL_RE.search(line)
            if not m:
                continue
            turns += 1
            elapsed_us = int(m.group(2))
            util_values.append(float(m.group(3)))
            elapsed_ms_values.append(elapsed_us / 1000.0)

            forward_used += float(m.group(4))
            backward_used += float(m.group(6))
            combat_used += float(m.group(8))
            planner_used += float(m.group(10))
            path_used += float(m.group(12))

    if turns == 0:
        raise RuntimeError(f"No timing lines parsed from {log_file}")

    phase_total = forward_used + backward_used + combat_used + planner_used + path_used

    def share(v: float) -> float:
        return (v * 100.0 / phase_total) if phase_total > 0 else 0.0

    in_band = sum(1 for v in util_values if 90.0 <= v <= 100.0)

    return {
        "turns": turns,
        "avg_util_pct": sum(util_values) / turns,
        "min_util_pct": min(util_values),
        "max_util_pct": max(util_values),
        "in_band_ratio_pct": in_band * 100.0 / turns,
        "avg_elapsed_ms": sum(elapsed_ms_values) / turns,
        "phase_forward_ms_per_turn": forward_used / turns,
        "phase_backward_ms_per_turn": backward_used / turns,
        "phase_combat_ms_per_turn": combat_used / turns,
        "phase_planner_ms_per_turn": planner_used / turns,
        "phase_path_ms_per_turn": path_used / turns,
        "phase_forward_share_pct": share(forward_used),
        "phase_backward_share_pct": share(backward_used),
        "phase_combat_share_pct": share(combat_used),
        "phase_planner_share_pct": share(planner_used),
        "phase_path_share_pct": share(path_used),
    }


def run_command(command: List[str], cwd: Path) -> subprocess.CompletedProcess:
    return subprocess.run(command, cwd=str(cwd), capture_output=True, text=True)


def compile_bot(bot_dir: Path, src_file: str, out_file: str) -> None:
    cmd = ["g++", "-O2", "-std=c++17", src_file, "-o", out_file]
    proc = run_command(cmd, bot_dir)
    if proc.returncode != 0:
        raise RuntimeError(f"Compile failed:\n{proc.stdout}\n{proc.stderr}")


def run_map11(sim_dir: Path, bot1_path: Path, bot2_path: Path, map_file: Path) -> Tuple[int, int, str]:
    cmd = [
        "python3",
        "run_simulation.py",
        str(bot1_path),
        str(bot2_path),
        "--map",
        str(map_file),
    ]
    proc = run_command(cmd, sim_dir)
    if proc.returncode != 0:
        raise RuntimeError(f"Simulation command failed:\n{proc.stdout}\n{proc.stderr}")

    score_match = SCORE_RE.search(proc.stdout)
    if not score_match:
        raise RuntimeError(f"Could not parse scores from simulation output:\n{proc.stdout}\n{proc.stderr}")

    return int(score_match.group(1)), int(score_match.group(2)), proc.stdout


def metric(p1_score: int, p2_score: int, avg_util: float, in_band_ratio: float) -> float:
    util_center_penalty = abs(avg_util - 95.0)
    return (p1_score * 100.0) - (p2_score * 5.0) + (in_band_ratio * 0.5) - util_center_penalty


def write_reports(results: List[RunResult], out_csv: Path, out_json: Path) -> None:
    fieldnames = [
        "rank",
        "index",
        *CONST_KEYS,
        "p1_score",
        "p2_score",
        "score_metric",
        "turns",
        "avg_util_pct",
        "min_util_pct",
        "max_util_pct",
        "in_band_ratio_pct",
        "avg_elapsed_ms",
        "phase_forward_ms_per_turn",
        "phase_backward_ms_per_turn",
        "phase_combat_ms_per_turn",
        "phase_planner_ms_per_turn",
        "phase_path_ms_per_turn",
        "phase_forward_share_pct",
        "phase_backward_share_pct",
        "phase_combat_share_pct",
        "phase_planner_share_pct",
        "phase_path_share_pct",
        "log_file",
    ]

    with out_csv.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for rank, r in enumerate(results, start=1):
            row = {"rank": rank, "index": r.index, **r.config}
            row.update({
                "p1_score": r.p1_score,
                "p2_score": r.p2_score,
                "score_metric": round(r.score_metric, 4),
                "turns": r.turns,
                "avg_util_pct": round(r.avg_util_pct, 4),
                "min_util_pct": round(r.min_util_pct, 4),
                "max_util_pct": round(r.max_util_pct, 4),
                "in_band_ratio_pct": round(r.in_band_ratio_pct, 4),
                "avg_elapsed_ms": round(r.avg_elapsed_ms, 4),
                "phase_forward_ms_per_turn": round(r.phase_forward_ms_per_turn, 4),
                "phase_backward_ms_per_turn": round(r.phase_backward_ms_per_turn, 4),
                "phase_combat_ms_per_turn": round(r.phase_combat_ms_per_turn, 4),
                "phase_planner_ms_per_turn": round(r.phase_planner_ms_per_turn, 4),
                "phase_path_ms_per_turn": round(r.phase_path_ms_per_turn, 4),
                "phase_forward_share_pct": round(r.phase_forward_share_pct, 4),
                "phase_backward_share_pct": round(r.phase_backward_share_pct, 4),
                "phase_combat_share_pct": round(r.phase_combat_share_pct, 4),
                "phase_planner_share_pct": round(r.phase_planner_share_pct, 4),
                "phase_path_share_pct": round(r.phase_path_share_pct, 4),
                "log_file": r.log_file,
            })
            writer.writerow(row)

    payload = [asdict(r) for r in results]
    out_json.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Sweep timing budget percentages and benchmark a bot on a selected map")
    parser.add_argument("--trials", type=int, default=12, help="Number of configurations to run (including default if enabled)")
    parser.add_argument("--seed", type=int, default=20260318, help="RNG seed for random configuration generation")
    parser.add_argument("--no-default", action="store_true", help="Do not include default timing configuration")
    parser.add_argument("--keep-best", type=int, default=10, help="Number of top results to print")
    parser.add_argument("--map", default="../test-maps/complex-pathing/11-bigmap-E45Sx-long-term-target.txt", help="Map path relative to simulation dir")
    parser.add_argument("--opponent", default="../bots/epic4-solver-bot.exe", help="Opponent bot path relative to simulation dir")
    parser.add_argument("--source", default="epic4-solver-BFS-bot.cpp", help="Bot source filename relative to bots dir")
    parser.add_argument("--output-bot", default="epic4-solver-BFS-bot.exe", help="Output bot executable filename relative to bots dir")
    parser.add_argument("--log-glob", default="epic4_bfs_bot_log_*.txt", help="Log glob pattern in WinterChallenge2026-Exotec")
    parser.add_argument("--out-csv", default="timing_sweep_results.csv", help="CSV output path (relative to simulation dir)")
    parser.add_argument("--out-json", default="timing_sweep_results.json", help="JSON output path (relative to simulation dir)")
    parser.add_argument("--configs-file", default=None, help="Optional JSON file with explicit list of timing configs")
    args = parser.parse_args()

    sim_dir = Path(__file__).resolve().parent
    bot_dir = (sim_dir / "../bots").resolve()
    repo_root = sim_dir.parent.parent
    winter_dir = (repo_root / "WinterChallenge2026-Exotec").resolve()

    cpp_path = (bot_dir / args.source).resolve()
    exe_path = (bot_dir / args.output_bot).resolve()
    opponent_path = (sim_dir / args.opponent).resolve()
    map_path = (sim_dir / args.map).resolve()
    out_csv = (sim_dir / args.out_csv).resolve()
    out_json = (sim_dir / args.out_json).resolve()

    if not cpp_path.exists():
        print(f"Missing source file: {cpp_path}", file=sys.stderr)
        return 2
    if not opponent_path.exists():
        print(f"Missing opponent bot: {opponent_path}", file=sys.stderr)
        return 2
    if not map_path.exists():
        print(f"Missing map file: {map_path}", file=sys.stderr)
        return 2

    if args.configs_file:
        configs_path = (sim_dir / args.configs_file).resolve()
        if not configs_path.exists():
            print(f"Missing configs file: {configs_path}", file=sys.stderr)
            return 2
        configs = load_configs_file(configs_path)
    else:
        include_default = not args.no_default
        configs = generate_configs(args.trials, args.seed, include_default)

    original_source = read_text(cpp_path)
    results: List[RunResult] = []

    print(f"Running {len(configs)} timing configurations on map: {map_path.name}")
    print(f"Output: {out_csv.name}, {out_json.name}")

    try:
        for idx, cfg in enumerate(configs, start=1):
            cfg_sum = sum(cfg[k] for k in CONST_KEYS)
            if cfg_sum != 100:
                raise RuntimeError(f"Invalid config sum ({cfg_sum}) at index {idx}")

            print("-" * 72)
            print(f"[{idx}/{len(configs)}] config={cfg}")

            patched = update_constants(original_source, cfg)
            write_text(cpp_path, patched)

            compile_bot(bot_dir, args.source, args.output_bot)

            before_ts = time.time()
            p1_score, p2_score, sim_stdout = run_map11(sim_dir, exe_path, opponent_path, map_path)
            log_file = newest_matching_log(winter_dir, args.log_glob, before_ts - 1.0)
            if log_file is None:
                raise RuntimeError(f"No logs found for pattern: {args.log_glob}")

            metrics = parse_log_metrics(log_file)
            run_metric = metric(p1_score, p2_score, metrics["avg_util_pct"], metrics["in_band_ratio_pct"])

            result = RunResult(
                index=idx,
                config=cfg,
                p1_score=p1_score,
                p2_score=p2_score,
                turns=int(metrics["turns"]),
                avg_util_pct=metrics["avg_util_pct"],
                min_util_pct=metrics["min_util_pct"],
                max_util_pct=metrics["max_util_pct"],
                in_band_ratio_pct=metrics["in_band_ratio_pct"],
                avg_elapsed_ms=metrics["avg_elapsed_ms"],
                phase_forward_ms_per_turn=metrics["phase_forward_ms_per_turn"],
                phase_backward_ms_per_turn=metrics["phase_backward_ms_per_turn"],
                phase_combat_ms_per_turn=metrics["phase_combat_ms_per_turn"],
                phase_planner_ms_per_turn=metrics["phase_planner_ms_per_turn"],
                phase_path_ms_per_turn=metrics["phase_path_ms_per_turn"],
                phase_forward_share_pct=metrics["phase_forward_share_pct"],
                phase_backward_share_pct=metrics["phase_backward_share_pct"],
                phase_combat_share_pct=metrics["phase_combat_share_pct"],
                phase_planner_share_pct=metrics["phase_planner_share_pct"],
                phase_path_share_pct=metrics["phase_path_share_pct"],
                log_file=str(log_file),
                score_metric=run_metric,
            )
            results.append(result)

            print(
                f"scores=({p1_score},{p2_score}) "
                f"util(avg/min/max)={result.avg_util_pct:.2f}/{result.min_util_pct:.2f}/{result.max_util_pct:.2f}% "
                f"in-band={result.in_band_ratio_pct:.1f}% metric={run_metric:.2f}"
            )

        results.sort(key=lambda r: r.score_metric, reverse=True)
        write_reports(results, out_csv, out_json)

        keep = max(1, min(args.keep_best, len(results)))
        print("=" * 72)
        print(f"Top {keep} configurations")
        for rank, r in enumerate(results[:keep], start=1):
            print(
                f"#{rank} idx={r.index} score=({r.p1_score},{r.p2_score}) metric={r.score_metric:.2f} "
                f"util={r.avg_util_pct:.2f}% band={r.in_band_ratio_pct:.1f}% cfg={r.config}"
            )
        print(f"Saved: {out_csv}")
        print(f"Saved: {out_json}")

    finally:
        write_text(cpp_path, original_source)
        try:
            compile_bot(bot_dir, args.source, args.output_bot)
        except Exception as exc:
            print(f"WARNING: failed to restore/recompile source after sweep: {exc}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
