# Epic 4 Progress Handoff (Adversarial AI)

## Scope recap
Epic 4 goal is to improve enemy-aware behavior without expensive deep global trees:
- danger envelopes (1-2 turn enemy reachability)
- localized 2-3 ply alpha-beta only in close danger zones
- dynamic role assignment for multiple friendly snakes
- practical test health in `test-maps/enemies`, `test-maps/coop`, `test-maps/tactics`

## What is implemented in code
Primary file:
- `bot-development/bots/epic4-solver-bot.cpp`

Key additions done so far:
1. **Danger map (Story 4.1)**
   - 1D `danger_map` built from enemy heads with depth-limited BFS (projection depth 2).
   - Cells receive path-count-like risk accumulation (`uint16_t` weights).
   - High-risk cells are penalized heavily unless move is a critical objective (powerup while losing length race).

2. **Localized alpha-beta (Story 4.2)**
   - Triggered only when friendly snake is in enemy danger envelope and enemy is nearby.
   - Uses strict `out_of_time()` checks (turn budget guard remains based on existing ~69ms cutoff).
   - Searches local duel with nearest enemy for 2-3 ply depending on snake count.
   - Evaluation includes:
     - length delta term,
     - tail/lost-segment proxy penalty,
     - head-to-head distance/bonus term.

3. **Dynamic roles (Story 4.3)**
   - Roles assigned per turn from proximity heuristics:
     - `Collector`, `Support`, `Defender`, `Suffocator`, `Killer`
   - Role-specific score modifiers influence local action scoring.
   - Legacy tactical path-to-powerup logic is now gated by danger checks.

## Current decision pipeline (high level)
For each turn:
1. Parse state, build static/dynamic grid.
2. Compute Voronoi summary (`length_delta`, exclusive/contested).
3. Build enemy danger map.
4. Assign per-snake roles.
5. For each friendly snake:
   - if in immediate danger zone, try localized alpha-beta action first,
   - else evaluate candidate moves with weighted scoring:
     - survivability flood-fill,
     - danger penalties,
     - resource and Voronoi pressure,
     - role-specific tactical pressure,
     - one-turn simulated follow-up safety.
6. Emit `id ACTION;id ACTION` output.

## Test assets created/updated for Epic 4
Folder:
- `bot-development/test-maps/enemies/`

Maps:
- `01-danger-envelope-avoid.txt`
- `02-local-alpha-beta.txt`
- `03-dynamic-roles-coop.txt`

Current expected scores in this folder are set to **non-`-1`** outcomes and validated in **bot-vs-bot mirror mode**.

## Validation notes and caveats
1. `Boss.py` prints plain `WAIT` and is not robust for multi-snake command formatting in some scenarios.
   - On some multi-snake maps this can produce `-1` on Boss side.
   - Therefore Epic 4 enemy validations were stabilized using bot-vs-bot mirror checks.

2. `run_simulation.py` has been simplified back to base usage.
   - Removed extra flags (`--auto-side`, `--tested-only`) to keep workflow minimal.

3. Some `coop`/`tactics` maps remain sensitive to baseline expectation philosophy:
   - strict P1/P2 expected pair vs tested-bot-only acceptance.

## What remains to complete Epic 4 strictly
Against the literal acceptance criteria in `prompts/epic-4-adversarial-ai.md`:
1. **Story 4.1**: implemented, but danger thresholding can still be tuned for cleaner avoidance on edge cases.
2. **Story 4.2**: implemented localized alpha-beta + timing guards; iterative deepening is still basic and can be improved.
3. **Story 4.3**: role system exists, but support/platform behavior is still heuristic, not explicit structural placement planning.
4. **Story 4.4**: full `coop` + `tactics` green versus Boss with strict original dual-score expectations is not fully guaranteed yet due Boss-side limitations on some maps.

## Next steps
1. Improve `Support` role with explicit short-horizon platform intent (ally lift opportunities).
2. Add lightweight iterative-deepening loop around local alpha-beta (time-sliced depth growth).
2.1. improve long term planning, deep iterative interuptable path search. as per the test case bot-development/test-maps/tactics/01-poison-apple.txt where eating the powersource results in death. 
3. IMPORTANT implement map wide per snake role target power cell so that the snakes always have long term goal, that can be delayed by local circumstances like survive, assist, kill suffocate.
 - that target might dissapear on later turn, so check it. 
 - also make sure the target is not shared by two of our snakes
 - in the edge case of last powerup on the map, assign one snake with the power source gather role and the rest with a `suport` role and nearby safe target location.
 - target test cases for this feature are maps in bot-development/test-maps/complex-pathing
4. Separate score constants into a config block for easier tuning (Epic 5 preparation).
