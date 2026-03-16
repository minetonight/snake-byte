# Epic 3: Foundational Search and Heuristics

**Goal:** Provide the bot with basic spatial reasoning, route planning, and high-performance area evaluations using a decentralized approach.
**Supporting Documents:** High-Level Plan v1 (Epic 3), High-Level Plan v2 (Phase 3).

## Resources
The rules of the game are in the snakebyte-rules.txt. The project overview is in the basic-ideas.txt.
Consider the other epics in the prompts folder as they give context what was completed and what will be done later.
Folder WinterChallenge2026-Exotec contains the game code that we are creating an AI bot for.

### target hardware
   * GCC pragmas and attributes that influence code generation are allowed
   * 100000Kb source code limit
   * 768Mb ram
   * one core but with hyperthreading
   * 73ms effective limit per turn (heuristic test on codingame)


## Stories for Implementation

### Story 3.1: Pathfinding with Gravity (Modified A*)
**As a** navigation system,
**I want** an advanced A* pathfinder suited for SnakeByte physics,
**So that** individual snakes can chart courses across disconnected platforms factoring in gravity falls and ascending climbs.
* **Acceptance Criteria:**
  * Support falling constraints: Ensure paths check landing stability based on body layout.
  * Consider other snakes as potential stepping stone platforms, but evaluate the risk of their dynamic movement.
  * Pre-calculate routes prioritizing the furthest or safest resources based on overall strategy.
  * the test with map bot-development/test-maps/pathing/01 check gravity short path-right side.txt passes
  * the test with map bot-development/test-maps/pathing/01 check gravity short path-left side.txt passes
  * the way to start the maps is
    ```
    cd bot-development/simulation/
    python3 run_simulation.py "/home/aleks/Development/Python/snake-byte/bot-development/bots/epic3-solver-bot.exe" "python3 /home/aleks/Development/Python/snake-byte/bot-development/bots/Boss.py" --map "/home/aleks/Development/Python/snake-byte/bot-development/test-maps/pathing/01 check graviti short path-right side.txt"
    ```
  * runtime profiling of the execution tracks the time since the start of the turn and stops searching when we reach 95% of the allotted time of 73ms

### Story 3.2: Voronoi Territory Division & Heuristics
**As a** strategic evaluator,
**I want** to map the board into influence zones using Flood Fills / BFS reachability algorithms,
**So that** I know exactly which resources belong to my team securely and which must be tightly contested.
* **Acceptance Criteria:**
  * Run BFS reachability from all heads to determine distances to all remaining power sources.
  * Tag every power source as either: `Exclusive Mine` (safe to delay collecting), `Exclusive Theirs` (unobtainable), or `Contested` (rush priority).
  * Calculate overall sizes (`Length Delta`) for dynamic behavioral adjustments (play defensive/spread if winning, aggressive/hunting if losing).

### Story 3.3: Decentralized Priority Engine
**As a** multi-agent commander,
**I want** to execute logic per snake independently rather than through a single massive global tree search,
**So that** I do not hit performance timeouts traversing an unboud exponential branching factor of $3^{1600}$.
* **Acceptance Criteria:**
  * the program runs on one core but with hyperthreading.
  * Search execution must decouple independent snakes. A snake safely isolated running toward its powerups does not join the Minimax permutations of a snake actively fighting an opponent.
  * Snakes process rules locally in sequence: `Survive constraints > Exclusive grabs > Safe contested grabs > Block/Trap opponents`.
  * runtime profiling of the execution tracks the time since the start of the turn and stops searching when we reach 95% of the allotted time of 73ms