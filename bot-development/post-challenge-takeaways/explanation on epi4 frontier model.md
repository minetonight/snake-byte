# 1. Big flow overview
The whole bot has 4 conceptual layers:

Read current turn input and rebuild the board
Represent the world as a GameState
For each of my snakes, run a bounded search over simulated future states
Choose the first action of the best-looking future and output it
The main loop is in epic4-reachable-frontier-bot.cpp:1403-1657.

In plain English
On each turn, the bot does this:

rebuilds walls, apples/powerups, and snake bodies
for each controlled snake:
decide a current long-term target:
a remembered apple goal, if still valid
otherwise a remembered center-ish anchor cell
choose a search budget based on board size and time left
scan reachable future states using simulation
prefer:
a reachable apple that can be eaten safely
otherwise a move that makes progress toward an apple
otherwise a move that makes progress toward the center goal
otherwise a basic legal move
finally, run a danger filter against stronger nearby snakes
output actions
So the strategy is:

“Look ahead a bit, using real game rules, pick the best future, but stay within the time budget.”

# 2. The mental model you should keep in your head
If you understand these 3 things, the file becomes much easier:

 ## A. The board is a padded 1D array
The world is stored as one flat vector<int16_t> called grid inside GameState, declared around epic4-reachable-frontier-bot.cpp:252-255.

Instead of using (x, y) objects everywhere, the code converts a cell to:
```
𝑝𝑜𝑠=𝑦⋅𝑚𝑎𝑥_𝑤𝑖𝑑𝑡ℎ+𝑥
```
That is old-school, fast, and common in game bots.

It also pads the world with extra margin on all sides during first-turn initialization in epic4-reachable-frontier-bot.cpp:1438-1460.

Why padding?
Because snakes can fall and move beyond the original visible map.
So the bot creates a larger simulated space than the visible board.

This is a practical systems trick:

simpler simulation
fewer special cases at the border
easier gravity handling
 ## B. A snake is stored as a ring buffer
The snake is represented by Snake at epic4-reachable-frontier-bot.cpp:170-176.

Important fields:

body: array of body positions
head_idx
tail_idx
length
This is a circular buffer idea.

Why this matters
Instead of shifting the entire body array on every move, the bot:

moves head_idx
moves tail_idx
overwrites one slot
That is much cheaper than array-copying.

As a Java 6 analogy: think of it like implementing your own ultra-fast ArrayDeque<int> for snake segments.

## C. Search nodes store full simulated states
A search node is:

GameState state
first_action
depth
head_pos
See SearchNode at epic4-reachable-frontier-bot.cpp:995-999.

That means this is not a symbolic search like “distance only”.
It is a world-state search.

Each node is a complete simulated future board.

That is why the bot can reason about:

gravity
collisions
growth after eating a powerup
whether follow-up moves still exist
This is the most important architectural choice in the file.

# 3. Big flow, step by step
### Step 1: one-time initialization
At the start of the game, main() reads:

my player id
world width and height
initial wall rows
which snake ids belong to me
See epic4-reachable-frontier-bot.cpp:1410-1431.

Then, on the first turn, it computes:

max_len
padded max_width
padded max_height
grid_size
See epic4-reachable-frontier-bot.cpp:1444-1459.

Why max_len = 3 + total_powerups_count?
Because a snake can grow by eating powerups.
So the body array is pre-sized large enough to hold future growth.

That avoids reallocating body vectors every turn.

### Step 2: rebuild current turn state
Each turn:

reset the grid from static_walls
place powerups
parse every snake body string
mark body cells in the grid
split snakes into my_snakes and opp_snakes
See epic4-reachable-frontier-bot.cpp:1462-1512.

This is a clean pattern:

persistent static terrain + fresh dynamic entities each turn

So the bot does not carry fragile mutable state across turns.
It reconstructs truth from the input.

That is robust.

### Step 3: pick a goal for each of my snakes
Inside the loop over state.my_snakes, the bot first decides what the snake is trying to achieve.

See epic4-reachable-frontier-bot.cpp:1544-1566.

There are two goal types:

 - A. apple goal
A remembered target powerup, if still valid:

not expired
still present on the board
not already at the snake head
This logic is in get_valid_apple_goal() at epic4-reachable-frontier-bot.cpp:1353-1359.

 - B. center goal
If there is no valid apple goal, the bot chooses a center-ish anchor cell.

