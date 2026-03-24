Below is a teaching-style walkthrough of epic8-team-adversarial-beam-search-bot.cpp.

1. What this bot is, in one sentence
This file is a time-limited adversarial planner for a multi-snake game.

It does not say:

“What is my best next move if enemies do nothing?”
It says:

“Given the full shared board, my team actions, likely enemy replies, and only ~50 ms, what small set of promising futures should I examine, score, prune, and choose from?”
So the core idea is:

build a full world state,
generate candidate joint actions for your team,
predict a few opponent reply styles,
simulate the world forward,
score the resulting state,
keep only a diverse best subset,
repeat a little deeper if time allows,
output only the root move.
2. Big flow overview
Think of the file as having five layers.

Layer 1: Represent the whole world
Main data structures:

Snake
SharedGameState
JointActionPlan
BeamNode
This is the foundation.
The bot always reasons about all snakes together, not one snake isolated.

That matters because this game is simultaneous-move and shared-physics:

both teams move,
collisions happen across teams,
gravity changes board occupancy,
one snake’s move changes safety for another.
So a one-snake planner would lose important interactions.

Layer 2: Simulate one future tick
The function SharedGameState::simulate_joint_actions() is the physics engine.

It runs four stages in order:

apply_movement()
apply_eats()
resolve_collisions()
apply_gravity()
That sequence is the “laws of the universe” for one turn.

Layer 3: Evaluate the resulting position
The bot does not search until terminal end of game.
That would be too expensive.

Instead it uses heuristic scoring in evaluate_shared_state():

alive snakes
team length difference
projected losses
mobility
territory control
apple/powerup ownership
head danger
corridor trap risk
friendly coordination problems
This is the bot’s “taste”.

Layer 4: Search with adversarial replies
The main planner is choose_story85_beam_plan().

Conceptually:

generate friendly root candidates,
for each one, consider opponent reply candidates,
assume the worst opponent reply,
score that,
keep only a small diverse beam,
expand deeper if time remains.
This is not full minimax.
It is a beam-search approximation of minimax.

Very important mental model:

classical minimax: search everything,
this bot: search only a curated small frontier.
That is why it can fit into 50 ms.

Layer 5: Log and output
main():

reads game setup,
reads each turn,
builds SharedGameState,
calls choose_story85_beam_plan(),
converts chosen actions to text commands,
logs diagnostics,
prints result.
3. End-to-end turn flow
For a Java developer, imagine the turn loop as:

parse input into a GameState
call Planner.plan(state)
get back BeamSearchResult
print chosen_plan
That is basically the runtime story.

Step A: initial setup in main()
At startup the bot reads:

player id
world width/height
static map rows
which snake ids belong to me
It computes padded dimensions:

max_len
max_width
max_height
grid_size
This bot uses a padded board.

Why pad the board?
Instead of using just the visible map, it creates a larger array with margins around it.

Why?

Because snakes can fall due to gravity, so positions may temporarily move “below” the original world area.
Padding makes simulation simpler.

This is a classic simulation trick:
use extra space so movement code stays simple.

Step B: build the turn state
Each turn, read_turn_state() constructs a fresh SharedGameState:

starts from static walls,
places current powerups,
parses every snake body,
splits them into my_snakes and opp_snakes,
writes snake cells into grid.
So each turn begins from authoritative input, not from previous simulated memory.

That is good engineering:
it avoids cumulative simulation drift.

Step C: plan with choose_story85_beam_plan()
This is the main planner.

Its broad structure is:

create fallback baseline plan,
detect tactical hotspots,
compute search tuning based on time/complexity,
generate root candidates,
evaluate each against opponent replies,
select a diverse beam,
optionally expand beam deeper,
choose final winner from frontier,
re-evaluate chosen root precisely for logs/output.
That is the high-level algorithm.

4. The most important mental model: what is a “state”?
The heart of understanding this bot is SharedGameState.

SharedGameState contains
grid
my_snakes
opp_snakes
projected_my_losses
projected_opp_losses
So this object is the full world snapshot.

For Java terms, think:

grid = board array
snake lists = living entities
projected loss counters = extra bookkeeping for evaluation
Why is this “shared”?
Because all snakes exist in one board at once.

This is not:

“my snake state”
plus “enemy guess state”
It is one consistent world.

That is the correct abstraction for simultaneous adversarial search.

5. State changes: how one simulated turn actually works
Now the crucial part.

The state transition order is:

St+1=Gravity(Collisions(Eats(Movement(St,actions))))
That exact order matters.

5.1 apply_movement()
This function moves all snakes according to joint actions.

