# Epic 4 Algorithm Overview

## Current Direction

Epic 4 is currently best understood as a **pathfinding bot with physics-aware simulation**, not as a generic adversarial search bot.

The strongest active ingredients are:
- **iterative deepening DFS** for long-range powerup planning
- **shortest-path style search with simulation** rather than static-grid pathfinding
- **branch pruning by legality, survivability, and threat checks**
- **heuristic move scoring** as a fallback and local selector

Alpha-beta is not the current direction. The practical core is still: simulate future movement under gravity and choose moves that preserve survival while progressing toward powerups.

---

## Search Structure

### 1. Global decision flow
For each of our snakes, the bot combines several search layers:

1. **Target assignment / routing context**
   - Voronoi control, target choice, cached gravity paths, role assignment.
2. **Planner-first tactical pathing**
   - If a target is strategic enough, use deep planning toward target progress or next power gain.
3. **Cached / BFS / path-following shortcuts**
   - Use precomputed path steps when they are still safe.
4. **Heuristic move scoring fallback**
   - Score each legal direction and choose the maximum-scoring move.

So the bot is not one single algorithm. It is a layered policy with a deep planner sitting above a heuristic local scorer.

---

## Deep Planner

### 2. `first_action_to_powerup_gain_budgeted()`
This is the main long-range planner for "what first move gives me the fastest future growth?"

It works as:
- start at some initial depth
- repeatedly run deeper searches while time remains
- keep the best first move found so far

This is **iterative deepening DFS**.

### 3. `first_action_to_powerup_gain()`
For each legal non-backward move:
- simulate the move in a copied physics state
- reject dead states
- reject newly added bad states:
  - **length-loss state**
  - **same-body-state as the branch start**
- if the move gains length immediately, score it as best possible (`steps = 1`)
- otherwise recurse into `min_steps_to_powerup_gain()`

This means the planner is optimizing:

> **minimum number of simulated steps until the snake grows**

### 4. `min_steps_to_powerup_gain()`
Recursive search returns:
- `0` if growth already happened
- `1 + recursive_result` if a future gain exists
- `999999` if dead / impossible / out of time

This is effectively a simulated shortest-path search in action space.

### 5. Planner pruning now includes
The planner now rejects branches when:
- move is backward
- move hits wall or snake
- simulated snake dies
- simulated snake becomes **shorter than branch start length**
- simulated snake has the **same full body hash** as the starting state of the branch

That last rule is a cheap anti-loop / anti-no-progress guard.

---

## Heuristic Local Scoring Fallback

When planner/cached routing does not commit a move, Epic 4 scores legal actions.

Each legal move starts with `score = 0`, then adds penalties/bonuses.

### Main score components

#### A. Danger map
- hard penalty in high-risk cells
- soft penalty in lower-risk cells

#### B. Flood fill survivability
- large penalty if the move leads into insufficient space
- small bonus if the move has enough room

#### C. Immediate powerup
- strong bonus for stepping onto a powerup
- special enclosed-map exception can flip this to a penalty

#### D. Target progress
- penalize distance remaining to assigned target
- bonus if target is reached

#### E. Powerup race heuristic
For each powerup:
- reward smaller own distance
- consider nearest opponent distance too
- take best such evaluation

#### F. Voronoi strategic terms
- expected length advantage
- our exclusive powerups
- opponent exclusive powerups

#### G. Role-specific terms
Collectors, support, defenders, suffocators, and killers each receive local bonuses/penalties.

#### H. One-step simulation safety
After simulating the candidate move:
- death penalty
- side/floor/open-edge exit penalties
- safe-followup bonuses
- zero/one-followup penalties
- adjacent-powerup bonus
- delayed risk penalties after collecting powerups
- backward-BFS-derived reward for short path to nearest future powerup

---

## Newly Added Evaluation Terms

Two requested penalties were added.

### 1. Smaller snake penalty
If a simulated future move produces a snake with **smaller length than current**, that move is penalized by about:

- `-20000`

Purpose:
- explicitly value future states with preserved length
- make shrinkage / lost-body outcomes unattractive even if still technically alive

Constant:
- `SHORTER_SNAKE_FUTURE_PENALTY = 20000`

### 2. No-movement / same-state penalty
If a simulated future state has the **same full body hash** as the starting snake state for that branch, that branch is treated as no-progress / missed opportunity.

Penalty:
- `-20000`

Purpose:
- reduce looping and wasted branches
- discourage rotation / oscillation that leaves the snake effectively in the same state

Constant:
- `SAME_STATE_OPPORTUNITY_PENALTY = 20000`

Implementation detail:
- full-body hash over ordered body positions, not just head coordinate
- this is stronger than comparing head location only

