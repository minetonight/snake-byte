# Epic 5: Bot Tuning and Parameter Sweeping

**Goal:** Optimize algorithm parameters through extensive local headless simulation and automated tuning scripts without consuming AI generation quotas.
**Supporting Documents:** High-Level Plan v1 (Epic 5), High-Level Plan v2 (Phase 5), basic-ideas.txt.

## Resources
The rules of the game are in the snakebyte-rules.txt. The project overview is in the basic-ideas.txt.
Consider the other epics in the prompts folder as they give context what was completed and what will be done later.
Folder WinterChallenge2026-Exotec contains the game code that we are creating an AI bot for.

## Stories for Implementation

### Story 5.1: Monte Carlo / Deterministic Parameter Sweeping
**As an** automated tuning script,
**I want** to run thousands of local headless sub-games systematically adjusting heuristic weights,
**So that** I mathematically discover the optimization peaks for my C++ behavior configuration arrays.
* **Acceptance Criteria:**
  * Create a local Python script using `scipy.optimize` or sweeping parallel grid searches invoking the binary subprocess.
  * Variables exposed for strict tuning: `length_delta_weight`, `exclusive_resource_weight`, `collision_penalty`, `danger_map_threshold`, `FUTURE_length_influence`.
  * Ensure identical seeds are NOT reused across tuning vs comparison to avoid biased overfitting.
  * Output optimal weight results to a JSON or C++ header constants file that will compile directly into the final agent.

### Story 5.2: Intra/Inter-Algorithm Tournament Runner
**As a** benchmarking validation loop,
**I want** to periodically pit various strategic iterations against one another locally (e.g. evaluating "Safe Zones First" strategy vs. "Contested Sources First" strategy),
**So that** I can definitively trace performance regressions to specific code versions.
* **Acceptance Criteria:**
  * Use identical fixed map seeds exclusively for direct head-to-head algorithm comparisons.
  * Capture and graph objective win/loss metrics: tracked total points maxed, frequency of head collisions, overall survival length.
  * Implement a robust feedback hook capable of emitting performance diff logs allowing developers and external LLM agents to verify whether code commits actively helped or hindered.

### Story 5.3: Time-Budget Split Sweeping (Epic4 BFS)
**As a** timing-tuning pipeline,
**I want** to sweep per-algorithm turn-budget percentages,
**So that** the bot uses almost all allowed per-turn compute while preserving result quality.
* **Scope:** tune these six runtime parameters (documented in `prompts/epic4-result-parameters.md`):
  * `TURN_PHASE_FORWARD_BFS_PCT`
  * `TURN_PHASE_BACKWARD_BFS_PCT`
  * `TURN_PHASE_LOCAL_COMBAT_PCT`
  * `TURN_PHASE_POWER_PLANNER_PCT`
  * `TURN_PHASE_PATH_AND_SCORING_PCT`
  * `TURN_WRAPUP_BUFFER_PCT`
* **Acceptance Criteria:**
  * Parameter sweeps enforce `sum(5 phase pcts) + TURN_WRAPUP_BUFFER_PCT = 100`.
  * On map `complex-pathing/11-bigmap-E45Sx-long-term-target.txt`, bot logs show per-turn utilization in the `90%-100%` band of total `73ms`.
  * Benchmark report includes:
    * consolidated turn-timing ranges (merge consecutive similar turns)
    * per-method phase-time percentages (`forward_bfs`, `backward_bfs`, `local_combat`, `power_planner`, `path_scoring`)
  * Tuning runner must compare both score quality and utilization quality (high utilization alone is insufficient).

## Timing Sweep Notes
- Runtime budget is phase-scoped through `out_of_time()` with cumulative phase deadlines.
- Deep-search methods are budget-aware:
  - local combat: `choose_local_alpha_beta_action_budgeted(...)`
  - power planner: `first_action_to_powerup_gain_budgeted(...)`
- Turn logs emit `util: X%` and `phase_ms: phase=used/budget` for direct ingestion by sweep scripts.

## Sweep Script (Implemented)
- Script path: `bot-development/simulation/sweep_timing_budgets.py`
- Purpose: patch timing constants in `epic4-solver-BFS-bot.cpp`, compile, run map 11, parse score/utilization/phase-time, rank configurations, and export CSV/JSON.

### Inputs
- Random sweep mode:
  - `--trials`, `--seed`, `--no-default`
- Fixed configuration mode:
  - `--configs-file <json>`

### Outputs
- CSV report (default): `bot-development/simulation/timing_sweep_results.csv`
- JSON report (default): `bot-development/simulation/timing_sweep_results.json`

### Standard Commands
- Random sweep:
  - `cd bot-development/simulation && python3 sweep_timing_budgets.py --trials 20 --keep-best 10`
- Fixed 5-profile 90/10 sweep:
  - `cd bot-development/simulation && python3 sweep_timing_budgets.py --configs-file timing_budget_presets_90.json --keep-best 5 --out-csv timing_sweep_presets90.csv --out-json timing_sweep_presets90.json`

### Fixed 90/10 Presets
- Preset file: `bot-development/simulation/timing_budget_presets_90.json`
- Rule enforced per preset:
  - algorithmic phases sum to 90
  - `TURN_WRAPUP_BUFFER_PCT = 10`
