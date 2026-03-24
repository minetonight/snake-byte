# Epic 8 Validation Ladder

This ladder is the Story 8.12 regression and benchmark scaffold for the Epic 8 adversarial beam bot.

## Groups

### Group A — Engine fidelity / baseline survival
Use existing maps from:
- `pathing/`
- `complex-pathing/`

Representative ladder maps:
- `pathing/00-survive-in-well.txt`
- `pathing/00b-survive-in-well-emerge.txt`
- `pathing/06-fall-prediction.txt`
- `complex-pathing/06-plan-fall.txt`

### Group B — Corridor and post-growth escape
Representative ladder maps:
- `pathing/07-corridor-lock.txt`
- `pathing/07b-corridor-lock-long.txt`
- `pathing/08-scary-safe-apple.txt`
- `tactics/09-choke-control.txt`
- `beam-search/01-greedy-apple-loses-chamber.txt`
- `beam-search/02-temporary-retreat-wins-later.txt`
- `beam-search/03-fake-best-pruned-slow-good.txt`
- `beam-search/04-root-quota-preserves-family.txt`

### Group C — Enemy foresight and punish windows
Representative ladder maps:
- `enemies/01-danger-envelope-avoid.txt`
- `enemies/02-local-alpha-beta.txt`
- `enemies/10b-deadly tunnel - foresee enemies.txt`
- `adversarial/01-equal-apples-one-punishable.txt`
- `adversarial/02-enemy-head-pressure-contested-apple.txt`
- `adversarial/03-distant-enemies-abstracted.txt`
- `adversarial/04-hotspot-local-branching.txt`

### Group D — Cooperative allocation
Representative ladder maps:
- `coop/01-coop.txt`
- `coop/13-lift-a-friend.txt`
- `multi-snake/10a-smallmap-2v2-E6Sx-many-targets.txt`
- `multi-snake/11a-midmap-3v3-E6Sx-many-targets.txt`
- `multi-snake-adversarial/01-friendly-snakes-split-basins.txt`
- `multi-snake-adversarial/02-anchor-center-harvest.txt`

### Group E — Adversarial multi-snake pressure
New Epic 8 coverage:
- `multi-snake-adversarial/03-3v3-cross-pressure.txt`
- `multi-snake-adversarial/04-4v4-frontier-compression.txt`
- `multi-snake-adversarial/01-friendly-snakes-split-basins.txt`
- `multi-snake-adversarial/02-anchor-center-harvest.txt`
- `adversarial/03-distant-enemies-abstracted.txt`
- `adversarial/04-hotspot-local-branching.txt`

## New scenario intent

### `beam-search/`
- greedy apple loses chamber
- temporary retreat wins later
- fake-best line should be pruned while slow-good survives
- root quota preserves a non-greedy family

### `adversarial/`
- two equal apples but one is enemy-punishable
- enemy head pressure near a contested apple
- distant enemies should stay abstract unless they become relevant
- hotspot enemies should trigger local branching

### `multi-snake-adversarial/`
- friendly snakes split across basins
- one snake anchors center while another harvests
- 3v3 cross-pressure
- 4v4 frontier compression

## How to run

Candidate mirror run:
- `python3 bot-development/simulation/run_epic8_validation_ladder.py /abs/path/to/candidate_bot`

Candidate vs baseline benchmark:
- `python3 bot-development/simulation/run_epic8_validation_ladder.py /abs/path/to/candidate_bot --baseline-bot /abs/path/to/epic7_bot`

Optional group filter:
- `python3 bot-development/simulation/run_epic8_validation_ladder.py /abs/path/to/candidate_bot --groups B,C,E`

## Notes

- These Story 8.12 maps are adversarial diagnostics first, score-assertion tests second.
- Older map expectations should not be weakened to make Epic 8 pass.
- For new maps without explicit expected scores, compare candidate behavior against Epic 7 baselines and inspect the Story 8.11 diagnostics in the bot logs.
