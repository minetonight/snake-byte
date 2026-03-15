# High-Level Application Plan v2: Snake-Byte Bot Agent

## Project Overview
The goal of this project is to build an artificial intelligence bot for the game "Snake-Byte." Snake-Byte is a grid-based, simultaneous-action, non-zero-sum competitive game involving gravity, persistent moving snake-like bots, and multiple agents (2-4 snakes) per player.

**Execution Strategy (v2):** This plan is optimized for an AI-driven CI/CD loop where a local deterministic script acts as the orchestrator, and Gemini-3.1 Pro acts as the sole intelligence engine. Rather than "Epics" designed for humans, this plan is structured as **Phase Prompts** and **API Contracts** designed to be fed into Gemini alongside a robust Test-Driven Development (TDD) runner. 

---

## Phase 1: The Deterministic Loop, TDD Foundation, and Compiler
*Goal: Build the local scripts and testing environment that will validate Gemini's code generation without manual human intervention.*

* **Prompt 1.1: The Local AI Orchestrator Script (Python)**
  * Write a Python script (`build_loop.py`) that reads a target feature, bundles the C++ codebase into a prompt, calls the Gemini-3.1 Pro API, saves the response, and handles the `g++` compilation.
  * If compilation fails or `pytest` fails, it feeds the `stderr` back to Gemini.
* **Prompt 1.2: Scenario Testing Infrastructure (Python -> C++)**
  * Create a local game engine interface in Python connecting the C++ binary (compiled from sourcecode, invoked as a subprocess) with the provided `WinterChallenge2026-Exotec` engine.
  * Define deterministic seed-based test cases (`test-scenarios.txt`) covering basic 1v1 and 2v2 situations (e.g., gravity cascades, delayed death traps).
  * *Note: This phase must be completed and passing (even with a dummy C++ bot) before any game logic is written.*

---

## Phase 2: Core Game Logic Implementation (Internal Model in C++)
*Goal: Recreate game rules perfectly to allow rapid local simulation of future game states within the 50ms strict time limit.*

* **Prompt 2.1: Data Structure Formulation (C++)**
  * **Input to Gemini:** "Design a 1D flat integer array representation of the Snake-Byte grid in C++ using `std::array` or raw arrays to optimize CPU cache. Define the structs for `Snake`, `PowerSource`, and `GameState`. Aggressively utilize inline functions and `constexpr` where possible."
  * **Requirement:** State cloning must execute in < 0.1ms.
* **Prompt 2.2: The Physics & Rules Engine (C++)**
  * **Input to Gemini:** "Implement `apply_gravity(GameState&)` and `resolve_collisions(GameState&)` in C++. Gravity snakes fall until landing on solids. Heads crashing into heads resolves simultaneously. Simultaneous source grabs cancel size gains."
  * **Validation:** Must pass the `test-scenarios.txt` gravity and collision edge cases.

---

## Phase 3: Foundational Search and Space Allocation (C++)
*Goal: Give the bot basic spatial awareness and pathfinding without full adversarial logic.*

* **Prompt 3.1: Pathfinding with Gravity (A* in C++)**
  * **Input to Gemini:** "Implement a high-performance, low-allocation modified A* algorithm in C++ for grid mapping across disconnected platforms, accounting for falling constraints and climbing mechanics."
* **Prompt 3.2: Voronoi Territory Division & Heuristics (C++)**
  * **Input to Gemini:** "Implement Voronoi Reachability zones in C++. Classify all power sources on the board as 'Exclusive Mine', 'Exclusive Theirs', or 'Contested' based on distance calculations utilizing SIMD or bitboards if applicable."
* **Prompt 3.3: Decentralized Decision Tree (C++)**
  * **Input to Gemini:** "Implement a decentralized rule processor for each snake: Survive constraints > Exclusive grabs > Safe contested grabs > Block/Trap opponents."

---

## Phase 4: Enemy Pruning and Avoidance (C++)
*Goal: Implement a shallow, high-speed method of avoiding enemy snakes without expanding the game tree exponentially.*

* **Prompt 4.1: Reachability Envelopes (The Danger Map in C++)**
  * **Input to Gemini:** "Implement a C++ function that masks out grid cells reachable by enemy snake heads within 1-2 turns. Weight nearby cells by 'path count danger' based on enemy range. Ensure arrays are pre-allocated."
  * **Validation:** Bot demonstrates avoiding high-probability danger zones in 1v1 close-quarters tests via the Python test integration.
* **Prompt 4.2: Alpha-Beta Tying (Optional Depth in C++)**
  * **Input to Gemini:** "If time remains in the 50ms window (checked via `std::chrono` inside the C++ game loop) after computing Voronoi zones, run a shallow (2-ply) Alpha-Beta search *only* for snakes currently overlapping with enemy Reachability Envelopes."

---

## Phase 5: Local Deterministic Parameter Sweeping
*Goal: Establish autonomous iterative workflows to tune parameters. This phase requires ZERO AI API calls.*

* **Script 5.1: Monte Carlo / SciPy Parameter Tuning**
  * Write a local Python script (e.g., using `scipy.optimize` or grid search) to run thousands of headless sub-games locally.
  * **Variables to Sweep:** `length_delta_weight`, `exclusive_resource_weight`, `collision_penalty`, `danger_map_threshold`.
  * **Output:** A JSON constants file containing the mathematical optimal weights to be compiled into the final agent submission.

---

## Phase 6: Code Assembly and Deployment (C++)
*Goal: Package the final C++ code into the single-file format required by CodinGame/SnakeByte.*

* **Prompt 6.1: Code Amalgamation Script (Python)**
  * Write a Python script to scan the individual C++ classes (`physics.cpp`, `search.cpp`, etc.) and concatenate them into a single `bot.cpp` file. Ensure that `#include` guards are respected and that standard libraries are merged gracefully at the top. This file is the final deployable asset.

---

## Technical Constraints & CI/CD Reminders
- **Context Window Utilization:** Ensure `build_loop.py` bundles the `rules.txt`, the current `Snacion-bot/tests/test1-solver-bot.cpp`, and the specific failing test trace (whether it's `g++` compilation errors, Memory Leaks, or `pytest` assertion failures) into *every* prompt to Gemini.
- **Performance:** Response times must remain ≤ 50ms (or 73ms effective limits depending on the engine). Using C++ ensures that the search depth can be significantly wider than a Python analog, but memory allocations (e.g. `new`/`malloc`) inside the main game loop must be strictly forbidden. Use pre-allocated buffers.
