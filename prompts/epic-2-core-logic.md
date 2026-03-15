# Epic 2: Core Game Logic Implementation (Internal Model)

**Goal:** Recreate game physics and rules perfectly in C++ to allow rapid local simulation of future game states within a strict <73ms window.
**Supporting Documents:** High-Level Plan v1 (Epic 2), High-Level Plan v2 (Phase 2), basic-ideas.txt.

## Resources
The rules of the game are in the snakebyte-rules.txt. The project overview is in the basic-ideas.txt.
Consider the other epics in the prompts folder as they give context what was completed and what will be done later.
Folder WinterChallenge2026-Exotec contains the game code that we are creating an AI bot for.

## Stories for Implementation

### Story 2.1: Data Structure Formulation
**As a** performance optimizer,
**I want** to construct an ultra-fast, memory-efficient internal grid representation in C++,
**So that** state cloning can execute in < 0.1ms for broad search trees without any main-loop heap allocations (`new`/`malloc`).
* **Acceptance Criteria:**
  * Utilize a flat 1D array or `std::array` instead of nested arrays to maximize CPU cache affinity.
  * Model World expansion logic: The logical field must expand beyond `0` and `width` to calculate snake falls accurately. Total model spatial width should represent `world_width + total_powerups_count`.
  * Treat `-1` as walls, `3` as powerups in array indexing.
  * Represent the snake's body using a lightweight memory structure (like pre-allocated deques or static arrays) separated by team/enemy.

### Story 2.2: The Physics & Rules Engine
**As a** physics simulator,
**I want** to apply gravity and collision rules exactly as governed by the official `SnakeByte` engine, defined in the folder WinterChallenge2026-Exotec/src/main/java/com/codingame/game/Game.java
**So that** the bot correctly predicts simultaneous actions and positional outcomes.
* **Acceptance Criteria:**
  * Evaluate movement: Head progresses, back segments follow. Unreachable cells below/outside grid boundaries are mathematically permitted as long as a segment rests on something solid (cascading falls logic).
  * Collisions: Resolve simultaneous head hits. If a head enters an occupied cell (platform or body), the head dies, and the *next* segment becomes the new head. If remaining body length is < 3, the entire snake is completely destroyed.
  * Multi-Consume Penalty: If multiple bot heads occupy the same powerup simultaneously, each receives `+1` length but suffers `-1` consequence for head clashing, naturally canceling size gains. Referee tie-breaker penalties strongly log lost heads.

### Story 2.3: State Cloning and Rollbacks
**As a** strategic tree search framework,
**I want** to branch and reset `GameState` computationally cheaply,
**So that** I can analyze tens of thousands of potential multi-agent permutations within ~70ms.
* **Acceptance Criteria:**
  * Ensure the `GameState` struct/class exposes a high-speed copy mechanism.
  * Aggressively utilize inline functions and `constexpr` variables to maximize compiler optimization limits.