Inputs
current SharedGameState
JointActionPlan
my_actions
opp_actions
What it does
For each snake:

determine action
compute next head cell
detect whether next cell has powerup
write new head into ring buffer
if not eating, move tail forward and clear old tail cell
if eating, increase length and remember consumed powerup position
Important detail: snake body is stored as a ring buffer
Snake has:

body
head_idx
tail_idx
length
This is a classic performance choice.

Instead of shifting the whole body array every move, the code only changes indices.

For a Java analogy:

instead of rebuilding a List<Point> each turn, it uses a circular array.

That is much faster.

Key insight
Movement is applied first, before collision resolution.
That means snakes can move into dangerous cells, and only afterward consequences are computed.

That matches simultaneous-move game logic.

5.2 apply_eats()
This removes powerups that were stepped on by heads.

Why a separate stage?

Because movement decides where heads land; then food/powerups are consumed.

This separation makes the simulation pipeline clearer.

5.3 resolve_collisions()
This is where the game gets adversarial.

The function collects all live snakes into one list, then:

reads all head positions,
destroys snakes whose head:
is out of bounds,
hits wall,
hits snake-occupied cell,
shares cell with another head,
applies shrinking/death rules,
rewrites surviving snake occupancy onto the grid.
Important rule in this bot
If a snake is destroyed:

if length <= 3, it dies completely
else it loses one segment
So collision is not always instant total death.

That is domain-specific game logic.

Why track projected_my_losses and projected_opp_losses?
Because evaluation wants more nuance than just alive/dead.

A state where you lose 1 segment is better than losing 5.

So the simulation keeps loss statistics for heuristic scoring.

5.4 apply_gravity()
This is the most unusual part if you come from ordinary snake games.

What it does
Snakes that are not “grounded” fall downward.

The function repeatedly:

decides which snakes are grounded,
removes airborne snakes from grid,
shifts airborne bodies down by one row,
kills snakes that fall completely out,
redraws surviving airborne bodies,
repeats until nothing falls.
What does “grounded” mean?
A cell is grounded if the cell below is:

wall,
powerup,
or occupied by a grounded snake.
So grounding propagates.

This is a little physics solver.

Mental model
Think of snakes as hanging chains in a cave.
If nothing supports them from below, they fall.

That is why a simple “snake pathfinder” is not enough.
This bot really is a board physics simulator.

6. How candidate actions are represented
JointActionPlan
This is simple:

my_actions
opp_actions
For example:

my snake 0: UP
my snake 1: RIGHT
opponent snake 0: LEFT
opponent snake 1: DOWN
This is a joint move for the whole board.

Because the game is multi-agent, single action is not enough.
The planner reasons in joint action space.

7. How the bot evaluates positions
The function evaluate_shared_state() is the heuristic brain.

It produces EvaluatorBreakdown with components like:

terminal_score
length_delta
loss_risk_penalty
mobility_delta
territory_delta
apple_ownership_delta
head_danger_penalty
corridor_trap_penalty
coordination_penalty
total_score
7.1 Terminal score
If one side has no live snakes, score becomes huge positive/negative.

Also if there are no powerups left, it treats length comparison as an endgame criterion.

This is standard heuristic design:
terminal outcomes dominate all softer heuristics.

7.2 Length and losses
more friendly total length is good,
more projected enemy losses is good,
more projected friendly losses is bad.
This gives the bot preference for trades that are materially favorable.

7.3 Territory
The bot computes territory using analyze_territory_and_apples().

Inside that, it runs multi_source_bfs_distances() separately for:

my snake heads
opponent heads
This gives a distance map from each side to all reachable cells.

Then each traversable cell is classified as:

mine,
opponent’s,
contested.
This is a simple and effective concept:

“Whose side can reach this area first?”

That is a good approximation of future space control.

For someone new to algorithms:
this is a practical use of BFS, not theory for theory’s sake.

Intuition
If your heads can reach more cells sooner, you likely have better survival and expansion options.

7.4 Apple/powerup ownership
Same distance-map logic, but only for powerup cells.

This estimates who is favored to collect future growth.

7.5 Mobility
count_team_mobility() sums legal actions for each live snake.

A position with more legal moves is safer and richer.

This is a common heuristic in board-game AI:
number of good continuations matters.

7.6 Head danger
estimate_head_danger_penalty() punishes your heads being near enemy heads of equal or greater length.

Why?

Because head-on interactions are dangerous if opponent is not shorter.

This is tactical threat modeling.

7.7 Corridor trap
estimate_corridor_trap_penalty() punishes snakes with only 1 or 2 legal actions.

