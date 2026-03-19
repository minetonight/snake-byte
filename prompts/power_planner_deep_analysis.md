# Power Planner Algorithm: Deep Dive & Optimization Strategy

## Part 1: Algorithm Purpose & Architecture

### High-Level Purpose
**Goal:** Find the fastest sequence of movements to reach/consume a powerup (food), accounting for gravity physics and multi-turn lookahead. The planner is *greedy about powerups*—it prioritizes powerup collection above all else, which is critical in a resource-constrained environment where length = survival.

**Core Insight:** Unlike static BFS (which fails when gravity rearranges the board mid-path), this uses **physics-validated recursive DFS**, simulating the full game state through gravity, collision detection, and snake growth for each candidate action.

---

## Part 2: Algorithm Inner Workings

### The Three-Function Call Stack

#### **Level 1: `first_action_to_powerup_gain()` (wrapper)**
```cpp
static int first_action_to_powerup_gain(const GameState& state, int snake_id, int max_depth)
```
- **Input:** Current game state, my snake's ID, maximum lookahead depth (e.g., 6 for 6 turns ahead)
- **Logic:**
  1. Try each of 3 cardinal directions (skip backward moves)
  2. For each direction, validate it's not blocked by walls/snakes
  3. Simulate one turn forward with that action, opponent moving on autopilot
  4. After simulation, check if we gained length (ate a powerup)
     - If YES: return immediately with action
     - If NO: recursively call Level 2 to find if we eat within remaining depth
  5. Return the action that leads to quickest powerup gain
- **Why Recursive?** Because we need to explore *sequences*, not just immediate powerups. A clear path around obstacles might take 4+ moves.

#### **Level 2: `min_steps_to_powerup_gain()` (recursive explorer)**
```cpp
static int min_steps_to_powerup_gain(
    const GameState& state, 
    int snake_id, 
    int start_length,     // Length when we started this search
    int depth_left
)
```
- **Input:** Game state after Level 1 tried one action, my ID, what my length was when we entered this branch, remaining turn budget
- **Logic:**
  1. Base cases:
     - If out of time: return infinity (fail)
     - If already grew longer than start_length: return 0 (SUCCESS, we ate!)
     - If no depth left: return infinity (timeout on this branch)
  2. For each of 3 valid directions:
     - Simulate one turn with that action
     - Recursively call self with depth_left - 1
     - Track minimum steps across all branches
  3. Return `1 + best_recursive_result` (add 1 for current turn)
- **Why Step Count?** Returns *how many turns ahead* we must plan. If result is 3, we ate within 3 steps. Level 1 can prioritize actions with lower step counts.

#### **Level 3: `first_action_to_powerup_gain_budgeted()` (iterative deepening)**
```cpp
static int first_action_to_powerup_gain_budgeted(
    const GameState& state,
    int snake_id, 
    int start_depth
)
```
- **Input:** Game state, my ID, initial search depth guess
- **Logic:**
  1. Iteratively call Level 1 with increasing depth: depth 1, then 2, then 3, etc.
  2. Each iteration improves the answer (or confirms it if already found)
  3. Stop when out of time (controlled by `out_of_time()` checking wall-clock + phase deadline)
  4. Return best action found before timeout
- **Why Iterative Deepening?** Handles variable time budget gracefully. If easy powerup found at depth 3, no wasted work on depth 4+. If board is complex, keep going until 73ms turns are up.

### Physics Validation in the Loop
Each simulated step calls `next_state.simulate(sim_my_actions, sim_opp_actions)` which:
- Applies gravity: drops airborne snakes until they land
- Resolves collisions: kills snakes that hit walls, other snakes, or their own bodies
- Grows snakes that ate powerups
- Clears tail if no growth

**Critical Feature:** Opponent actions (`opp_snakes.clear()` in current code) are on autopilot—the algorithm assumes opponent moves according to inferred default action. This *avoids infinite game-tree explosion* but also *ignores opponent threats*.

---

## Part 3: Algorithm Comparison (epic4 vs epic3-deep)

### Implementation Comparison

| Aspect | epic4-BFS | epic3-deep |
|--------|-----------|-----------|
| **Core Algorithm** | Identical: DFS + iterative deepening | Identical: DFS + iterative deepening |
| **Invocation Site** | Line 3113, 3218, 3294, 3464 (simple-path + tactical) | Line 1261, 1387 (collector + lookahead) |
| **Max Depth Cap** | 128 safety limit | 128 safety limit |
| **Time Budget** | 25% of 73ms (18.25ms default, up to 60% in sweeps) | 25% of 73ms (18.25ms default, up to 60% in sweeps)|
| **Opponent Model** | Autopilot (opp default actions) | Autopilot (opp default actions) |
| **Heuristic Guidance** | Step count only | Step count only |