---

## What the bot is really optimizing now

At a high level:

1. **Planner path**: reach future growth as fast as possible, while pruning dead / regressive / same-state branches
2. **Fallback scorer**: choose the locally best action by combining danger, flood fill, target progress, Voronoi advantage, followups, and short-horizon future gain

So the bot is currently a **hybrid of deep growth-oriented planning and heuristic local path scoring**.

---

## Why this fits the project direction

This keeps the bot aligned with the current best direction:
- not alpha-beta
- not generic adversarial minimax
- but **physics-aware pathfinding with iterative deepening and pragmatic pruning**

That is the right frame for the current Epic 4 work.

---

## Next logical improvements

1. Improve planner leaf evaluation beyond only "time to next growth"
2. Penalize branch stagnation even more explicitly over 2-3 turns
3. Add snake-length-aware future value deeper into planner evaluation, not only fallback scoring
4. Use same-state detection for broader anti-loop handling in cached pathing too
5. Revisit opponent-aware planning only after path evaluation is stable

---

## March 19, 2026 evolution: reachable-frontier planning

During debugging of `complex-pathing/11-bigmap-E45Sx-long-term-target.txt`, a new failure mode became clear:

- the bot could mark an apple as "reachable"
- but that judgement was still partly based on a mixed set of path metrics
- and one of those metrics (`build_head_only_gravity_path_to_target()`) could be too optimistic because it validated only a gravity-settled head path, not the full future body evolution

This created a structural mismatch:

1. **Target assignment** could accept a long-range apple based on a simplified reachability test.
2. **Execution** then depended on fallback routing, cached path steps, walls-only BFS, and local heuristics.
3. The bot could oscillate in a basin while still believing the apple was strategically valid.

### Main diagnosis

The issue was not just "bad move choice".

The deeper issue was that Epic 4 mixed several incompatible notions of distance and reachability:

- **walls-only BFS distance**
- **gravity-aware path length**
- **head-only gravity reachability**
- **local fallback progress**

Those are individually useful, but together they can produce false confidence about long-term target accessibility.

### New design direction

The planned replacement direction is a cleaner single reachability model:

> **bounded reachable-frontier scanning over real simulated states**

Instead of first assigning a distant target and only later trying to route toward it with mixed fallback layers, the bot should:

1. simulate only states that are actually reachable under the game physics
2. scan those states up to a bounded ply depth
3. collect only the apples that are reached inside that simulated frontier
4. choose among those apples only

This is effectively a **simultaneous mapping and positioning** pass over the reachable state frontier.

### Reachable apples rule

Under this evolution, an apple is no longer considered reachable because a simplified planner says so.

It is considered reachable only if the bounded simulated frontier actually reaches a state where that apple is collected.

That makes the meaning of reachability much stricter and more consistent with the real physics.

### No-apple fallback rule

If no apple is found within the reachable ply depth, the bot should not keep inventing a questionable long-range apple route.

Instead it should:

1. choose a **long-term exploration goal** near the center of the map
2. persist that goal for later turns
3. keep moving toward it using the same reachable-frontier scan
4. override that long-term goal immediately when a reachable apple is discovered in a later scan

This preserves forward progress without relying on optimistic target certification.

### Why center is a good fallback

A center-leaning exploration goal is useful because it often:

- reduces commitment to dead-end side basins
- keeps more route options open
- increases the chance that later scans expose additional apples

The important detail is that the center target should also be chosen through the same reachability model whenever practical, so that fallback behavior remains consistent with the planner.

### Practical architecture change

This led to a new preferred structure for the deep-only successor bot:

1. **Reachable frontier scan** from the current single-snake state
2. **Reachable apple selection** if any apples are found in that frontier
3. **Persistent center-oriented exploration goal** if no apples are found
4. **Immediate override** of the long-term goal when a reachable apple appears

### Code-level implication

This evolution argues for a cleaner successor implementation rather than continuing to grow the already mixed `epic4-solver-BFS-bot.cpp` target-assignment and fallback stack.

The successor should:

- remove the mode-macro layering and unused compile-time branches
- keep the proven simulation core
- replace optimistic long-range target certification with reachable-frontier scanning
- make the long-term goal system explicit and traceable in logs

### March 19, 2026 bug fix: root frontier expansion under gravity

While validating the clean frontier bot on `complex-pathing/03` and `complex-pathing/04`, a deeper simulation bug was found.

The initial suspicion was that root children were being discarded by the frontier filter:

- `next_self->length < start_length`

That turned out to be a false lead.

The real issue was earlier in the simulation pipeline:

- after gravity, airborne snakes were being stamped back onto the grid
- that restamp included the head cell before collision resolution
- `resolve_collisions()` then interpreted the head as landing on a snake cell
- root children were falsely rejected as dead