This is basically:
“Are we getting boxed in?”

Very practical heuristic.

7.8 Coordination penalty
estimate_coordination_penalty() punishes friendly heads being too close to each other.

Why?

Because teammates can block each other.

This is especially important in multi-snake planning.

8. Hotspots: when the bot decides “this position is tactically sharp”
analyze_hotspots() produces HotspotSummary.

It looks for patterns like:

head collision risk
contested apple race
corridor pressure
constrained growth
beheading swing
This is a danger/intensity detector.

Why does hotspot detection exist?
Because not every position deserves equal search effort.

If the state is calm:

shallow search is enough.
If the state is explosive:

deeper and more tactical branching is worthwhile.
This is an important optimization idea:
spend time where local consequences are sharp.

9. Opponent modeling: how the bot imagines enemy replies
This file does not assume enemies are perfect minimax agents.

Instead it uses a policy abstraction.

Enemy reply policies
EnemyReplyPolicy includes:

DefaultContinue
SafeSpace
AppleRace
HeadPressure
For each enemy snake, helper methods produce plausible actions:

choose_enemy_safe_space_reply()
choose_enemy_apple_race_reply()
choose_enemy_head_pressure_reply()
So the bot says:

“I will not enumerate every possible enemy move. I will enumerate a few meaningful styles of enemy move.”

That is a very practical AI design.

Why this matters
Full joint branching is explosive.

If you have 3 friendly snakes and 3 enemy snakes, with roughly 4 moves each:

4
3
×
4
3
=
4096
4 
3
 ×4 
3
 =4096
for just one ply of joint actions.

That becomes too expensive very fast.

So opponent policy abstraction is a branching-control technique.

10. Root candidates: how the bot generates its own high-level plans
The function generate_team_root_candidates() creates a set of structured plans.

These plans are not random.
They are named strategic families.

Examples:

SAFE_SPREAD
DEFENSIVE_RECOVERY
SPLIT_HARVEST
ANCHOR_AND_FORAGE
CONTEST_LEFT_CLUSTER
CONTEST_RIGHT_CLUSTER
CENTER_PRESSURE
This is a good hybrid design:

not raw brute force,
not purely hand-scripted,
but hand-crafted families combined with simulation.
Why named families?
Because they inject domain knowledge.

Examples:

split snakes across apples,
keep one near center,
pressure enemy with closest striker,
recover safely when cramped.
This reduces dumb branching and improves search quality.

11. Coordinating multiple friendly snakes
A subtle but important method is coordinate_joint_actions().

It takes:

per-snake action preferences
snake priority order
Then chooses actions while avoiding immediate friendly head conflicts.

This is a greedy coordination pass.

Why needed?
Without it, two friendly snakes might both prefer the same destination.

This would create bad self-collisions in root candidates.

So the planner first creates preferences, then resolves them into a coherent joint plan.

That is very useful for multi-agent systems.

12. Beam search: the central search algorithm
Now the core concept.

What is beam search?
Beam search is like breadth-first search with memory loss.

At each depth:

expand many children,
score them,
keep only top k,
discard the rest forever.
k is the beam width.

This bot uses beam search because full search is too expensive.

How this file uses beam search
The main function is choose_story85_beam_plan().

Phase 1: generate root candidates
It creates candidate friendly plans.

Phase 2: adversarial evaluation
For each friendly plan, evaluate_plan_node():

generates opponent reply candidates,
simulates each reply,
evaluates resulting states,
chooses the worst reply for us.
This is the minimax flavor:

plan value
≈
min
⁡
opponent replies
heuristic score
plan value≈ 
opponent replies
min
​
 heuristic score
So each root plan is judged pessimistically.

Phase 3: beam selection
It does not keep all root nodes.
It calls select_diverse_beam().

Phase 4: expand deeper
For each retained beam node, it generates follow-up friendly candidates and evaluates them similarly.

Phase 5: choose best frontier node
After depth 1, 2, maybe 3, the best frontier node decides the root move.

Only the root actions are output.

13. Why “diverse” beam instead of just top scores?
This is one of the best parts of the file.

select_diverse_beam() is not just “sort by score and cut”.

It does more:

merges exact duplicate states by full hash,
tracks novelty signatures,
enforces bucket quotas,
enforces family quotas,
ensures different root signatures survive,
penalizes overcrowded novelty groups.
This is there to avoid premature convergence.

Analogy
Suppose top 10 states are all slight variations of “rush left apple”.

If you keep only raw score top 10, you may lose:

a survival line,
a space-control line,
a tactical attack line.
Then if the evaluator is slightly wrong, the search collapses.