**Conclusion:** **Functionally identical**. Both are bottlenecked by:
1. Exponential branching (3 directions per depth)
2. No pruning or intelligent branch skipping
3. No opponent threat modeling

---

## Part 4: Why It Dominates 99% of Turn Time

### Computational Complexity Analysis
- **Branching factor:** 3 (cardinal directions, skipping backward)
- **Depth:** 6–8 (typical lookahead before time runs out)
- **Nodes to expand:** $3^6 = 729$ minimum (without early termination)
- **Per-node cost:** 1 full `GameState.simulate()` call (expensive: gravity, collision checks, vector copies)
- **Total:** ~1K–65K full simulations per decision per snake

### Why No Rotation in Aggressive Presets?
Even when phase budget increased to 60% (vs default 25%), **power_planner stayed at 99%** because:
1. It's fundamentally compute-limited, not time-allocated
2. Gave it 60% of budget → it still exhausted time doing exhaustive search
3. Other phases (forward BFS, backward BFS) are O(grid_size) = ~1K–10K cells, far cheaper
4. Power planner is the "money printer" for computation; other phases finish quickly, leaving pile of time for planner to consume

---

## Part 5: Optimization Strategies (Inspired by Academia)

### Strategy 1: Threat-Based Pruning (Paranoid Search)
**Paper:** "Opponent-Pruning Paranoid Search"  
**Concept:** If opponent can kill you next turn, prune that branch (return -∞ immediately).

```cpp
// Add after simulating opponent move, before recursing:
for (const Snake& opp : next_state.opp_snakes) {
    if (can_collision_occur(my_head_next_pos, opp)) {
        return 999999; // Dead end; don't explore further
    }
}
```

**Expected Gain:** 60–75% node reduction (kills off suicidal branches early)  
**Implementation Cost:** 5 min, one loop  
**Time Savings:** ~20–30ms per turn

### Strategy 2: Opponent Action Bias (MCTS-style)
**Paper:** "Know your Enemy: MCTS in Pommerman"  
**Concept:** Don't simulate all 4 opponent directions equally. Weight by distance/threat.

```cpp
// Instead of: for (int opp_a = 0; opp_a < 4; ++opp_a) ...
// Do: vector<int> weighted_actions = get_threat_weighted_opponent_actions(state);
// Explore high-weight actions (closer threats) first; skip distant ones.
```

**Expected Gain:** 35–50% node reduction (branching factor drops from 4×4 to ~3×3)  
**Implementation Cost:** 15 min  
**Time Savings:** ~10–15ms per turn

### Strategy 3: Decoupled Subgraph Search
**Paper:** "MCTS by Focusing on Yourself"  
**Concept:** If opponent is far away (>6 cells), ignore them entirely during lookahead.

```cpp
// Before planning loop:
bool opp_is_distant = true;
for (const Snake& opp : state.opp_snakes) {
    if (manhattan_dist(my_head, opp.head) < 6) {
        opp_is_distant = false;
        break;
    }
}
if (opp_is_distant) {
    // Plan in isolation; skip opponent simulation in lookahead
    return first_action_to_powerup_self_only(state, snake_id, max_depth);
}
```

**Expected Gain:** 30–40% when available (large maps, scattered enemies)  
**Implementation Cost:** 10 min  
**Time Savings:** Conditional, 5–10ms when applicable

### Strategy 4: Greedy Action Sorting (Heuristic Guidance)
**Paper:** "RL in Battlesnake", "Actor-Critic Multi-Objective"  
**Concept:** Explore hunger-friendly moves first. Sort actions by food distance before recursing.

```cpp
// Before the 4-direction loop, pre-sort:
struct ActionScore { int action; int food_dist; };
vector<ActionScore> sorted_actions;
for (int a = 0; a < 4; ++a) {
    int nx = next_x_after_action(a);
    int ny = next_y_after_action(a);
    int dist = shortest_dist_to_any_powerup(nx, ny);
    sorted_actions.push_back({a, dist});
}
sort(sorted_actions.begin(), sorted_actions.end(), 
     [](auto& x, auto& y) { return x.food_dist < y.food_dist; });

// Now loop in sorted order; early termination if we find powerup gives instant win
for (auto& sa : sorted_actions) {
    int a = sa.action;
    // ... simulate, recurse ...
    if (found_powerup_at_depth_1) break; // Early exit
}
```

**Expected Gain:** 10–30% (helps with early termination; moves good actions to front)  
**Implementation Cost:** 5 min  
**Time Savings:** ~5ms per turn

