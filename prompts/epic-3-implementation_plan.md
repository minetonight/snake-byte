# Epic 3 Implementation Plan: Search & Heuristics

## Goal Description
Implement foundational spatial reasoning, route planning, and high-performance area evaluations for the C++ SnakeByte bot. The bot needs an advanced A* pathfinder that accounts for gravity, a Voronoi-based BFS to evaluate territory ownership (Exclusive vs Contested powerups), and a decentralized priority engine to decide snake actions within the strict 73ms turn time limit.

## User Review Required
None so far. This plan strictly follows the [epic-3-search-heuristics.md](file:///home/aleks/Development/Python/snake-byte/prompts/epic-3-search-heuristics.md) stories.

## Proposed Changes

### Core Logic (`bot-development/bots/epic3-solver-bot.cpp`)
We will create a new bot file `epic3-solver-bot.cpp` taking [epic2-solver-bot.cpp](file:///home/aleks/Development/Python/snake-byte/bot-development/bots/epic2-solver-bot.cpp) as a baseline.

#### [NEW] `bot-development/bots/epic3-solver-bot.cpp`
1. **Time Management Engine**:
   - Add a global timer initialized at the start of the turn.
   - Inject checks in A*/BFS to break out if elapsed time > 69ms (95% of 73ms).
2. **Voronoi BFS Evaluator**:
   - Implement `calculate_voronoi()`: runs simultaneous or multi-source BFS from all alive snake heads.
   - Tags every powerup as: `EXCLUSIVE_MINE`, `EXCLUSIVE_THEIRS`, or `CONTESTED`.
   - Computes `Length Delta` based on scores.
3. **Modified A* Pathfinder**:
   - Instead of 2D A*, states node expansions by evaluating movement (UP, DOWN, LEFT, RIGHT).
   - Accounts for gravity and stepping stones by directly reusing the internal C++ [GameState](file:///home/aleks/Development/Python/snake-byte/bot-development/bots/epic2-solver-bot.cpp#35-334) and its [simulate()](file:///home/aleks/Development/Python/snake-byte/bot-development/bots/epic2-solver-bot.cpp#328-333) method to compute resulting positions and check validity.
4. **Decentralized Priority Engine (Decision Logic)**:
   - For each of `my_snakes` independently (ignoring global combinations to avoid exponential bloat):
   - Evaluate possible moves directly adjacent, ordered by:
     1. Survivability (doesn't instantly die AND passes a basic flood fill algorithm to ensure it doesn't trap itself in the next `length/2` turns due to self-collision).
     2. Path to `Exclusive Mine` powerups.
     3. Path to `Contested` powerups if closer/viable.
     4. Block opponents.
   - Format final output (e.g. `ID ACTION;ID ACTION`).

## Verification Plan

### Automated Tests
- Run `run_simulation.py` with custom maps:
  - [bot-development/test-maps/pathing/01 check graviti short path-right side.txt](file:///home/aleks/Development/Python/snake-byte/bot-development/test-maps/pathing/01%20check%20graviti%20short%20path-right%20side.txt)
  - `bot-development/test-maps/pathing/01 check graviti short path-left side.txt`
- Assert that the snake successfully paths to the goal using gravity.
- Verify time elapsed logging confirms stops before the 73ms budget.