Diversity retention makes the search more robust.

That is a very mature search design.

14. State hashing and novelty
There are two important hashes.

14.1 full_state_hash()
This is exact-state identity.

It includes:

powerup positions
each snake’s identity/owner/alive flag
length
head/tail indices
ordered body positions
projected losses
Purpose:

detect exact duplicates,
merge same resulting state from different action sequences.
This is classic transposition-style dedup.

14.2 novelty_signature
This is not exact identity.
It is a coarser grouping.

It includes quantized traits like:

beam bucket
root family
alive masks
rough head zones
apple bands
territory bands
Purpose:

keep strategically different states,
even if exact scores are similar.
This is more like “state category” than exact state.

15. Beam buckets: strategic categories
BeamBucket groups states into categories:

SURVIVAL
GROWTH
SPACE
CONTEST
ATTACK_OR_PRESSURE
ESCAPE_OR_RECOVERY
Classification is done by classify_beam_bucket().

This gives the beam selection routine a way to preserve strategic variety.

For example:

some nodes should survive because they are best survival states,
some should survive because they are best growth states.
This is a strong design pattern:
rank within categories, not only globally.

16. Tactical mode and lexicographic scoring
In sharp situations, the file activates tactical analysis.

Relevant pieces:

analyze_tactical_branch()
generate_tactical_followup_candidates()
generate_tactical_opponent_reply_plans()
TacticalLexicoScore
What is lexicographic scoring?
Instead of adding everything into one number, compare by priority list:

keep more of my snakes alive
kill more enemy snakes
preserve my minimum snake length
preserve my total length
reduce enemy total length
future growth
mobility
territory
apples
positional quality
scalar fallback
This is implemented in is_better_tactical_lexico_score().

Why use lexicographic comparison?
In tactical combat, some things are not tradeable.

For example:

losing one whole snake is not something you want to offset by “but I got +20 territory”.
Lexicographic order expresses hard priorities better than one weighted sum.

This is a sophisticated idea, but intuitive in practice.

17. Time management and search tuning
The file is budgeted for about 50 ms:

TURN_BUDGET_MS = 50
TURN_RESERVE_MS = 5
Helpers:

elapsed_turn_ms()
time_remaining_ms()
out_of_time()
out_of_time_with_guard()
compute_search_tuning()
This method dynamically adjusts:

root candidate cap
root beam cap
frontier beam cap
reply cap
tactical root cap
followup cap
max depth
based on:

hotspot intensity
number of live snakes
board area
branching pressure
remaining time
This is critical.

A fixed search size would either:

be too slow in hard positions,
or too shallow in easy positions.
Adaptive tuning is what makes the planner practical.

18. Recursion in this file
You said recursion is familiar. Good: several parts are recursive combinators.

Examples:

generate_opponent_reply_plans_dfs()
generate_tactical_followup_candidates_dfs()
generate_tactical_opponent_reply_plans_dfs()
These are essentially building Cartesian products with pruning.

Mental model
For each participant snake:

choose one action option,
recurse to next snake,
stop when all assigned,
emit one complete joint plan.
That is a classic recursive enumeration pattern.

In Java 6 terms, imagine:

mutable arrays passed down recursion,
backtracking after each choice,
result list filled at leaves.
Very standard and good to imitate.

19. The most important helper methods
Now the file’s methods by role.

A. Low-level move and geometry helpers
infer_previous_action()
opposite_action()
is_backward_action()
action_to_string()
manhattan_dist_pos()
next_position_for_action()
These are your “coordinate math” utilities.

If implementing by hand, write these first and test them heavily.

B. Legality helpers
is_action_locally_legal()
first_legal_action_shared()
legal_actions_for_snake()
These define short-horizon move validity.

Note: “locally legal” is not the same as “globally safe”.

A move can be locally legal but still bad after simultaneous collisions or gravity.

That distinction is essential.

C. Spatial heuristics
nearest_powerup_distance()
nearest_enemy_head_distance()
nearest_my_head_distance()
local_open_neighbor_count()
These are cheap local estimators used in candidate generation.

They are deliberately lightweight.

D. Territory analysis
multi_source_bfs_distances()
analyze_territory_and_apples()
This is your board-control layer.

If you want deep understanding, re-implement these separately first.

E. Friendly action policy helpers
choose_action_toward_target()
choose_safe_spread_action()
choose_escape_action()
choose_pressure_action()
choose_center_anchor_action()
These are simple strategy-specific action pickers.

They are not the final planner by themselves.
They are generators for plausible candidates.

That distinction is important.

