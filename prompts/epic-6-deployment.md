# Epic 6: Code Assembly and Final Deployment

**Goal:** Package the complex object-oriented C++ physics loop, heuristic searches, and engine integration layers into the single source file expected by the SnakeByte CodinGame engine.
**Supporting Documents:** High-Level Plan v2 (Phase 6).

## Resources
The rules of the game are in the snakebyte-rules.txt. The project overview is in the basic-ideas.txt.
Consider the other epics in the prompts folder as they give context what was completed and what will be done later.
Folder WinterChallenge2026-Exotec contains the game code that we are creating an AI bot for.

## Stories for Implementation

### Story 6.1: Code Amalgamation Script (Python)
**As a** deployment pipeline,
**I want** a Python script to consolidate multiple isolated `.cpp` implementations and `.h` prototypes,
**So that** I can dynamically output an `amalgamated_bot.cpp` file compatible with the CodinGame platform's restrictions.
* **Acceptance Criteria:**
  * Correctly resolve `#include "local_module.h"` directives merging functions sequentially into proper namespaces or global scope.
  * Consolidate `#include <vector>`, `<array>`, `<chrono>`, and `<iostream>` imports to the single compilation header at the top of the combined file.
  * Exclude extraneous local test harnesses strictly tied to Epic 1 testing suites.

### Story 6.2: Constraints Verification & Payload Checks
**As a** deployment safety gate,
**I want** to automatically verify structural and performance requirements of the final concatenated payload,
**So that** my upload is consistently compliant and prevents silent platform rejection faults.
* **Acceptance Criteria:**
  * Programmatic file size verification ensuring the merged payload is significantly under the `100,000 KB` hard limit.
  * Line width constraint verification.
  * Automatically strip verbose local `std::cerr` print statements or ensure they are properly disabled via disabled `#define DEBUG` flags locally to optimize bot payload speeds in production.
