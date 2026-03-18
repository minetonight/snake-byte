# Epic 4 BFS Takeaways

## Goal and Outcome
- We introduced a BFS-oriented pathing upgrade into `epic4-solver-BFS-bot.cpp` to reduce false local traps and improve long-path objective pursuit.
- The critical success case is `complex-pathing/05 check gravity long path.txt` against `epic4-solver-bot.cpp`, where the bot now reaches `P1=4, P2=3` consistently.

## What Changed (Core Behavior)

### 1) Targeting uses real path distance, not only Manhattan distance
- Persistent target assignment now uses precomputed forward BFS distances from each snake head.
- This removes the common error where a power source looks close in grid terms but is practically far because of platforms/walls.

### 2) Backward BFS map for nearest power distance
- Added backward multi-source BFS from all power sources.
- Used as a fast heuristic for “distance-to-nearest-power” during move scoring.

### 3) Heuristic BFS uses soft body occupancy
- For planning heuristics, snake body cells are treated as soft/passable (walls remain hard obstacles).
- Immediate safety is still enforced by simulation checks.
- This avoids false `unreachable` states from assuming all current bodies are static walls.

### 4) Long-path collect mode (anti-fear mode)
- In single-power scenarios with enemy far enough, bot enters long-path collect mode.
- It prioritizes physics-aware planner action (`first_action_to_powerup_gain`) first.
- If planner has no move, it falls back to BFS-gradient style guidance.
- This reduces overreaction to non-immediate risks.

## Root Causes Found During Debug
- Over-conservative safety gates (danger/floodfill/followup filters) blocked valid long-path progress.
- Heuristic map previously treated moving snake bodies as hard blockers, creating false “ghost obstacles”.
- This produced oscillation loops near chokepoints and prevented committing to profitable routes.

## Current Known Limits
- Improvement is most visible on long-path trap layouts.
- On many symmetric or open maps, outcomes may remain equal versus previous version because Manhattan already matched practical distance.
- We have not yet implemented a full multi-turn dynamic opponent occupancy forecast; current approach is heuristic + immediate simulation checks.

## Testing Guidance for Teammates
- Primary validation map:
  - `bot-development/test-maps/complex-pathing/05 check gravity long path.txt`
- Suggested command pattern:
  - BFS bot as P1, epic4 bot as P2.

## Engineering Notes
- Keep planner-first commit mode for single-power long-path cases; this is the key fix for route commitment.
- Keep heuristic BFS soft on snake bodies; hard-body heuristics reintroduce false fear loops.
- Keep safety checks in simulation path (do not remove immediate-death checks).

## Shipping Hygiene
- Debug-heavy per-snake logs were removed for shippable runtime.
- Remaining logs are lightweight turn-level timing/output lines only.