F. Opponent policy helpers
choose_enemy_safe_space_reply()
choose_enemy_apple_race_reply()
choose_enemy_head_pressure_reply()
generate_enemy_reply_choices()
These give the adversarial search a compact enemy action set.

G. Search node construction
make_beam_node()
evaluate_plan_node()
evaluate_plan_node() is one of the key methods to understand.

It says:

get possible opponent replies,
simulate each,
keep the worst for us,
convert resulting state into a BeamNode.
So this method is a mini adversarial evaluator.

H. Beam maintenance
select_diverse_beam()
expand_beam_frontier()
These are the beam engine.

If you wanted the algorithm skeleton, these are central.

20. Why this is not brute force
This is worth emphasizing.

The file controls explosion using several independent tricks:

1. Candidate families instead of all actions
Not every joint action is generated.

2. Opponent policy abstraction
Not every enemy joint action is generated.

3. Local tactical branching only in hotspots
More detail only when useful.

4. Beam width limits
Most nodes are pruned immediately.

5. Exact-state merging by hash
Duplicate futures are collapsed.

6. Novelty grouping
Search diversity is preserved.

7. Adaptive search tuning
Caps shrink when time is tight.

8. Lightweight heuristics
Distance and openness are cheap to compute.

This is the real lesson:
good game AI is mostly branching control plus good state evaluation.

21. How to think about this as a Java 6 developer
If translating mentally into Java 6:

struct-like C++ objects here are basically POJOs
vector<T> is mostly like ArrayList<T>
unordered_map is HashMap
unordered_set is HashSet
static helper functions are utility methods in one class
recursion patterns are standard backtracking methods
beam search frontier is a List<BeamNode>
sorting with lambdas would become custom Comparators
So conceptually, none of this depends on fancy modern C++ ideas.
The algorithm is the important part.

22. If implementing by hand, what order should you build it in?
A strong implementation order is:

Stage 1: board and snake representation
Implement:

Snake
SharedGameState
grid encoding
move math
Stage 2: deterministic simulation
Implement and test:

apply_movement()
apply_eats()
resolve_collisions()
apply_gravity()
Do this before any search.

If simulation is wrong, search will only amplify wrongness.

Stage 3: legality and simple baseline
Implement:

legal action enumeration
previous-action continuation
first legal fallback
At this point you have a runnable basic bot.

Stage 4: heuristic evaluation
Implement:

length score
mobility
BFS territory
apple ownership
danger penalties
Now you have a scoring engine.

Stage 5: one-ply adversarial evaluation
Implement:

generate candidate friendly moves
generate enemy reply styles
choose worst reply
pick best friendly root
Now you have “minimax-lite”.

Stage 6: beam search
Implement:

BeamNode
beam frontier
expansion
pruning
Stage 7: diversity and optimization
Add:

exact hashes
novelty signatures
bucket quotas
adaptive caps
tactical hotspot branching
That is the mature version.

23. The algorithm in plain human language
If reduced to plain words, the bot says:

I will not try everything.
I will generate a handful of meaningful team plans.
For each plan, I will imagine a handful of plausible enemy replies.
I will pessimistically judge each plan by its worst reply.
I will keep a diverse set of promising futures.
If time allows, I will continue one or two layers deeper.
Then I will play the root move of the best surviving future.

That is the essence.

24. What is scientifically elegant about this file?
Three things.

1. It separates physics from strategy
simulate_joint_actions() is independent from evaluation taste.

That is good architecture.

2. It separates candidate generation from final selection
Policy heuristics propose actions; search judges them.

That is stronger than pure scripting.

3. It accepts bounded rationality
The bot does not pretend to solve the full game tree.
It uses approximations consciously and systematically.

That is how most practical real-time game AI works.

25. The single most important concept to master first
If learning this deeply, start with:

“A search algorithm is only as good as its state transition.”
In this file, the most important deep understanding is:

how state is represented,
how a joint action changes it,
why the order movement → eat → collide → gravity matters.
Once that feels natural, the rest becomes layers on top:

evaluation,
opponent modeling,
beam pruning,
optimizations.
26. Short summary of the architecture
Big flow
parse turn
build shared state
generate candidate team plans
predict enemy replies
simulate
evaluate
keep diverse beam
expand deeper if time allows
output root actions
State changes
move heads/tails
consume powerups
resolve collisions and losses
apply gravity repeatedly until stable
Methods and optimization routines
BFS territory analysis
hotspot detection
policy-based enemy modeling
tactical lexicographic comparison
exact-state hashing
novelty signatures
bucket/family quotas
adaptive time-aware search tuning