That comes from get_or_refresh_center_goal() at epic4-reachable-frontier-bot.cpp:1337-1349, which uses choose_center_anchor_cell() at epic4-reachable-frontier-bot.cpp:919-947.

Why remember goals?
Without memory, a search bot often oscillates:

turn 1: go left
turn 2: go right
turn 3: left again
TTL-based goal memory gives directional stability.

This is a classic practical AI trick.

### Step 4: choose search budget
The bot is time-bounded: TURN_BUDGET_MS = 72 at epic4-reachable-frontier-bot.cpp:28-38.

Budget selection is in choose_scan_budget() at epic4-reachable-frontier-bot.cpp:1260-1335.

It adjusts:

depth limit
expansion limit
deep scan milliseconds
follow-through milliseconds
Based on:

board area
number of snakes
index of current snake in processing order
whether it already has an apple goal
how much turn time remains
Why this matters
This is not purely algorithmic theory.
This is engineering.

A smart bot that times out is worse than a mediocre bot that always replies.

### Step 5: search future states
This is the heart of the bot: scan_reachable_frontier() at epic4-reachable-frontier-bot.cpp:1074-1258.

This function performs a BFS-like frontier scan over simulated future states.

It starts from the current state, then repeatedly:

pops a node from a queue
tries 4 actions
simulates the next state
scores useful outcomes
pushes unseen good states back into the queue
This is the key idea you want to internalize:

The queue contains future worlds, not just future positions.

### Step 6: choose action by priority
After the scan, the bot chooses according to this preference order in epic4-reachable-frontier-bot.cpp:1572-1604:

reachable_apple
apple_progress
apple_goal or center_goal
fallback legal move
Then it runs a final danger override:

if the chosen action is immediately unsafe versus stronger nearby snakes
try a safer alternative with choose_safe_action_for_target()
See epic4-reachable-frontier-bot.cpp:1606-1612.

This is a common layered design:

optimistic planner first
tactical safety filter second
# 4. State changes: how the world actually evolves
You specifically asked for state changes next, so let’s go deep there.

The simulation pipeline is in GameState::simulate() at epic4-reachable-frontier-bot.cpp:439-442:

apply_movement()
apply_gravity()
resolve_collisions()
That ordering is extremely important.

## 4.1 apply_movement()
See epic4-reachable-frontier-bot.cpp:340-369.

For each snake:

get current action
convert action to new head cell
remove tail cell from grid
rotate ring buffer indices
write new head into the ring buffer
Conceptually
Before move:

head at H
body behind it
tail at T
After move:

new head inserted in front
old tail disappears
body shifts by changing indices, not by copying whole arrays
Important subtlety
The method does not fully resolve consequences yet.
It just performs movement.

So after movement:

a snake may now overlap a wall
two heads may now be in same cell
a snake may now be unsupported and should fall
Those effects are handled later.

This separation is good design:

one function: move
one function: gravity
one function: resolve damage/collisions
## 4.2 apply_gravity()
See epic4-reachable-frontier-bot.cpp:269-338.

This is the most domain-specific method in the file.

What it does
It repeatedly finds snakes that are not grounded, and moves them downward by one row until everything stable has settled.

Grounding logic
is_cell_grounded() at epic4-reachable-frontier-bot.cpp:256-267 says a cell is grounded if below it there is:

bottom/out of grid
wall
powerup
body of another already grounded snake
Then apply_gravity():

collects all alive snakes as initially airborne
repeatedly marks snakes as grounded if any body segment is grounded
clears old body cells for airborne snakes
drops airborne snakes by +max_width
kills a snake if it falls fully out of world
redraws airborne snakes after moving
Why the repeated loop?
Because grounding can depend on other snakes.

Example:

snake A rests on wall
snake B rests on snake A
snake C rests on snake B
You cannot detect all of that in one pass unless you propagate grounding.

That is why there is this fixed-point style loop:

keep marking grounded snakes
until no more change
As a teaching note: this is very similar to dependency propagation in dataflow systems.

## 4.3 resolve_collisions()
See epic4-reachable-frontier-bot.cpp:372-437.

This handles:

out-of-bounds head positions
head into wall
head into occupied snake cell
head-to-head collisions
eating powerups
shrink/death effects
The logic
For each alive snake head:

record head position
mark whether it should be destroyed
mark whether it ate powerup
Then for each snake:

