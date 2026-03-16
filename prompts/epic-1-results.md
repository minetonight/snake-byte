# Epic 1: Project Setup, Orchestration, and Foundational Infrastructure - Results

This document summarizes the completed foundational infrastructure, which future epics should rely on.

## What We Have

*   **Orchestration Pipeline (Story 1.1):** 
    Automation and orchestration (CI/CD loops, prompt feeding, build fixing) are designated to be handled directly by the **Agentic IDE**. A custom Python orchestrator (`build_loop.py`) is bypassed.
*   **Scenario Testing Infrastructure (Story 1.3):** 
    We have a robust testing harness located at `bot-development/simulation/run_simulation.py`. The script connects compiled binaries via a subprocess directly to the Java engine replica found in `WinterChallenge2026-Exotec`.
    *   **Usage:** `python3 run_simulation.py <command path/to/bot1_cmd> <command path/to/bot2_cmd> [--map path/to/map.txt] [--seed 1234567]`
    *   The `test-maps` folder contains multiple targeted map scenarios that enforce rules of SnakeByte like collision, choke-point control, and gravity tests.
*   **Java Endpoints:** `HeadlessMain.java` and `Main.java` are configured to intercept these arguments dynamically. They split the arguments logically using the `|||` delimiter to assign bot execution strings and configure reproducible match seeds via `MultiplayerGameRunner`.
*   **C++ Base Protocol (Story 1.2):**
    `bot-development/bots/test1-solver-bot.cpp` accurately parses the initialization and turn inputs for the game. Additionally, it implements strict performance constraints utilizing `std::chrono` to prevent the engine from registering timeout losses beyond the 73ms threshold.

## How To Use It

1.  **Iterating on Bot Logic:** 
    Use the IO sequence defined in `test1-solver-bot.cpp` as your base template. Make sure to abide by the engine's 73ms time limit to output a valid move string: `WAIT`, `UP`, `DOWN`, `LEFT`, `RIGHT`. 
2.  **Firing up standard Headless Simulation:**
    Navigate to the `simulation` directory and specify the bot executables:
    ```bash
    cd bot-development/simulation/
    python3 run_simulation.py "/home/aleks/Development/Python/snake-byte/bot-development//your_new_bot.exe" "python3 /home/aleks/Development/Python/snake-byte/bot-development/bots/Boss.py"
    ```
3.  **Evaluating Edge-case Maps deterministically:**
    Pass a map logic file to test expected scores
    ```bash
    cd bot-development/simulation/
    python3 run_simulation.py "/home/aleks/Development/Python/snake-byte/bot-development/your_new_bot.exe" "python3 /home/aleks/Development/Python/snake-byte/bot-development/bots/Boss.py" --map /home/aleks/Development/Python/snake-byte/bot-development/test-maps/test_map_with2-eating.txt
    ```
4.  **Batch Validation:**
    Use `python3 run_simulation.py` edited to use `test_all_maps(bot1, bot2)` within the Python script to validate your new approaches against the entire suite of `tests-scenarios.md`.
5.  **Bots tournaments**
    Organize tournaments between bots versions, optionally hardcoding the game's internal RNG:
    ```bash
    cd bot-development/simulation/
    python3 run_simulation.py "/home/aleks/Development/Python/snake-byte/bot-development/your_new_bot.exe" "/home/aleks/Development/Python/snake-byte/bot-development/your_other_bot.exe" --seed 99999
    ```