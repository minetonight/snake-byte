# Epic 5: Bot Tuning and Parameter Sweeping

**Goal:** Optimize algorithm parameters through extensive local headless simulation and automated tuning scripts without consuming AI generation quotas.
**Supporting Documents:** High-Level Plan v1 (Epic 5), High-Level Plan v2 (Phase 5), basic-ideas.txt.

## Resources
The rules of the game are in the snakebyte-rules.txt. The project overview is in the basic-ideas.txt.
Consider the other epics in the prompts folder as they give context what was completed and what will be done later.
Folder WinterChallenge2026-Exotec contains the game code that we are creating an AI bot for.

## Stories for Implementation

### Story 5.1: Monte Carlo / Deterministic Parameter Sweeping
**As an** automated tuning script,
**I want** to run thousands of local headless sub-games systematically adjusting heuristic weights,
**So that** I mathematically discover the optimization peaks for my C++ behavior configuration arrays.
* **Acceptance Criteria:**
  * Create a local Python script using `scipy.optimize` or sweeping parallel grid searches invoking the binary subprocess.
  * Variables exposed for strict tuning: `length_delta_weight`, `exclusive_resource_weight`, `collision_penalty`, `danger_map_threshold`, `FUTURE_length_influence`.
  * Ensure identical seeds are NOT reused across tuning vs comparison to avoid biased overfitting.
  * Output optimal weight results to a JSON or C++ header constants file that will compile directly into the final agent.

### Story 5.2: Intra/Inter-Algorithm Tournament Runner
**As a** benchmarking validation loop,
**I want** to periodically pit various strategic iterations against one another locally (e.g. evaluating "Safe Zones First" strategy vs. "Contested Sources First" strategy),
**So that** I can definitively trace performance regressions to specific code versions.
* **Acceptance Criteria:**
  * Use identical fixed map seeds exclusively for direct head-to-head algorithm comparisons.
  * Capture and graph objective win/loss metrics: tracked total points maxed, frequency of head collisions, overall survival length.
  * Implement a robust feedback hook capable of emitting performance diff logs allowing developers and external LLM agents to verify whether code commits actively helped or hindered.
