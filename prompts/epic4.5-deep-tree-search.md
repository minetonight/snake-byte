# Epic 4.5: Deep Tree Search and Long-Horizon Targeting

**Goal:** Extend Epic 4 with interruptable deeper lookahead and stable long-term per-snake objectives, while preserving strict turn-time safety.
**Supporting Documents:** `epic-4-adversarial-ai.md`, `epic-4-extended.md`, High-Level Plans, Epic 1/2/3/4 results.

## Resources
The rules are in `snakebyte-rules.txt`. Project context is in `basic-ideas.txt`. Existing bot logic is in `bot-development/bots/epic4-solver-bot.cpp` (or the current active solver file). Use `WinterChallenge2026-Exotec` for simulation.

## Why this Epic Exists
Epic 4 delivered danger envelopes, localized alpha-beta, and dynamic roles. Remaining gaps are:
- deeper but safe tree search (iterative deepening still basic),
- delayed-death handling (poison apple style traps),
- explicit map-wide role targets per snake (non-shared long-horizon goals),
- stronger support/platform behavior in multi-snake coordination.

## Stories for Implementation

### Story 4.5.1: Interruptable Deep Iterative Search
**As a** tactical planner,
**I want** iterative deepening search that can be interrupted at any time,
**So that** I gain deeper foresight without risking timeout defeat.

* **Acceptance Criteria:**
  * Use iterative deepening with strict `std::chrono` budget checks on every loop/branch.
  * Search depth expands while time allows; best fully-evaluated action is always retained as fallback.
  * Deep search is still localized to relevant combat zones (do not expand to whole-map exponential trees).
  * Maintain hard safety margin under the engine turn limit (existing ~69ms guard acceptable if stable).

### Story 4.5.2: Delayed-Death Trap Avoidance
**As a** survival-focused strategist,
**I want** short-horizon future-state checks for seemingly good immediate moves,
**So that** I avoid self-kill sequences like poison-powerup collapses.

* **Acceptance Criteria:**
  * Add explicit delayed-risk scoring for moves that consume a power source but lead to near-term death/fall.
  * Regression target: `bot-development/test-maps/tactics/01-poison-apple.txt`.
  * Regression target: `bot-development/test-maps/tactics/01b-poison-apple-under-enemy.txt`.
  * Regression targets: `bot-development/test-maps/tactics/07*-corridor-*.txt`.
  * The bot must prefer survivable alternatives over immediate gain when delayed-death risk is high.

### Story 4.5.3: Map-Wide Per-Snake Role Targets
**As a** multi-agent coordinator,
**I want** each friendly snake to hold a persistent, non-conflicting strategic target,
**So that** team behavior remains coherent beyond single-turn heuristics.

* **Acceptance Criteria:**
  * Maintain a target power-cell (or safe support cell) per friendly snake across turns.
  * Targets must be unique per snake (no duplicate primary targets).
  * If a target disappears, reassign quickly and safely.
  * Edge case: if only one power source remains, assign exactly one collector and route others to support-safe nearby cells.
  * Actions must be separated by a semicolon ; and be one of the following.
  * Add visual and for the human information every turn:
    - as per the `snakebyte-rules.txt`

```text
Any of the movement actions can be followed by text which will be displayed above the appropriate snakebot for debugging purposes.

Special commands:

MARK x y: places a marker at the specified coordinates. Markers are visible in the viewer for debugging purposes.
```

    - for each snake command: if there is assigned target power source or target platform: 
     + add the target coordinates after the command, eg 1 UP 24 9;2 LEFT 13 8;
     + MARK the target coordinates after the command, eg 1 UP 24 9;MARK 24 9;2 LEFT 13 8;MARK 13 8;
    - the order of the marks and id commands are not important, eg 1 UP 24 9;2 LEFT 13 8;MARK 13 8;MARK 24 9;

  * Target feature validation maps: `bot-development/test-maps/complex-pathing/`.

### Story 4.5.4: Support/Platform Role Upgrade
**As a** support snake,
**I want** explicit ally-assist positioning behavior,
**So that** allies can reliably climb/route while reducing friendly collision risk.

* **Acceptance Criteria:**
  * Support role chooses assist positions that improve ally route feasibility.
  * Immediately before supported-snake powerup consume events, add immediate rotate-away safety behavior to reduce crash penalties.
  * Integrate support logic with persistent target system from Story 4.5.3.
  * Target feature validation maps: `bot-development/test-maps/coop`

### Story 4.5.5: Parameterized Scoring Block (Epic 5 Prep)
**As a** tuning workflow,
**I want** key evaluation weights grouped in one config block,
**So that** parameter sweeps and A/B testing are fast and reproducible.

* **Acceptance Criteria:**
  * Consolidate major heuristic constants into a single section/struct.
  * No behavior change required from refactor alone; this is structural prep for Epic 5 tuning.

## Test and Validation Strategy
- Use deterministic simulations throughRun with:
- `cd bot-development/simulation/`
- `python3 run_simulation.py "</abs/bot1>" "</abs/bot2>" --map "</abs/map-file>"`
- Validate both orientations where relevant (`our bot` as P1 and P2).
- Run mirror (`bot vs bot`) runs as a efficient checks for bot behaviour: we get 2x information and logs from one test. 

## Definition of Done for Epic 4.5
- Deep iterative search is safely interruptable and improves tactical outcomes in close combat.
- Poison/delayed-collapse trap handling is visibly improved on targeted maps.
- Per-snake persistent targets are active, unique, and robust to disappearing objectives.
- Support/platform behavior is explicit rather than purely incidental.
- Scoring constants are centralized for future sweep/tuning work.

bugs observer on live server:
) bug: snakes with even lenght dont chase their tail as safe square but go for walls.
) bug: a snake on 24, 5; apples on 22, 7 and 23, 3. platforms on 24, 8 and 23, 4; the goal of the snake is 23, 3 which is unreachable. 
) also snakes abandon their apple target and choose new , without the target dissapearing and still having clear line if sight, no danger. the long term target should be taking precendence if this case.
) 