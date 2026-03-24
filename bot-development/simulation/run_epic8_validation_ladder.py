from __future__ import annotations

import argparse
from pathlib import Path
from typing import Dict, List

from run_simulation import run_simulation

REPO_ROOT = Path(__file__).resolve().parents[2]
TEST_MAPS = REPO_ROOT / "bot-development" / "test-maps"

GROUPS: Dict[str, Dict[str, object]] = {
    "A": {
        "label": "Engine fidelity / baseline survival",
        "maps": [
            "pathing/00-survive-in-well.txt",
            "pathing/00b-survive-in-well-emerge.txt",
            "pathing/06-fall-prediction.txt",
            "complex-pathing/06-plan-fall.txt",
        ],
    },
    "B": {
        "label": "Corridor and post-growth escape",
        "maps": [
            "pathing/07-corridor-lock.txt",
            "pathing/07b-corridor-lock-long.txt",
            "pathing/08-scary-safe-apple.txt",
            "tactics/09-choke-control.txt",
            "beam-search/01-greedy-apple-loses-chamber.txt",
            "beam-search/02-temporary-retreat-wins-later.txt",
            "beam-search/03-fake-best-pruned-slow-good.txt",
            "beam-search/04-root-quota-preserves-family.txt",
        ],
    },
    "C": {
        "label": "Enemy foresight and punish windows",
        "maps": [
            "enemies/01-danger-envelope-avoid.txt",
            "enemies/02-local-alpha-beta.txt",
            "enemies/10b-deadly tunnel - foresee enemies.txt",
            "adversarial/01-equal-apples-one-punishable.txt",
            "adversarial/02-enemy-head-pressure-contested-apple.txt",
            "adversarial/03-distant-enemies-abstracted.txt",
            "adversarial/04-hotspot-local-branching.txt",
        ],
    },
    "D": {
        "label": "Cooperative allocation",
        "maps": [
            "coop/01-coop.txt",
            "coop/13-lift-a-friend.txt",
            "multi-snake/10a-smallmap-2v2-E6Sx-many-targets.txt",
            "multi-snake/11a-midmap-3v3-E6Sx-many-targets.txt",
            "multi-snake-adversarial/01-friendly-snakes-split-basins.txt",
            "multi-snake-adversarial/02-anchor-center-harvest.txt",
        ],
    },
    "E": {
        "label": "Adversarial multi-snake pressure",
        "maps": [
            "multi-snake-adversarial/01-friendly-snakes-split-basins.txt",
            "multi-snake-adversarial/02-anchor-center-harvest.txt",
            "multi-snake-adversarial/03-3v3-cross-pressure.txt",
            "multi-snake-adversarial/04-4v4-frontier-compression.txt",
            "adversarial/03-distant-enemies-abstracted.txt",
            "adversarial/04-hotspot-local-branching.txt",
        ],
    },
}


def resolve_maps(group_ids: List[str]) -> Dict[str, List[Path]]:
    resolved: Dict[str, List[Path]] = {}
    for group_id in group_ids:
        entry = GROUPS[group_id]
        resolved[group_id] = [TEST_MAPS / rel for rel in entry["maps"]]  # type: ignore[index]
    return resolved


def summarize_scores(p1: int, p2: int) -> str:
    if p1 > p2:
        return f"P1+{p1 - p2}"
    if p2 > p1:
        return f"P2+{p2 - p1}"
    return "TIE"


def run_suite(title: str, bot1: str, bot2: str, group_ids: List[str]) -> None:
    print(f"\n=== {title} ===")
    resolved = resolve_maps(group_ids)
    for group_id in group_ids:
        label = GROUPS[group_id]["label"]  # type: ignore[index]
        print(f"\n[{group_id}] {label}")
        for map_path in resolved[group_id]:
            p1, p2 = run_simulation(bot1, bot2, map_file=str(map_path))
            rel = map_path.relative_to(REPO_ROOT)
            print(f"SUMMARY | {rel} | {p1}:{p2} | {summarize_scores(p1, p2)}")


def parse_groups(raw: str) -> List[str]:
    groups = [part.strip().upper() for part in raw.split(",") if part.strip()]
    invalid = [group for group in groups if group not in GROUPS]
    if invalid:
        raise argparse.ArgumentTypeError(f"Unknown groups: {', '.join(invalid)}")
    return groups


def main() -> None:
    parser = argparse.ArgumentParser(description="Run the Epic 8 validation ladder.")
    parser.add_argument("candidate_bot", help="Absolute path or command for the candidate bot")
    parser.add_argument("--baseline-bot", help="Optional Epic 7 baseline bot for comparison")
    parser.add_argument("--groups", default="A,B,C,D,E", type=parse_groups,
                        help="Comma-separated group ids to run, e.g. A,B,E")
    args = parser.parse_args()

    group_ids: List[str] = args.groups
    run_suite("Candidate mirror", args.candidate_bot, args.candidate_bot, group_ids)

    if args.baseline_bot:
        run_suite("Candidate vs baseline", args.candidate_bot, args.baseline_bot, group_ids)


if __name__ == "__main__":
    main()
