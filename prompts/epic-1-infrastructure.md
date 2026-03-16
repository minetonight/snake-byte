# Epic 1: Project Setup, Orchestration, and Foundational Infrastructure

**Goal:** Establish the basic build, execution, and testing environments that will validate the C++ game bot using an AI-driven deterministic loop. This must function seamlessly before any complex game logic is written. 

## Background
- **Environment:** Local Python orchestrator acting as the AI controller (Gemini-3.1 Pro API) alongside a C++ compiler. 
- **Target Deliverable:** A robust testing loop connecting the C++ `WinterChallenge2026-Exotec` engine with deterministic scenarios, plus the C++ IO parsing skeleton based on the game rules.

## Resources
The rules of the game are in the snakebyte-rules.txt. The project overview is in the basic-ideas.txt.
Consider the other epics in the prompts folder as they give context what will be done later.
Folder WinterChallenge2026-Exotec contains the game code that we are creating an AI bot for.

## Stories for Implementation

### Story 1.1: The Local AI Orchestrator Script (Python)
**As a** system orchestrator,
**I want** a Python script (`build_loop.py`) that bundles C++ code and test traces into prompts for the AI, 
**So that** I have an autonomous CI/CD loop that generates, compiles, and tests C++ game logic.
* **Acceptance Criteria:**
  * Script reads a target feature definition.
  * Script calls the AI API with the C++ codebase, `basic-ideas.txt` context, and `snakebyte-rules.txt`.
  * Script compiles the response using `g++`.
  * On `g++` compilation failure or `pytest` failure, the `stderr` is automatically fed back into the AI for correction.

### Story 1.2: Environment Scaffolding and IO Protocol (C++)
**As a** C++ bot,
**I want** to correctly parse the `SnakeByte` game Initialization Input and Turn Input,
**So that** I can output valid commands (`id WAIT`, `id UP`, `id DOWN`, `id LEFT`, `id RIGHT`) while respecting the 1000ms first-turn and 73ms per-turn rules.
* **Acceptance Criteria:**
  * Parse Initialization: `myId`, `width` (15-45), `height` (10-30), grid layout (inverted Y: row 0 is top, going UP means decreasing Y), and snakebot IDs.
  * Parse Turn Input: Remaining power sources, number of snakebots, and their 2D body part positions.
  * Establish a baseline bot answering with `WAIT` to prove connectivity.
  * Include timing guardrails using `std::chrono` to abort operations and print fallback commands if execution nears 70ms. (Note: executing beyond 73ms or providing invalid commands results in automatic defeat, yielding a 0 score per rules).

### Story 1.3: Scenario Testing Infrastructure (Python -> C++)
**As an** automation engineer,
**I want** a local game engine interface connecting a compiled C++ dummy binary (via subprocess) to the testing engine,
**So that** I can deterministically test 1v1 and 2v2 situations based on static text-based scenarios.
* **Acceptance Criteria:**
  * Define seed-based or initial-state test cases (`test-scenarios.txt`).
  * Ensure the C++ bot can receive mocked IO and output commands to the Python parent process reliably.