if ate powerup:
extend tail
increment length
if destroyed:
decrement length
advance head index
if length < 3, mark dead and clear body from grid
Then:

remove eaten powerups from grid
redraw all surviving snakes into grid
Why redraw at end?
Because after a batch of collisions/growth/deaths, the board occupancy must reflect the final consistent state.

This is a classic “compute results first, apply board write-back after” pattern.

# 5. The search algorithm in student-friendly terms
Now the most important concept.

This is basically BFS over future game states
If you know recursion, think of this search like:

recursive version:
from state S, try all legal actions
recurse into next states
stop at depth limit
This file uses an iterative queue-based version, not recursion.

Why BFS here?
Because BFS explores shallower futures first.
That is useful when you care about:

shortest time to apple
first move that quickly leads to a good future
The queue-based search is inside scan_reachable_frontier() at epic4-reachable-frontier-bot.cpp:1074-1258.

## 5.1 Why “reachable frontier”?
Because the bot does not score all cells abstractly.

It explores the frontier of actually reachable future states by simulation.

Each expansion means:

“Take one state on the frontier, try one action, simulate the real rules, and if still alive, add that new state to the frontier.”

That is much stronger than plain shortest path to apple.

Plain shortest path would ignore:

gravity consequences
growth consequences
future trap after eating
body evolution
This bot explicitly cares about those.

## 5.2 What a search node stores
A SearchNode contains:

full GameState
first_action
depth
head_pos
See epic4-reachable-frontier-bot.cpp:995-999.

Why store first_action?
Because when the bot reaches a good future node at depth 7, it still only needs to output one action now.

So every node remembers:

“What was the first move from the root that eventually led here?”

This is a standard search trick.

## 5.3 The search loop
At a high level, scan_reachable_frontier() does:

isolate the state so only this snake is planned in detail
initialize queue with starting node
maintain best_depth hash table to avoid revisiting same states
while queue not empty and budget not exhausted:
pop node
update best apple-progress / goal-progress info
if depth limit reached: continue
try actions 0..3
reject illegal/backward moves
simulate next state
reject dead or weaker outcomes
if ate apple, verify it’s not a dead end
hash next state
if not already seen at a shallower depth, enqueue it
That is the algorithm in one paragraph.

# 6. Why the bot isolates one snake during planning
See isolate_state_for_single_snake_planning() at epic4-reachable-frontier-bot.cpp:667-687.

This function:

copies the grid
converts all snake-occupied cells into:
CELL_EMPTY if it is our own snake
CELL_WALL if it belongs to any other snake
keeps only this snake in my_snakes
Why do this?
Because full multi-agent planning explodes combinatorially.

If 4 snakes each have 4 actions, one ply already has:

4^4 = 256

At depth 6 that becomes enormous.

So this file chooses a pragmatic compromise:

plan my snake deeply
treat others mostly as static obstacles
add a separate tactical punishment check for nearby stronger snakes
This is a classic engineering tradeoff:

less optimal than full adversarial search
much cheaper
often good enough
# 7. Safety checks and space checks
These are very important because shortest path alone gets snakes killed.

## 7.1 Flood fill for survival space
survives_flood_fill() is at epic4-reachable-frontier-bot.cpp:450-481.

This is a standard BFS flood fill:

start from a cell
count reachable empty/powerup cells
stop early if enough space found
Then count_safe_followups() at epic4-reachable-frontier-bot.cpp:483-501 uses it to count how many next moves leave enough space.

Intuition
A cell may be legal but still terrible if it enters a tiny pocket.

So the bot asks:

“If I go there, do I still have breathing room?”

That is what flood fill is approximating.

As a student mental model:

shortest path says “can I get there?”
flood fill says “once there, can I live there?”
Both matter.

## 7.2 Stronger snake punishment check
is_action_immediately_unsafe_against_stronger_snake() is at epic4-reachable-frontier-bot.cpp:785-824.

This method asks:

“If I take this action, is there any nearby equal-or-stronger snake that can answer with a move causing me to die or shrink immediately?”

It only checks nearby snakes within Manhattan distance 4.

Why local only?
Because checking every enemy everywhere would be too expensive.

This is a local tactical threat model:

limited scope
high impact
cheap enough to run often
Then choose_safe_action_for_target() at epic4-reachable-frontier-bot.cpp:826-859 picks a safer alternative that still:

