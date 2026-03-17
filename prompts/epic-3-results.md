# Epic 3 Results: Search, Heuristics, and Debugging Handoff

This document is the implementation handoff for Epic 3.
It explains what was built, what the next developer can reuse directly, and how to debug/troubleshoot with the same workflow used during implementation.

---

## 1) What Epic 3 delivered

Epic 3 produced a practical decentralized search/heuristic bot in:
- `bot-development/bots/epic3-solver-bot.cpp`

Core outcomes:
- Gravity-aware turn simulation integrated in decision scoring.
- Voronoi-style reachability heuristic for resource pressure (`mine / theirs / contested` proxy).
- Flood-fill survivability checks to avoid immediate dead zones.
- Time-budgeted search (`~69ms` soft stop under a 73ms turn cap).
- Tactical route guidance for single-powerup situations and open-edge maps.
- Edge/fall-awareness for maps without side walls or without full floor.

---

## 2) Current algorithm (how it behaves now)

The current move policy is layered and decentralized per snake:

1. **Fast tactical pre-rules** (single-powerup special cases)
   - Directly above apple: prefer `UP`.
   - Diagonal-above apple: prefer side-step toward apple.
   - Additional constrained left-edge/open-floor handling for known edge-fall map patterns.

2. **Short tactical route search**
   - `first_action_to_powerup_gain(...)` chooses first move of shortest local route to gain length.
   - Uses `min_steps_to_powerup_gain(...)` recursion with depth tuned by map openness.
   - Opponent is removed in this tactical preview (`local_plan_state.opp_snakes.clear()`) to get a local route signal.

3. **Heuristic scoring fallback** (if no tactical override)
   - Survivability gating via `survives_flood_fill(...)`.
   - Immediate powerup value with enclosed-map guardrails.
   - Voronoi-derived length/resource pressure (`calculate_voronoi()`).
   - Center/space/edge penalties.
   - One-turn simulation check with anti-fall penalties (open side or floor maps).

4. **Time budget guardrails**
   - `out_of_time()` checks are spread through BFS/recursion/loop scoring.
   - On budget exhaustion, fallback action is inferred from previous heading.

---

## 3) Artifacts the next developer can reuse

Reusable helpers in `epic3-solver-bot.cpp`:
- `GameState::simulate(...)`
- `GameState::calculate_voronoi()`
- `GameState::survives_flood_fill(...)`
- `GameState::count_safe_followups(...)`
- `min_steps_to_powerup_gain(...)`
- `first_action_to_powerup_gain(...)`
- map openness flags initialized from static walls:
  - `map_has_open_left_edge`
  - `map_has_open_right_edge`
  - `map_has_open_floor_edge`

These are the extension points for Epic 4+ (adversarial logic / better tactical planning).

---

## 4) Key validated results

### Pathing suite status
Local pathing suite was run from `bot-development/simulation/run_simulation.py` in manual mode.
All maps in `bot-development/test-maps/pathing` are currently green under current expectations.

### Angle-map investigation result
Map:
- `bot-development/test-maps/test-snake-map_up-angled-snake.txt`

Observed failure (`epic3` mirror):
- `(P1, P2) = (0, 4)`

Root cause found during trace:
- P1 dies by **wall collisions** (`isInWall=true`), not by timeout/disqualification.
- This map remains useful as a tactical regression target for wall-recollision avoidance.

---

## 5) Debugging and tracing playbook (copy/paste)

## A) Reproduce a single map quickly
```bash
cd bot-development/simulation/
python3 run_simulation.py \
  "/home/aleks/Development/Python/snake-byte/bot-development/bots/epic3-solver-bot.exe" \
  "/home/aleks/Development/Python/snake-byte/bot-development/bots/epic3-solver-bot.exe" \
  --map "/home/aleks/Development/Python/snake-byte/bot-development/test-maps/test-snake-map_up-angled-snake.txt"
```

## B) Run full pathing batch
```bash
cd bot-development/simulation/
python3 run_simulation.py
```
(Manual mode runs the pathing folder batch.
Check the state of manual mode: bots to start, map folders to test, edit as needed)

## C) Headless engine trace with Maven
Use this when Python summary is insufficient.
```bash
cd WinterChallenge2026-Exotec/
mvn compile exec:java \
  -Dexec.mainClass=HeadlessMain \
  -Dexec.classpathScope=test \
  '-Dexec.args=/abs/path/to/bot1|||/abs/path/to/bot2|||' \
  -DcustomMapFile=/abs/path/to/map.txt \
  -q
```

## D) Capture full run logs to file
```bash
cd WinterChallenge2026-Exotec/
mvn compile exec:java \
  -Dexec.mainClass=HeadlessMain \
  -Dexec.classpathScope=test \
  '-Dexec.args=/abs/path/to/bot1|||/abs/path/to/bot2|||' \
  -DcustomMapFile=/abs/path/to/map.txt \
  -q > ../bot-development/simulation/debug-run.txt 2>&1
```

Then inspect:
```bash
tail -n 120 ../bot-development/simulation/debug-run.txt
grep 'DEBUG_' ../bot-development/simulation/debug-run.txt | head -n 80
```

## E) Temporary instrumentation pattern (when needed)
When root cause is unclear, add temporary prints in engine:
- `WinterChallenge2026-Exotec/src/main/java/com/codingame/game/Referee.java`
  - log `player.getOutputs().get(0)` per turn
- `WinterChallenge2026-Exotec/src/main/java/com/codingame/game/Game.java`
  - log beheading cause flags (`isInWall`, `isInBird`)
- those logging features are now saved in the engine code.
**Rule used during Epic 3:**
- Add instrumentation narrowly.
- Reproduce.
- Extract decisive lines.
- Remove instrumentation immediately after diagnosis.

---

## 6) Known caveats / technical debt

1. The tactical section now contains several targeted rules for edge/no-floor maps.
   - Works for current suite, but should be refactored into cleaner strategy modules later.

2. Local tactical route search ignores opponent future movement (by design) for route discovery.
   - Good for stability and speed, but can misestimate contested tactical races.

3. `test-snake-map_up-angled-snake.txt` still exposes wall-recollision behavior in mirror mode.
   - Candidate next improvement: add a "recent wall-collision risk memory" term or explicit wall-trap detector.

---

## 7) Suggested next steps for Epic 4+

- Replace tactical hard rules with a compact gravity-aware planner that includes opponent occupancy risk.
- Add map-mirroring generator script for pathing tests (reduce hand-authored map drift).
- Introduce structured debug toggles (`#define DEBUG_TRACE`) in bot to avoid ad-hoc logging edits.
- Add regression script that runs:
  - pathing batch,
  - selected simple maps (including angled),
  - seeded mirror sanity checks.

---

## 8) Quick checklist for the next developer

- Build bot:
```bash
g++ -O2 -std=c++17 bot-development/bots/epic3-solver-bot.cpp -o bot-development/bots/epic3-solver-bot.exe
```
- Run full pathing batch:
```bash
cat run_simulation.py # check the state of manual mode: bots, map folders to test
cd bot-development/simulation && python3 run_simulation.py
```
- If one map fails:
  1. Reproduce single map.
  2. Capture headless engine output to file.
  3. Add minimal temporary instrumentation.
  4. Remove instrumentation after finding cause.
