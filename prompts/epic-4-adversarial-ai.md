# Epic 4: Adversarial AI and Danger Avoidance

**Goal:** Avoid enemy traps and anticipate future movements efficiently by leaning on statistical envelopes rather than deep exponentially-growing search trees.
**Supporting Documents:** High-Level Plan v1 (Epic 4), High-Level Plan v2 (Phase 4), Basic Ideas.

## Resources
The rules of the game are in the snakebyte-rules.txt. The project overview is in the basic-ideas.txt.
Consider the other epics and epic-results in the prompts folder as they give context what was completed and what will be done later.
Folder WinterChallenge2026-Exotec contains the game code that we are creating an AI bot for.

## Stories for Implementation

### Story 4.1: Reachability Envelopes (The Danger Map)
**As a** fast defensive algorithm,
**I want** to mask out areas of the grid that enemies can easily claim,
**So that** my snakes do not stumble into predictable ambushes without forcing the engine to calculate perfect deep game trees.
* **Acceptance Criteria:**
  * TDD: create test cases in a new folder test-maps/enemies/ following the ideas of the exiting test cases. make test situations that must be completed by this implementation. tests first, and not edited, code later. when a test must be changed, ask the user for confirmation, stop your flow.  
  * Implement a 1d representation of a 2D array representation (`danger_map`) that projects potential enemy movement out 1-2 turns.
  * Mask out cells probabilistically based on "path count" (i.e. cells with 6 converging enemy paths are exceptionally more dangerous than cells with 1 path).
  * Exclude these high-risk tiles entirely from standard friendly A* pathfinding unless chasing an explicitly enforced critical objective.

### Story 4.2: Alpha-Beta Tying (Targeted Deep Search)
**As an** opportunistic close-combat hunter,
**I want** to trigger localized 2-3 ply Alpha-Beta logic only when enemies actively encroach into my immediate zone,
**So that** I spend cycle time safely calculating close-quarters encounters instead of distant irrelevant bots.
* **Acceptance Criteria:**
  * TDD: create test cases in a new folder test-maps/enemies/ following the ideas of the exiting test cases. make test situations that must be completed by this implementation. tests first, and not edited, code later. when a test must be changed, ask the user for confirmation, stop your flow.
  * Enable deep search only for friendly snakes currently overlapping with an enemy's Danger Map.
  * Enforce strict `std::chrono` timing logic to exit the search early (Iterative Deepening methodology) to permanently prevent 70ms turn timeouts (resulting in instant defeat).
  * Incorporate fitness evaluations covering length differences, lost tail segments (-1 penalty), and head-to-head collision bonuses.

### Story 4.3: Decentralized Roles and Behaviors
**As a** coordinated multi-snake logic router,
**I want** to assign dynamic roles to my friendly snakes based on their position relative to resources and enemy snakes,
**So that** they provide utility to the overall team score even if they are fundamentally blocked from safely routing to powerups.
* **Acceptance Criteria:**
  * TDD: create test cases in a new folder test-maps/enemies/ following the ideas of the exiting test cases. make test situations that must be completed by this implementation. tests first, and not edited, code later. when a test must be changed, ask the user for confirmation, stop your flow.
  * Implement distinct role profiles across the snake array:
    * `Collector`: Secure resources directly.
    * `Platform/Support`: Act as a stepping stone structure to elevate a friendly snake to a power source. at the moment of eating immediately rotate away to avoid collision penalties.
    * `Defender`: Broadly occupy chokepoints.
    * `Suffocator`: Wrap around to trap enemies.
    * `Killer`: Strictly hunt vulnerable 3 block snakes to remove them from the board.
  * Shift roles dynamically sequentially across turns based on proximity and board layout scoring.
  * tests in bot-development/test-maps/coop must pass

### Story 4.4: All Tests Green
**As a** developer,
**I want** all tests in bot-development/test-maps/coop and bot-development/test-maps/tactics to pass,
**So that** our bot is viable in the basic and complex pathing given
**Acceptance Criteria:** 
  * in all tests you fight boss.py, dummy bot that just WAITs.
  * our bot is usually P1. If the expected score of P2 is higher, then probably our bot must be P2 and P1 is Boss.py.
  * try mirror matches against yourself just to see if both bots survive. in mirror matches the basic root test expected scores are not mandatory, and might deviate. If we score more than expected, that is a good sign.