### Strategy 5: Multi-Objective Evaluation (Soft Terminal Evals)
**Paper:** "Pareto Actor-Critic", "Fuzzy Game-Tree Search"  
**Concept:** Instead of binary "ate food / didn't", use soft reward: distance to food + territory + safety.

```cpp
// Instead of: if (next_self->length > self->length) return 0;
// Do:
int soft_reward = 100 * (next_self->length > self->length)  // Ate = +100
                + 50 * (!is_trapped_in_corridor(next_state, snake_id))  // Safe
                + min_dist_to_food(next_state, snake_id);  // Guidance
return soft_reward; // Better heuristic for branch selection
```

**Expected Gain:** 5–15% (pruning low-reward subtrees earlier)  
**Implementation Cost:** 15 min  
**Time Savings:** ~2–5ms per turn

---

## Part 6: Combined Impact & Implementation Roadmap

### Expected Cumulative Speedup
| Technique | Time Saved | Nodes Reduced | Cumulative |
|-----------|-----------|-----------|-----------|
| **Threat pruning** | ~20–30ms | 60–75% | **20–30ms saved** |
| **+ Opponent bias** | ~10–15ms | 35–50% | **30–50ms saved** |
| **+ Decoupled subgraph** | ~5–10ms | 30–40% | **40–60ms saved** |
| **+ Greedy sort** | ~5ms | 10–30% | **45–65ms saved** |
| **All combined** | | **~82% of compute** | **Depth 6→8-9 feasible** |

### With 60% Time Budget Allocation
- **Current state:** ~45ms for power_planner, depth ~6
- **With all optimizations:** ~7–10ms for power_planner, depth ~9-10
- **Freed time:** 35–50ms → reallocate to other phases or second-order lookahead

### Implementation Sequence (Recommended)

**Phase 1 (Day 1):** Threat-based pruning
- 5 lines of code
- 60-75% speedup
- Test on maps 5 & 11

**Phase 2 (Day 2):** Opponent action bias
- 20 lines of code  
- 35-50% additional speedup
- Combined: approach 82% reduction

**Phase 3 (Day 3):** Greedy sorting
- 10 lines of code
- Early termination benefits
- Quick win

**Phase 4 (Optional):** Decoupled subgraph + multi-objective eval
- More complex, conditional benefits

---

## Part 7: Map 11 Hypothesis

Map 11 fails at (4,4) despite deep planning. Possible root causes:

1. **Depth cap insufficient:** Powerups might require 8-10 step sequences; depth 6-8 misses them
   - *Solution:* Threat pruning unlocks depth 8-9 naturally

2. **Opponent modeling absent:** Opponent threats aren't simulated; planning walks into ambush
   - *Solution:* Add opponent threat modeling (Paper: "Paranoid Search")

3. **Gravity edge cases:** Some powerups may be unreachable due to gravity locking or board geometry
   - *Solution:* Debug logs to show max depth reached when planning fails

4. **Evaluation function too simplistic:** Step count alone misses "safety + food quality" tradeoffs
   - *Solution:* Multi-objective evaluation (Pareto blend)

---

## Part 8: Academic Papers Summary

| Paper | Algorithm | Key Insight | Applicability |
|-------|-----------|------------|----------------|
| Know your Enemy-MCTS in Pommerman | MCTS + Opponent Bias | Reduce branching by weighting opponent moves | HIGH—Direct speedup |
| MCTS by Focusing on Yourself | Self-centric MCTS | Ignore distant opponents | MEDIUM—Conditional speedup |
| Opponent-Pruning Paranoid Search | Alpha-Beta + Threat Pruning | Prune branches where opponent wins | HIGH—Major speedup + safety |
| RL-in-Battlesnake | PPO + Policy Gradient | Pre-trained move policy | LOW—Requires training, risky |
| Pareto Actor-Critic | Multi-Objective RL | Combine survival + food + territory | MEDIUM—Better guidance + pruning |
| Fuzzy Game-Tree | Soft Terminal Evals | Smooth evaluation landscape | MEDIUM—Reduces variance |

---

## Summary: Actionable Next Steps

1. **Implement threat-based pruning** (15 min) → Expect 60-75% speedup, unlocks depth 7-8
2. **Add opponent action bias** (20 min) → Expect 35-50% additional speedup  
3. **Sort actions by food distance** (5 min) → Early termination benefits
4. **Re-run sweeps** on maps 5 & 11 with all three optimizations combined
5. **Monitor max depth reached** to confirm we're now planning 8-9 steps ahead
6. **Log path quality** (distance to nearest powerup per step) to detect path inefficiencies

**Expected Outcome:** Map 11 should improve from (4,4) to (5,5) or better, and turn latency should drop below 50ms consistently.