makes target progress if possible
otherwise stays near target
prefers more future followups
So this acts like a final tactical correction layer.

# 8. Path helpers: what they do and why they are simpler than the full planner
These helpers are in the middle of the file.

shortest_path_distance_walls_only()
See epic4-reachable-frontier-bot.cpp:594-622.

This is plain BFS on cells:

ignores dynamic snake motion
only blocks on walls
used as a cheap heuristic
first_action_toward_cell_walls_only()
See epic4-reachable-frontier-bot.cpp:624-665.

Also a BFS, but it remembers the root action leading to each reached cell.

Why “walls only”?
Because this is intentionally cheap and optimistic.
It is not trusted for final life-or-death planning; it is used as guidance.

That distinction is important:

heuristic helper: fast, approximate
state simulator: slower, more real
This bot uses both.

# 9. Apple logic: reachable apple vs apple progress
The bot distinguishes two concepts.

### A. Reachable apple
A simulated path actually leads to growth:

after simulation, snake length increased
and follow-up safety is acceptable
This is found inside scan_reachable_frontier() around epic4-reachable-frontier-bot.cpp:1192-1225.

### B. Apple progress
Even if it cannot prove a safe apple capture now, it remembers which first action gets it closer to a specific existing apple, while keeping decent future options.

That is tracked using AppleProgressCandidate and finalized around epic4-reachable-frontier-bot.cpp:1238-1254.

Why this distinction is good
Because often:

there is no fully proven safe apple path within the budget
but one move is clearly better than random wandering
So the bot uses “progress” as a second-best plan.

That is a very practical design.

# 10. Dead-end prevention after eating
This is one of the smarter parts.

When the scan finds a simulated apple-eating state, it does not instantly accept it.

It checks:

are there any safe follow-up moves?
on small boards with few apples, are there safe follow-through moves?
if more apples remain, is future growth still reachable?
See the logic around epic4-reachable-frontier-bot.cpp:1192-1213 and has_reachable_future_growth() at epic4-reachable-frontier-bot.cpp:1003-1072.

Why this matters
Naive bots often die because they treat “ate apple” as automatically good.

This bot asks:

“Yes, but what happens after I eat it?”

That is a more mature planning criterion.

# 11. Goal persistence and contest logic
Center goals
get_or_refresh_center_goal() at epic4-reachable-frontier-bot.cpp:1337-1349

The bot keeps a center-ish anchor for LONG_TERM_GOAL_TTL turns.

Apple goals
set_apple_goal() / get_valid_apple_goal() at epic4-reachable-frontier-bot.cpp:1353-1367

Once the bot commits toward an apple, it remembers it for APPLE_GOAL_TTL.

Contest detection
is_target_contested_by_stronger_snake() at epic4-reachable-frontier-bot.cpp:1377-1401

This compares shortest path distances:

if a stronger snake can get there almost as fast, avoid contest
if equal length but strictly faster, also avoid contest
Why this is useful
This prevents chasing apples that are probably losing races.

Again: not perfect adversarial reasoning, but a cheap useful approximation.

# 12. Timing and optimization routines
Now to the “optimisation routines” part.

This file is full of practical optimizations.

## 12.1 Hard turn budget
Constants at epic4-reachable-frontier-bot.cpp:28-38.

The bot must finish inside TURN_BUDGET_MS.

elapsed_turn_ms() and out_of_time() are in epic4-reachable-frontier-bot.cpp:73-131.

This lets any search routine exit early.

Why this matters
In real-time bots, an approximate answer on time beats a perfect answer too late.

## 12.2 Split phase budgets
The bot splits work into:

DeepScan
FollowThrough
See SearchPhase, PhaseTimingBudget, and ScopedSearchPhase at epic4-reachable-frontier-bot.cpp:45-120.

Why split?
Because deep scan is the main search, but expensive post-apple follow-through validation should not consume everything.

So the bot says:

spend most time exploring
spend some time validating promising apple captures
keep reserve time for output and other snakes
That is very good performance hygiene.

## 12.3 Depth and expansion caps
In choose_scan_budget() and the search loops:

depth is capped
node expansions are capped
This is the simplest and most important anti-explosion measure.

Without it, search trees grow too fast.

## 12.4 Hash-based state deduplication
encode_single_snake_plan_hash() at epic4-reachable-frontier-bot.cpp:898-917

The bot hashes:

