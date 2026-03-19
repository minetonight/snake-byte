# Epic 4 Result Parameters (Timing + Deep Search)

## Purpose
This file defines the new per-turn timing parameters added to `epic4-solver-BFS-bot.cpp` so they can be tuned in Epic 5 sweeps.

## Turn Budget Model
Total engine budget per turn: **73ms**.

Runtime now splits turn time into:
1. **Five algorithm-phase percentages** (cumulative phase deadlines)
2. **One wrapup buffer percentage** (reserved end-of-turn safety margin)

`out_of_time()` now respects both:
- hard turn deadline
- current phase deadline

## Tunable Parameters
All parameters are in `BotTuning` (`bot-development/bots/epic4-solver-BFS-bot.cpp`).

### Global budget controls
- `TURN_TOTAL_MS_BUDGET = 73`
- `TURN_WRAPUP_BUFFER_PCT = 5`
- `TURN_MIN_UTILIZATION_PCT = 90`

### Five phase percentages
- `TURN_PHASE_FORWARD_BFS_PCT = 15`
- `TURN_PHASE_BACKWARD_BFS_PCT = 20`
- `TURN_PHASE_LOCAL_COMBAT_PCT = 20`
- `TURN_PHASE_POWER_PLANNER_PCT = 25`
- `TURN_PHASE_PATH_AND_SCORING_PCT = 15`

## Constraints for Sweeping
Use this invariant in sweep scripts:

`TURN_PHASE_FORWARD_BFS_PCT + TURN_PHASE_BACKWARD_BFS_PCT + TURN_PHASE_LOCAL_COMBAT_PCT + TURN_PHASE_POWER_PLANNER_PCT + TURN_PHASE_PATH_AND_SCORING_PCT + TURN_WRAPUP_BUFFER_PCT = 100`

## Current Default Derived Budgets (from 73ms)
Because integer ms are used at runtime, defaults currently normalize to:

| Phase | Budget (ms) |
|---|---:|
| forward_bfs | 12 |
| backward_bfs | 14 |
| local_combat | 14 |
| power_planner | 18 |
| path_scoring | 12 |
| wrapup buffer | 3 |
| **Total** | **73** |

## Deepening Methods (Budget-aware)
These methods now deepen while phase time remains:
- `choose_local_alpha_beta_action_budgeted(...)`
- `first_action_to_powerup_gain_budgeted(...)`

## Logging Fields for Benchmark Parsing
Per-turn log line now includes:
- `elapsed: <microseconds>`
- `util: <percent_of_73ms>`
- `phase_ms: forward_bfs=used/budget,backward_bfs=...,local_combat=...,power_planner=...,path_scoring=...`
- `wrapup_pct=<value>`

## Map 11 Validation Snapshot (latest run)
Run target:
- map: `bot-development/test-maps/complex-pathing/11-bigmap-E45Sx-long-term-target.txt`
- matchup: `epic4-solver-BFS-bot.exe` vs `epic4-solver-bot.exe`
- log: `WinterChallenge2026-Exotec/epic4_bfs_bot_log_1267258.txt`

Observed utilization (200 turns):
- average: **90.56%**
- min: **90.42%**
- max: **90.69%**

Consolidated turn timing:

| Turns | Avg time | Range | Utilization |
|---|---:|---:|---:|
| 1-200 | 66.11ms | 66.00-66.20ms | 90.42%-90.69% |

### Phase-time share from this run (instrumented sections)
| Phase | Avg ms/turn | Share of instrumented phase-time |
|---|---:|---:|
| forward_bfs | 0.230 | 40.90% |
| backward_bfs | 0.309 | 54.86% |
| local_combat | 0.000 | 0.00% |
| power_planner | 0.000 | 0.00% |
| path_scoring | 0.024 | 4.24% |

Note: On this specific map/matchup, local combat and deep power planner paths are rarely triggered, so most measured phase time is precompute BFS work.