So the frontier often collapsed to only the root state on short gravity maps, which then forced fallback behavior.

The fix in `epic4-reachable-frontier-bot.cpp` was to avoid stamping the airborne snake head before collision resolution and leave only the body stamped at that phase.

### Validation snapshot after the gravity fix, local contest safety pass, and trap-apple filter

Mirror validation against `epic4-reachable-frontier-bot.exe` on the focused regression set currently gives:

- `02-deadly apple.txt` -> `(6, 4)`
- `03 check gravity mid path-left side.txt` -> `(4, 3)`
- `04 check gravity mid path-right side.txt` -> `(4, 3)`
- `05 check gravity long path.txt` -> `(4, 3)`
- `06-plan-fall.txt` -> `(6, 3)`
- `11-bigmap-E45Sx-long-term-target.txt` -> `(7, 7)`

Interpretation:

- the gravity root-expansion bug is fixed
- the short-map regression cluster on `03`, `04`, and `06` was resolved by adding a local immediate-loss safety filter on top of the frontier choice, so weaker or equal snakes stop walking into short-range punishable head-to-heads
- `02` was a different bug: the frontier accepted a reachable apple even when the gained state had no simulated legal exit, so the snake walked into a corner trap and then collapsed to `expanded=1`; the fix was to reject post-growth states with zero simulated safe follow-ups
- long-range frontier behavior on map `11` remains restored at `(7, 7)`
- the focused regression set is now green on the current clean frontier bot

---

## Simplified bot variants

To make the code easier to understand without changing the current hybrid bot, the following working variants were created:

1. **Hybrid reference bot**  
   [bot-development/bots/epic4-solver-BFS-bot.cpp](bot-development/bots/epic4-solver-BFS-bot.cpp) / `epic4-solver-BFS-bot.exe`  
   This is the full layered bot with planner, BFS routing, cached pathing, local combat, and heuristic fallback.

2. **Deep search only bot**  
   [bot-development/bots/epic4-deep-search-only-bot.cpp](bot-development/bots/epic4-deep-search-only-bot.cpp) / `epic4-deep-search-only-bot.exe`  
   Uses only the iterative-deepening planner (`first_action_to_target_progress_budgeted()` / `first_action_to_powerup_gain_budgeted()`) plus a trivial legal fallback.

3. **BFS routing only bot**  
   [bot-development/bots/epic4-bfs-routing-only-bot.cpp](bot-development/bots/epic4-bfs-routing-only-bot.cpp) / `epic4-bfs-routing-only-bot.exe`  
   Uses only direct BFS routing to assigned targets plus a trivial legal fallback.

4. **Heuristic scoring only bot**  
   [bot-development/bots/epic4-heuristic-scoring-only-bot.cpp](bot-development/bots/epic4-heuristic-scoring-only-bot.cpp) / `epic4-heuristic-scoring-only-bot.exe`  
   Skips planner/BFS tactical commits and chooses from the local heuristic scoring loop only.

---

## Benchmark summary of the 4 simplified bots

All 4 variants were benchmarked against the same `Boss.py` opponent across the real `.txt` maps under `bot-development/test-maps`.

### Overall ranking

| Bot | Wins | Losses | Draws | Total score diff |
|---|---:|---:|---:|---:|
| Deep search only | 48 | 1 | 1 | 218 |
| Heuristic scoring only | 48 | 1 | 1 | 217 |
| BFS routing only | 48 | 1 | 1 | 216 |
| Hybrid reference bot | 48 | 1 | 1 | 212 |

### Main takeaway

The most important result is that the **deep search only bot finished first overall**.

This strongly supports the idea that the deepest value in Epic 4 currently comes from the iterative-deepening search direction, not from the extra mixed-in routing layers.

### Shared weak cases

All 4 bots had the same two non-wins:

- `enemies/01-danger-envelope-avoid.txt` → loss
- `enemies/02-local-alpha-beta.txt` → draw

### Maps where the variants differ meaningfully

- `enemies/03-dynamic-roles-coop.txt`  
   The hybrid bot underperformed; the simplified variants did much better.

- `tactics/04-kill-or-run.txt` and `tactics/15-kill-or-run-copy.txt`  
   Deep search / heuristic / hybrid beat BFS-only by a small margin.

- `tactics/08-self-untrap.txt`  
   Deep search / BFS / hybrid beat heuristic-only by a small margin.

### Owner interpretation

If the goal is to simplify the project while keeping the strongest current idea, the best candidate to grow next is:

**`epic4-deep-search-only-bot.exe`**

That bot isolates the planner direction cleanly and also produced the best aggregate benchmark result.