snake length
head/tail indices
body positions
powerup positions
Then best_depth stores the shallowest depth seen for that state.

Used in:

has_reachable_future_growth()
scan_reachable_frontier()
Why this matters
Without deduplication, the search revisits equivalent states again and again.

This is similar to memoization / visited-state caching.

As a recursive-programmer analogy:

recursion without memoization can repeat subproblems
this hash table is the memoization guard
## 12.5 Isolated single-snake planning
Already discussed, but performance-wise it is huge.

Planning one snake deeply is far cheaper than planning all agents jointly.

This is probably the single biggest complexity reduction in the file.

## 12.6 Cheap heuristics before expensive simulation
Examples:

Manhattan distance: epic4-reachable-frontier-bot.cpp:509-515
walls-only BFS distance: epic4-reachable-frontier-bot.cpp:594-622
center anchor selection: epic4-reachable-frontier-bot.cpp:919-947
These are used to:

rank candidates
break ties
estimate progress cheaply
So the bot saves heavy simulation for the most important reasoning.

## 12.7 Selective strictness
Inside scan_reachable_frontier(), strict follow-through checks are enabled only when:

board area is small
number of initial powerups is small
See epic4-reachable-frontier-bot.cpp:1086-1088.

Why?
Because expensive deeper validation is more affordable on small/simple boards.

This is adaptive search effort.

# 13. One important observation: there is some dead or unused helper code
find_direct_apple_hint() is defined at epic4-reachable-frontier-bot.cpp:864-896, but is not called anywhere in this file.

So conceptually, the active design currently relies on:

reachable frontier scan
apple progress
center goal fallback
not on this direct-hint shortcut.

That is useful to notice when reading large bot files: not every helper is part of the live decision path.

# 14. If you wanted to implement this by hand, what would you build first?
Here is the learning-friendly implementation order.

Phase 1: world model
Implement:

grid as 1D array
Snake with ring-buffer body
parse input into GameState
If this is not solid, everything above it will be unreliable.

Phase 2: deterministic simulation
Implement and test in this order:

apply_movement()
apply_gravity()
resolve_collisions()
simulate()
At this stage, do not do search yet.
Just prove that one-step world evolution is correct.

This is the foundation.

Phase 3: local safety routines
Implement:

is_action_locally_legal()
survives_flood_fill()
count_safe_followups()
Now you can already make a decent greedy bot.

Phase 4: simple BFS planner
Implement a smaller version of scan_reachable_frontier():

only one snake
no enemy contest logic
no persistent goals
no phase budgets
only:
queue
simulate next state
detect apple
remember first action
That will teach you the core idea cleanly.

Phase 5: dedup + time budget
Add:

state hashing
visited/best-depth map
out_of_time()
expansion limit
Now it becomes production-grade enough to survive larger maps.

Phase 6: tactical enemy safety
Add:

“is a stronger nearby snake able to punish this move?”
This gives you much better robustness.

Phase 7: long-term stability
Add:

persistent apple goals
center fallback goals
contest detection
This turns a planner into a more coherent agent.

# 15. The algorithm in one compact pseudocode
If I compress the whole bot into teachable pseudocode:

read turn
rebuild GameState
for each of my snakes:
recover remembered apple goal if still valid
else recover or create center goal
choose search budget
run BFS over simulated future states for this snake only
nodes contain full future board states
reject illegal/dead states
record:
safe reachable apple
progress toward apples
progress toward current goal
stop on time/depth/expansion budget
choose best category:
reachable apple
apple progress
goal progress
fallback legal move
if tactically unsafe against nearby stronger snake:
replace with safer move
output actions
That is the whole architecture.

# 16. What makes this bot “scientifically” interesting
From an algorithm-teaching angle, this bot combines 4 families of ideas:

Simulation

apply real game rules to produce future states
Graph search

BFS over a state graph
Heuristics

center preference, Manhattan distance, follow-up counts
Resource-bounded reasoning

time budgets, depth caps, expansion caps
That combination is what makes it practical.

Pure theory might say:

do exact adversarial search over all snakes
But practical engineering says:

do the strongest affordable approximation inside 72 ms
This file is very much in that second camp.

# 17. Final intuition to remember
If you forget everything else, remember this sentence:
```
This bot searches not for the best next cell, but for the best next future.
```
And it does that by:
```
representing full world states
simulating real rules
exploring only a bounded frontier
preferring futures that still leave room to survive
```