# Epic 2: Core Game Logic Implementation - Results

This document summarizes the achievements, architectural decisions, and debugging knowledge gained during the implementation of Epic 2 (Core Game Logic). This serves as a foundation for Epic 3.

## What was completed
We implemented the core hyper-optimized C++ game state physics engine (`epic2-solver-bot.cpp`) to recreate the rules of SnakeByte perfectly while maintaining sub-millisecond execution times.

### 1. Data Structure Formulation
The grid was built out of a flat 1-dimensional array (`std::vector`). Instead of statically guessing sizes, the bot dynamically configures the required logical size on Turn 1 based on the exact properties of the map input:
- `MAX_LEN = 3 + total_powerups_count`
- `max_width = (2 * MAX_LEN) + world_width`
- `max_height = (2 * MAX_LEN) + world_height`

This ensures that snakes can legitimately climb above the visible map ceiling and fall far beneath the board mathematically without causing out-of-bounds errors, while preventing heap allocations (`std::vector` is allocated exactly once per state).

### 2. Physics & Rules Engine
A unified `simulate()` method processes one complete game turn internally.
- **Movement (`apply_movement`)**: Handles array shifting for snake body indexing representing all 4 directions natively. Default action is `WAIT` (which continues previous direction).
- **Collisions (`resolve_collisions`)**: Simultaneous rule resolution checks for heads colliding with bodies or walls, immediately removing them or enforcing the multi-consume length penalty where heads clash over an apple.
- **Gravity (`apply_gravity`)**: Enforces cascade falling algorithms natively on the 1D Array. If no cell under a snake's body is solid, it shifts downwards. **Important**: Powerups (`CELL_POWERUP`) act as solid platforms. Resting on them stops a snake from falling.

### 3. State Cloning (Performance)
State structure is intentionally lightweight. The `GameState` copies the single vector buffer and the circular deque array indices for `Snake` models. Local compiler tests showed 10,000 deep branches being executed and cloned sequentially in ~31ms total (Avg 0.003ms per clone).

## Debugging and Engine Knowledge

Dealing with silent `-1` crashes in a competitive programming engine like Codingame's Java Referee is notoriously difficult. Here are the techniques we put in place that will save time moving forward:

### 1. Python Subprocess Shell-Catching
The Python runner script (`run_simulation.py`) was modified to watch for crash states. If the Regex parser fails to find a valid `Player 1 Score:` (usually because the engine crashed or errored out), the Python script now forcefully dumps the entire `stdout` and `stderr` payload natively from the Java Subprocess.

### 2. Side-channel File Logging (The `bot_log` pattern)
The standard Java engine (`HeadlessMain.java`) strictly intercepts `stdout` and `stderr` streams from your bot. If the bot crashes, the Java engine often swallows the final error lines and exits silently.

To bypass this, we can import `<fstream>` directly into the C++ bot code and append logs to an external file independently of the standard output:
```cpp
#include <fstream>
// ...
ofstream log("bot_log.txt", ios::app);
log << "[DEBUG] Variables: " << my_var << endl;
```
Because the `run_simulation.py` Python script executes the Maven Java command with its Current Working Directory set to the `WinterChallenge2026-Exotec/` folder, the relative file path `"bot_log.txt"` will be written in that Java project directory natively, allowing us to inspect exactly how far our bot's execution got before crashing.

### 3. Turn-Based Microsecond Chronometers
We added `std::chrono` trackers to the C++ event loop that measure the microseconds elapsed per turn. We can now accurately measure if our minimax algorithms trigger the strict engine timeout rules dynamically. 
*Note:* The user updated `Referee.java` to increase the maximum turn time from `50ms` to `73ms` (`gameManager.setTurnMaxTime(73)`). That change reflects the actual allowed running time on the challenge website, that is more forgiving than the gamerules. Our C++ initialization time is currently ~500 microseconds, well within the limit.

### 4. Output Formatting
The engine expects strict output formatting. For controlling multiple snakes, the action must include the snake ID followed by the command, separated by semicolons (e.g., `1360 UP;1361 WAIT`). If a single string `WAIT` is output, the simulator may apply it as a default, but explicit IDs are safer and required for split actions.

### 5. Standard In/Out Stream Desync
If your engine crashes (e.g. `Boss.py` opponent crashes), the Java environment closes the C++ stdout/stdin pipes. If your C++ bot uses standard `cin >> variable;` without checking the stream status natively, it will enter an infinite loop spinning against EOF! 
Always test inputs by prefixing with:
```cpp
if (!(cin >> variable)) {
    // EOF. Game shutting down. Avoid outputting anything or just exit cleanly!
}
```
**CRITICAL**: Do NOT use `exit(0)` or `break` to shut down gracefully if you've already started reading, or the Java Engine will penalize you with `-1` for early termination. Simply bypass reading uninitialized memory (`int x = 0; int y = 0;`) and let your loop spit out `-1` or `WAIT` instantly to satisfy the shutdown buffer without triggering Out of Bounds Segfaults.

### 6. C++ Local Unit Testing
To ensure the `simulate()` cloning stays below the 0.1ms rule required by our Minimax tree, the C++ solver includes an `#ifdef LOCAL_TEST` directive inside `epic2-solver-bot.cpp`.
Running `g++ -O3 -std=c++17 -DLOCAL_TEST epic2-solver-bot.cpp -o local_test.exe` compiles the bot to run 10,000 isolated game turns in memory natively. Use this structure to catch infinite loops in gravity arrays before touching the Java Engine!