# High-Level Application Plan: Snake-Byte Bot Agent

## Project Overview
The goal of this project is to build an artificial intelligence bot for the game "Snake-Byte." Snake-Byte is a grid-based, simultaneous-action, non-zero-sum competitive game involving gravity, persistent moving snake-like bots, and multiple agents (2-4 snakes) per player.
This plan is language-independent and designed for iterative delivery by an agent workflow (e.g., Qwen-Agent) and human programmers. The approach prioritizes high-speed predictive simulations over complex zero-sum search trees, as the game's branching factor is enormous, and resource gathering heavily dictates victory.

---

## Epics & Milestones

### Epic 1: Project Setup and Foundational Infrastructure
*Goal: Establish the basic build, execution, and testing environments.*
* **Epic 1.1: Environment Scaffolding**
  * Set up source control layout and language-appropriate skeletons for engine IO parsing (myId, dimensions, board state, snake commands).
  * Establish baseline bot implementation answering with valid `id WAIT` or `id UP` commands.
* **Epic 1.2: Scenario Testing Infrastructure**
  * Create a local game engine interface connecting your code with the provided `WinterChallenge2026-Exotec` engine.
  * Define deterministic seed-based test cases (`test-scenarios.txt`) covering basic 1v1 and 2v2 situations (e.g., collision avoidance, climbing platforms, choosing safe food over distant contested food).

### Epic 2: Core Game Logic Implementation (Internal Model)
*Goal: Recreate game rules perfectly to allow rapid local simulation of future game states.*
* **Epic 2.1: World Representation**
  * Implement an ultra-fast, 1D or 2D array representation of the game grid holding walls, power sources, and snake body parts.
* **Epic 2.2: The Physics & Rules Engine**
  * Implement gravity resolution correctly (snakes falling until landing on solids).
  * Implement collision and scoring logic (heads crashing into heads, simultaneous source gathering, head loss mechanics).
* **Epic 2.3: State Cloning and Rollbacks**
  * Ensure the gamestate object can be cloned computationally cheaply (< 0.1ms) to accommodate wide search forests.

### Epic 3: Foundational Search and Heuristics
*Goal: Give the bot basic spatial awareness and pathfinding without full adversarial logic.*
* **Epic 3.1: Pathfinding with Gravity (BFS / A*)**
  * Implement modified A* mapping routes across disconnected platforms considering falling constraints and climbing mechanics.
* **Epic 3.2: World Evaluation Heuristics (Flood Fill & Voronoi Zones)**
  * Implement safety checking using reachability and Flood Fill (how many cells are safe).
  * Implement **Voronoi / Reachability territory division** to classify power sources as: "Exclusive Mine", "Exclusive Theirs", and "Contested".
* **Epic 3.3: Priority Rule Processing**
  * Execute standard logic flow: Survive constraints > Exclusive grabs > Safe contested grabs > Block/Trap opponents.

### Epic 4: Advanced Search Trees & Adversarial AI
*Goal: Evolve the bot to perform lookahead depth simulations (2-5 plies max) to predict enemy movements and optimize multi-agent coordination.*
* **Epic 4.1: Short-Depth Game Tree Expansion**
  * Build an iterative deepening Minimax search (with Alpha-Beta pruning) exploring localized areas.
  * Prune independent snakes: evaluate snakes far away from enemies independently from the main game tree to bypass exponential branching.
* **Epic 4.2: Reachability Envelopes & Enemy Masking**
  * Instead of evaluating every combination of enemy moves, weight nearby cells by "path count danger" based on enemy range.
  * Mask out highly probable enemy cell occupations to safely guide search toward low-risk resources.
* **Epic 4.3: Decentralized Snake Roles Execution**
  * Assign dynamic roles (Collector, Support/Platform, Defender, Killer/Suffocator) sequentially per snake based on proximity and board layout scoring.

### Epic 5: Bot Tuning & Reinforcement / Agentic Loop
*Goal: Establish autonomous iterative workflows to tune parameters and refine edge cases.*
* **Epic 5.1: Tournament and Benchmarking Execution**
  * Run parallel testing of parameter permutations (depth vs. width, heuristic weights like `length_delta`, collision penalty).
  * Retain best performing heuristics on predetermined static seed matches.
* **Epic 5.2: LLM Agentic Analysis Loop (using Qwen-Agent infrastructure)**
  * Create tasks assigning AI Agents (Coder, Runner, Critic) to review benchmark logs and failed physics tests to implement algorithmic fixes.
  * Automatically evaluate modifications by running against test-suites to compare performance (A/B testing of model changes).

---

## Technical Constraints to Keep in Mind
- Response times must remain ≤ 50ms (or 73ms effective limits depending on the engine).
- Memory size is finite; keep serialized state spaces and array caches strictly budgeted.
- we need performant cpp code. python is ok for local development, the tested and shipped product is a single cpp file.
- **Performance:** Response times must remain ≤ 50ms (or 73ms effective limits depending on the engine). Using C++ ensures that the search depth can be significantly wider than a Python analog, but memory allocations (e.g. `new`/`malloc`) inside the main game loop must be strictly forbidden. Use pre-allocated buffers.
