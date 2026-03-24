# Epic 8: 8-Snake Team Adversarial Beam Search

**Goal:** Build a new bot architecture that can reason about up to 8 simultaneous snakebots under a strict real-time turn budget, without collapsing back into isolated single-snake pathing. The bot must preserve strategically different futures, explicitly model nearby enemy replies, coordinate multiple friendly snakes, and avoid the classic beam-search failure where the winning line is pruned because it looks temporarily worse than a greedy line.

**Primary implementation target:** bot-development/bots/epic8-team-adversarial-beam-search-bot.cpp

**Reference implementations to mine, not blindly extend:**
- bot-development/bots/epic7-coop-reachable-frontier-bot.cpp
- bot-development/bots/epic7-lexicographic-survival-frontier-bot.cpp
- bot-development/bots/epic4-reachable-frontier-bot.cpp

**Rules and engine sources:**
- snakebyte-rules.txt
- basic-ideas.txt
- WinterChallenge2026-Exotec/src/main/java/com/codingame/game/Game.java
- WinterChallenge2026-Exotec/src/main/java/com/codingame/game/Referee.java

---

## Why Epic 8 exists

The current frontier family is competent at:
- simulation fidelity
- apple safety checks
- low-escape growth rejection
- center-goal fallback
- teammate target reservation
- solo or lightly cooperative pathing

But it is still structurally a **single-snake planner with safety patches**.

That architecture is not sufficient for the actual end-state problem:
- up to 8 total snakes on the board
- simultaneous actions
- local enemy punish windows
- contested apples
- tie-break penalties via `losses`
- long tactical sequences where a temporarily bad move wins later

The current planner often answers:
> “What is the safest reachable thing for this one snake if most other snakes mostly continue forward?”

Epic 8 must instead answer:
> “What small set of team futures should survive deeper adversarial search because each represents a genuinely different strategic plan?”

This epic exists specifically to stop mid-plan drift.

The implementation must not degrade into:
- another isolated single-snake search with more heuristics
- a full minimax fantasy that times out
- a greedy apple bot wearing beam-search vocabulary
- a monolithic refactor without regression gates

---

## Non-negotiable design truths

### 1. Real target budget is 50 ms
The local referee currently grants ~73 ms, but the rules file documents 50 ms. Epic 8 must be designed to survive **50 ms**, and treat any local extra time as safety margin, not entitlement.

### 2. Full joint search is impossible
With 8 snakes and about 3 legal moves each, one naïve simultaneous ply is roughly $3^8 = 6561$ joint actions. Full-width depth search is not the solution.

### 3. The answer is selective adversarial search
The correct design is:
- coarse global evaluation
- hotspot detection
- opponent reply abstraction
- diverse beam retention
- tactical local branching only where it matters

### 4. The main failure to defeat is not “too shallow”
The main failure to defeat is:
> **the actually best line is pruned early because the beam only keeps locally attractive states**

So beam diversity is a first-class requirement, not a nice-to-have.

### 5. Team coordination is part of search, not a post-process
The new bot must stop treating each friendly snake as an almost independent router. Team allocation, team safety, and team pressure must affect root action generation and state evaluation.

---

## Explicit success definition

Epic 8 is successful only when all of the following are true:

1. The bot compiles as a new executable from a new source file.
2. The bot keeps a hard internal safety margin under the real-time budget.
3. The bot no longer depends on isolated single-snake planning for its main move choice.
4. The bot evaluates **team states**, not only per-snake route fragments.
5. The bot preserves multiple strategic futures per root action category.
6. The bot simulates nearby enemy punish replies instead of assuming passive opponents.
7. The bot uses a global board-control evaluator including territory and apple ownership.
8. The bot passes a defined regression ladder covering:
   - solo survival
   - corridor escape
   - contested apples
   - coop allocation
   - enemy foresight
   - multi-snake pressure
9. Logs clearly explain why a move won the beam.
10. The implementation remains readable and evolvable.

---

## Architecture contract

Epic 8 must implement **five layers**, in this exact conceptual order.

### Layer A: High-fidelity shared simulation core
A single fast `GameState` simulation model that matches the real engine order:
1. move
2. eat
3. behead
4. fall

This layer must support:
- all friendly snakes
- all enemy snakes
- simultaneous actions
- tie-sensitive outcomes
- gravity and off-world padded simulation

This layer is allowed to reuse working ideas from Epic 7, but the final search state must remain a **shared world state**, not an isolated single-snake rewrite.

### Layer B: Coarse global evaluator
A fast board evaluator run on every candidate state, built around:
- multi-source territory ownership
- apple ownership / contest classification
- mobility and safe replies
- chamber / reachable region size
- head-on danger
- tie-break loss awareness
- team total length pressure

This is the main heuristic layer.

### Layer C: Opponent policy abstraction
Enemy snakes must not be modeled by “continue straight only”.
Instead, each nearby enemy must be representable by a small set of plausible replies.

### Layer D: Diverse beam search
The beam must retain multiple line categories and multiple root-action families. Global top-K alone is forbidden.

### Layer E: Tactical local branching
Only in hotspot regions do we allow more explicit adversarial branching over several snakes at once.

---

## Primary implementation file plan

### New file
Create a new file:
- `bot-development/bots/epic8-team-adversarial-beam-search-bot.cpp`

### Do not directly mutate the Epic 7 active bot as the main work surface
Epic 7 remains a stable baseline. Epic 8 is new architecture work and must live separately until it proves itself.

### Optional supporting files
If needed, add small supporting headers or helper files, but prefer keeping the implementation compact and contest-portable.

Allowed support docs and reports:
- `prompts/epic-8-team-adversarial-beam-search.md`
- `prompts/epic-8-results.md`
- `bot-development/test-maps/beam-search/`
- `bot-development/test-maps/adversarial/`
- `bot-development/test-maps/multi-snake-adversarial/`

---

## Core stories

# Story 8.0: Freeze the architectural direction before coding
**As a** maintainer,
**I want** the architecture pinned in writing before implementation starts,
**So that** the work does not drift back into isolated routing plus patches.

### Acceptance criteria
- This epic file is the contract.
- The implementation must explicitly state in comments the five-layer architecture above.
- Any code path that chooses the final move through isolated single-snake planning alone is considered a failure of scope.
- If an isolated planner remains anywhere, it may only be used as a fallback or a feature generator, never as the main decision engine.

### Implementation prompt
You are starting with empty agent context. Read this file first, then read `snakebyte-rules.txt`, `basic-ideas.txt`, `WinterChallenge2026-Exotec/src/main/java/com/codingame/game/Game.java`, `WinterChallenge2026-Exotec/src/main/java/com/codingame/game/Referee.java`, `bot-development/bots/epic7-coop-reachable-frontier-bot.cpp`, and `bot-development/bots/epic7-lexicographic-survival-frontier-bot.cpp`. Do not implement gameplay changes yet. Your task is to convert this epic into implementation guardrails inside a new source file `bot-development/bots/epic8-team-adversarial-beam-search-bot.cpp`: add a top-of-file architecture comment that names the five required layers, add placeholder structs/functions matching the intended architecture, add TODO markers matching the later stories, and ensure no main decision path uses isolated single-snake planning as the primary planner. Keep the file compiling if possible, but correctness of architecture skeleton is more important than behavior in this story. Do not modify Epic 7 files except for reading them.

---

# Story 8.1: Build a shared multi-snake search state
**As an** adversarial search engine,
**I want** a fast shared state representation for all snakes,
**So that** beam nodes represent real board futures rather than isolated approximations.

### Required design
Implement a state structure containing at minimum:
- padded grid
- static walls
- current powerup set
- alive flags for all snakes
- body representation for all snakes
- head and tail indices
- current facing / inferred previous direction
- player ownership of each snake
- aggregate team lengths
- optional cached metadata per node

### Mandatory constraints
- No per-node heap-heavy object graph.
- No string parsing inside search.
- No search-time filesystem or logging-side overhead except guarded debug summaries.
- All hot-path state transitions must be copy-light and predictable.

### Acceptance criteria
- Shared `simulate_joint_actions()` exists and matches engine semantics.
- Root beam nodes store the full team state.
- A node hash exists for the full shared state, not only a single snake.
- The implementation documents what information is included in the hash and why.

### Implementation prompt
You are starting with empty agent context. Read this epic, then inspect the current simulator in `bot-development/bots/epic7-coop-reachable-frontier-bot.cpp` and the engine truth in `WinterChallenge2026-Exotec/src/main/java/com/codingame/game/Game.java`. Implement Story 8.1 in a new file `bot-development/bots/epic8-team-adversarial-beam-search-bot.cpp`. Build a shared multi-snake state for all live snakes from both teams, with padded-grid support, simultaneous movement, eating, beheading, and falling. Reuse good ideas from Epic 7, but do not isolate one snake for the main state transition logic. Add `simulate_joint_actions()` and a full shared-state hash. Document in comments exactly what the hash contains and why. Preserve performance discipline: no string parsing in hot paths, no heavy per-node allocations, no filesystem work in search. If the file does not yet compile, continue until it does or until only clearly marked stubs remain.

---

# Story 8.2: Create a fast global board evaluator
**As a** beam search,
**I want** a cheap but expressive evaluator,
**So that** the beam ranks futures by more than nearest-apple greed.

### Required evaluator components
The evaluator must compute all of the following per node:

1. **Terminal outcome score**
   - immediate win / loss / forced collapse priority
   - large magnitude

2. **Team length delta**
   - my live total body parts minus opponent live total body parts

3. **Tie-break pressure**
   - track estimated exposure to extra beheads and unnecessary losses
   - beheading for no gain must be penalized

4. **Mobility score**
   - number of safe next actions across friendly snakes
   - optionally weighted by importance / hotspot status

5. **Territory / Voronoi score**
   - multi-source BFS from all live heads
   - classify cells as mine / theirs / contested / unreachable
   - produce territory counts by team

6. **Apple ownership score**
   - count powerups as mine / theirs / contested
   - score from `owned_apples_mine - owned_apples_theirs`

7. **Reachable chamber score**
   - approximate size of safely reachable space after accounting for gravity and obstructions

8. **Head danger score**
   - penalize cells that invite equal-or-larger enemy head collisions

9. **Escape/runway score**
   - detect futures where growth strands a snake in a dead corridor

10. **Coordination penalty**
   - penalize friendly self-interference, duplicated target claims, or snakes crossing into each other’s natural basin without need

### Acceptance criteria
- Evaluator runs on the shared node state.
- Evaluator does not require a deep search to be meaningful.
- Multi-source territory and apple ownership are implemented once and reused everywhere possible.
- The evaluator exposes a structured debug breakdown, not just one opaque number.

### Implementation prompt
You are starting with empty agent context. Read this epic plus `snakebyte-rules.txt`, `basic-ideas.txt`, and the current Epic 7 bot files. Implement Story 8.2 on top of the new Epic 8 shared state. Add a fast global evaluator that operates on the full shared node state and computes at minimum: terminal outcome, team length delta, tie-break/loss pressure, mobility, multi-source territory ownership, apple ownership, chamber/reachable space, head danger, runway/escape quality, and friendly coordination penalty. Implement multi-source BFS once and reuse it for territory and apple ownership. Expose the evaluator as a structured breakdown object plus a final combined score. Do not reduce this to nearest-apple scoring. Keep the evaluator cheap enough for beam use.

---

# Story 8.3: Add enemy reply abstraction
**As a** fast adversarial planner,
**I want** a bounded set of plausible enemy replies,
**So that** search handles punishment without exploding combinatorially.

### Required reply policies
For each enemy snake, generate up to these policy moves when relevant:
- `DEFAULT_CONTINUE`: keep current direction if legal
- `SAFE_SPACE`: move that maximizes local survival / followups
- `APPLE_RACE`: move that improves distance toward a contested or owned-nearby apple
- `HEAD_PRESSURE`: move that threatens a nearby friendly head if legal and advantageous

Not every enemy always gets all four replies.

### Relevance rules
A snake is a **hot enemy** if at least one of these is true:
- Manhattan / simulated proximity to any friendly head is small
- competing for the same apple basin
- in the same corridor, chamber, or choke zone
- can create a head-on collision window within 1–2 turns

A non-hot enemy may use only one default policy move.

### Acceptance criteria
- Enemy move generation is context-sensitive.
- Distant enemies do not consume full branching budget.
- Nearby enemies can produce more than one plausible reply.
- Logs can show which reply policies were active for a node.

### Implementation prompt
You are starting with empty agent context. Read this epic and the Epic 8 implementation file. Implement Story 8.3 by adding enemy reply abstraction. Replace any assumption that enemies simply continue straight. Create bounded enemy reply policies such as `DEFAULT_CONTINUE`, `SAFE_SPACE`, `APPLE_RACE`, and `HEAD_PRESSURE`. Add relevance rules so only nearby or tactically relevant enemies get multiple reply policies, while distant enemies collapse to a default policy. The output of this story should be code that can enumerate plausible enemy replies per state and log which policy classes were used. Keep branching bounded and explicitly document the pruning rationale.

---

# Story 8.4: Detect hotspots before deep branching
**As a** performance-constrained search,
**I want** to branch deeply only in tactically relevant regions,
**So that** 8-snake search remains feasible.

### Hotspot signals
A hotspot must be detected if any of these apply:
- friendly and enemy heads can collide soon
- several snakes are racing the same apple or small cluster
- a snake is about to enter or exit a corridor or shaft
- a chamber is small enough that one move changes survivability sharply
- a friendly snake is one move from growth in a constrained area
- strong tie-break swing is possible via deliberate beheading

### Acceptance criteria
- The code contains an explicit hotspot detector, not scattered implicit checks.
- Beam branching width increases in hotspot states and decreases elsewhere.
- A node can be annotated with hotspot reasons for debugging.

### Implementation prompt
You are starting with empty agent context. Read this epic and the current Epic 8 file. Implement Story 8.4 by creating an explicit hotspot detector. A hotspot should trigger when heads may collide soon, apples are actively contested, corridors or shafts sharply change survival, a constrained growth event is near, or a tie-break swing via beheading is plausible. Add a hotspot summary object to search nodes, and use it to control later branch width decisions. Do not scatter tactical checks across unrelated functions; centralize them in one hotspot-analysis module or function cluster. Add debug fields so logs can show why a node was classified as hot.

---

# Story 8.5: Implement a diverse beam, not a global top-K beam
**As a** robust beam search,
**I want** strategic diversity quotas,
**So that** promising long-term lines survive even when shallow heuristics dislike them.

### Mandatory beam buckets
At minimum, beam states must be bucketed by intent:
- `SURVIVAL`
- `GROWTH`
- `SPACE`
- `CONTEST`
- `ATTACK_OR_PRESSURE`
- `ESCAPE_OR_RECOVERY`

### Mandatory retention rules
For each depth:
1. keep a quota per bucket
2. keep a quota per root action family
3. keep a novelty filter so near-duplicate states do not fill the beam
4. keep at least one best line for each currently legal root move unless it is proven hopeless

### Forbidden behavior
- no single global sort followed by top-K only
- no allowing one greedy root move to consume the full beam at shallow depth
- no collapsing all similar-looking states without preserving line intent

### Acceptance criteria
- Beam code explicitly stores bucket type and root action ancestry.
- The implementation can explain why a state survived: score, bucket, novelty, root quota.
- There is a test or log proof showing a temporarily weaker line survives due to bucket or root quota.

### Implementation prompt
You are starting with empty agent context. Read this epic and the current Epic 8 file. Implement Story 8.5 by building the real diverse beam. Do not use a single global top-K. Add explicit beam buckets: `SURVIVAL`, `GROWTH`, `SPACE`, `CONTEST`, `ATTACK_OR_PRESSURE`, and `ESCAPE_OR_RECOVERY`. Add per-bucket quotas, per-root-family quotas, and explicit retention of at least one viable line per legal root move family. Every retained node must carry bucket type and root ancestry. Add enough logging or a deterministic test case to prove a temporarily weaker line survives because of diversity rules rather than raw scalar score.

---

# Story 8.6: Implement novelty / duplicate suppression
**As a** bounded beam,
**I want** duplicate-state pressure reduced,
**So that** beam slots are not wasted on nearly identical futures.

### Required mechanism
Each node must have:
- full shared-state hash
- coarse novelty signature

The novelty signature may include:
- team head positions
- alive masks
- apple ownership pattern
- coarse territory pattern
- bucket type
- root action family

### Acceptance criteria
- Exact transpositions can be merged or pruned.
- Near-duplicates are penalized or capped.
- Beam occupancy by one pattern family is visibly reduced in logs.

### Implementation prompt
You are starting with empty agent context. Read this epic and the current Epic 8 beam code. Implement Story 8.6 by adding duplicate suppression at two levels: exact shared-state transpositions and coarse novelty signatures. Build a full-state hash and a cheaper novelty signature based on head locations, alive masks, apple ownership pattern, coarse territory pattern, bucket type, and root family. Use these to merge, cap, or penalize duplicate futures so beam slots are not consumed by near-identical lines. Update logs so beam occupancy by novelty family can be inspected.

---

# Story 8.7: Root action generation must be team-aware
**As a** multi-snake team bot,
**I want** root candidates that already encode team coordination,
**So that** the beam does not waste time on self-sabotaging joint actions.

### Required root generation principles
- Generate joint friendly actions from a staged coordinator, not blind Cartesian products.
- Earlier root generation must prefer:
  - distinct apple basin claims
  - chamber coverage
  - collision avoidance between teammates
  - role diversity if natural
- Root actions should include categories such as:
  - safe spread
  - double contest
  - one grow / one anchor
  - one pressure / others stabilize

### Acceptance criteria
- Friendly joint actions are built from structured candidate sets.
- Full Cartesian explosion over all friendly snakes is forbidden in the default path.
- Root actions include multiple team intent profiles, not just the locally best move per snake.

### Implementation prompt
You are starting with empty agent context. Read this epic and the current Epic 8 file. Implement Story 8.7 by replacing independent per-snake root choice with team-aware root generation. Build structured friendly joint root families such as `SPLIT_HARVEST`, `ANCHOR_AND_FORAGE`, `SAFE_SPREAD`, `CENTER_PRESSURE`, and `DEFENSIVE_RECOVERY`. Root generation must prefer basin splitting, chamber coverage, teammate collision avoidance, and natural role diversity. Do not generate the default root set by full Cartesian product over all friendly actions. The output should be a bounded list of meaningful joint root candidates with family labels.

---

# Story 8.8: Add tactical local branching for contested situations
**As a** beam search,
**I want** more explicit adversarial branching near tactical danger,
**So that** forced collisions and corridor fights are not hand-waved away.

### Required behavior
When a hotspot exists:
- nearby enemy reply sets may expand
- nearby friendly alternatives may expand
- deeper search should continue for those localized tactical states
- non-hot snakes may be frozen to default policies to preserve budget

### Acceptance criteria
- Tactical local branching is clearly separate from the coarse planner.
- The implementation can branch more deeply in a 2–4 snake hotspot while abstracting the rest.
- Logs must identify when a tactical extension occurred.

### Implementation prompt
You are starting with empty agent context. Read this epic and the current Epic 8 file. Implement Story 8.8 by adding tactical local branching. When a hotspot exists, branch more explicitly over the few nearby friendly and enemy snakes that matter, while freezing or abstracting distant snakes to bounded default policies. Keep this logic clearly separate from the global coarse planner. The goal is not full joint minimax; it is a localized adversarial extension that captures forced collisions, corridor fights, and short tactical races. Add logs that show when tactical extension activated and which snakes participated.

---

# Story 8.9: Use lexicographic scoring where scalar scoring lies
**As a** search ranking system,
**I want** certain decisions compared lexicographically instead of by one flat number,
**So that** survival-critical distinctions are never drowned by soft heuristic gains.

### Mandatory lexicographic priorities in tactical search
At minimum, for deep tactical comparison:
1. stay alive longer
2. avoid catastrophic length collapse
3. preserve future growth potential
4. preserve followups / mobility
5. improve territory / apple ownership
6. improve positional quality

### Acceptance criteria
- Deep tactical ranking is not a single naïve additive score.
- Survival-first order is documented and testable.
- Existing good ideas from Epic 7 lexicographic survival are reused where appropriate.

### Implementation prompt
You are starting with empty agent context. Read this epic, the Epic 8 file, and `bot-development/bots/epic7-lexicographic-survival-frontier-bot.cpp`. Implement Story 8.9 by introducing lexicographic tactical scoring where scalar sums are unsafe. Preserve survival-first ordering: survive longer, avoid catastrophic shrinkage, preserve future growth, preserve followups, then improve territory and positional quality. Reuse the strongest ideas from Epic 7 lexicographic survival, but adapt them to shared-state tactical comparisons. Add clear code comments documenting the comparison order and why it exists.

---

# Story 8.10: Iterative deepening and hard time guards
**As a** real-time bot,
**I want** stable anytime behavior,
**So that** the bot always returns a legal move before timeout.

### Required behavior
- Complete shallower passes first.
- Store best completed root result.
- Stop with reserve time still intact.
- Search depth and beam width adapt to:
  - number of live snakes
  - board area
  - hotspot count
  - branching pressure
  - remaining turn budget

### Mandatory timing policy
- Design around 50 ms
- Hold back reserve margin for output and jitter
- Never spend the entire documented budget

### Acceptance criteria
- The bot can report the deepest fully completed pass.
- Logs show time spent per depth / phase.
- No timeout-based defeats on the validation ladder.

### Implementation prompt
You are starting with empty agent context. Read this epic, `snakebyte-rules.txt`, `WinterChallenge2026-Exotec/src/main/java/com/codingame/game/Referee.java`, and the current Epic 8 file. Implement Story 8.10 by adding iterative deepening and hard time guards designed around 50 ms, not 73 ms. The search must complete shallow passes first, remember the best completed root result, and stop with reserve margin for output jitter. Make beam width and tactical depth adaptive to live snake count, board area, hotspot count, and remaining time. Add structured timing logs per phase and depth. The implementation must favor safe completion over risky extra depth.

---

# Story 8.11: Diagnostics that explain beam decisions
**As a** developer,
**I want** beam and evaluator decisions to be inspectable,
**So that** future iteration does not become guesswork.

### Mandatory log fields
For root and winner states, log at least:
- turn number
- elapsed ms
- live snake counts
- root action family
- bucket type
- beam depth reached
- node count / expanded count
- hotspot flags
- chosen enemy reply policy classes
- evaluator breakdown:
  - terminal
  - length delta
  - losses / tiebreak pressure
  - territory
  - apple ownership
  - mobility
  - danger
  - chamber / runway
  - coordination penalty
- reason winner beat runner-up
- whether diversity quota preserved the winner line

### Acceptance criteria
- Debug logs answer “why this move?” without source diving.
- A failed map can be diagnosed from logs in one pass.

### Implementation prompt
You are starting with empty agent context. Read this epic and the current Epic 8 file. Implement Story 8.11 by making diagnostics first-class. Add root and winning-line logging that includes: turn, elapsed ms, live snake counts, root family, bucket type, beam depth, expanded nodes, hotspot flags, active enemy reply policies, evaluator breakdown terms, winner-over-runner-up reason, and whether diversity retention mattered. Keep logs structured and compact enough to use in repeated simulations. The result should let a fresh developer answer “why did the beam choose this?” from logs alone.

---

# Story 8.12: Regression and benchmark ladder
**As a** maintainer,
**I want** an explicit validation ladder,
**So that** Epic 8 proves itself against the real failure modes that motivated it.

### Rule for all new work
Tests first where practical. New map suites should be added rather than weakening old expectations.

### Required validation groups

#### Group A: Engine fidelity / baseline survival
Use existing pathing and complex-pathing maps to prove the new simulator and safety logic do not regress obvious basics.

#### Group B: Corridor and post-growth escape
Use corridor and well maps to prove the new search does not greedily trap itself after growth.

#### Group C: Enemy foresight and punish windows
Use enemy tunnel / trap maps to prove the bot does not treat opponent modeling as cosmetic.

#### Group D: Cooperative allocation
Use coop and multi-snake maps to prove team snakes distribute naturally.

#### Group E: Adversarial multi-snake maps
Create a new validation folder focused on 3v3 and 4v4 tactical pressure.

### New map folders to create
- `bot-development/test-maps/beam-search/`
- `bot-development/test-maps/adversarial/`
- `bot-development/test-maps/multi-snake-adversarial/`

### Mandatory new test concepts
Create tests for each of these concepts:
1. **Greedy apple loses chamber**
2. **Temporary retreat wins later**
3. **Two equal apples but one is enemy-punishable**
4. **Friendly snakes must split across basins**
5. **Enemy head pressure near contested apple**
6. **One snake anchors center while another harvests**
7. **Fake-best line should be pruned, slow-good line preserved**
8. **Root quota preserves non-greedy winning family**
9. **Distant enemies are abstracted without tactical loss**
10. **Hotspot enemies trigger local branching**

### Acceptance criteria
- The validation ladder is scripted or at least clearly documented.
- Epic 8 is not considered complete without adversarial map coverage added by Epic 8 itself.

### Implementation prompt
You are starting with empty agent context. Read this epic, inspect existing map folders under `bot-development/test-maps/`, and inspect the simulation harness under `bot-development/simulation/`. Implement Story 8.12 by creating the missing adversarial regression assets for Epic 8. Add new map folders if absent: `bot-development/test-maps/beam-search/`, `bot-development/test-maps/adversarial/`, and `bot-development/test-maps/multi-snake-adversarial/`. Add tests or documented scenarios for greedy-apple traps, temporary retreat wins, punishable equal apples, basin splitting, hotspot tactical branching, and preserved slow-good beam lines. Do not weaken older maps to make new code pass. Also document or script the validation ladder so Epic 8 can be benchmarked repeatedly against Epic 7 baselines.

---

## Mandatory algorithm details

### Shared-state evaluator formula
The exact numeric weights may be tuned, but the structure must be:

1. terminal / catastrophic terms
2. lexicographic survival terms where appropriate
3. additive heuristic terms after survival safety is established

The evaluator must expose components, for example:
- `terminal_score`
- `team_length_delta`
- `loss_risk_penalty`
- `territory_delta`
- `apple_ownership_delta`
- `mobility_delta`
- `head_danger_penalty`
- `corridor_trap_penalty`
- `coordination_penalty`
- `pressure_bonus`

### Bucket assignment rules
Each node must be classifiable. Suggested rule set:
- `SURVIVAL`: poor territory but strongest immediate survivability
- `GROWTH`: path improves owned apple capture or realized growth
- `SPACE`: expands territory or chamber control
- `CONTEST`: improves contested apple or contested zone race
- `ATTACK_OR_PRESSURE`: creates enemy collision pressure or denies path
- `ESCAPE_OR_RECOVERY`: retreats from tactical danger while keeping future alive

### Root family rules
A root family must identify the strategic intent of the initial joint team action. Example families:
- `SPLIT_HARVEST`
- `ANCHOR_AND_FORAGE`
- `SAFE_SPREAD`
- `CENTER_PRESSURE`
- `CONTEST_LEFT_CLUSTER`
- `CONTEST_RIGHT_CLUSTER`
- `DEFENSIVE_RECOVERY`

### Beam retention rules
For each depth, keep something like:
- best `N` per bucket
- best `M` per root family
- max `R` near-duplicate novelty signatures

Exact numbers may vary by time budget and live snake count, but the code must visibly enforce these three constraints.

---

## What must be reused from current work

The following existing ideas are good and should be reused where they fit:
- padded off-world model
- fast simulation order matching the engine
- growth runway checks
- followup counting
- lexicographic survival comparisons
- own-team target reservation ideas
- local unsafe-head checks
- percentage-based timing awareness

But reuse must happen inside the new architecture, not as a substitute for it.

---

## What must explicitly be removed or demoted

These current patterns are not allowed to remain the main decision engine:

1. **Single-snake isolation as primary planner**
   - acceptable as a helper only

2. **Enemy default action = continue forward**
   - acceptable as one reply policy only

3. **Global nearest reachable apple as dominant objective**
   - acceptable as one heuristic term only

4. **One flat top-K state list**
   - forbidden

5. **Team action chosen by independent per-snake best move only**
   - forbidden as the main root generator

---

## Implementation order

### Phase 1: Stable shared-state base
1. Create new Epic 8 bot file.
2. Port and verify shared simulation core.
3. Add full shared-state hashing.
4. Add structured debug scaffolding.

### Phase 2: Coarse evaluator
1. Implement multi-source territory / apple ownership BFS.
2. Add mobility, head danger, chamber, and tie-break terms.
3. Expose evaluator breakdown in logs.

### Phase 3: Root generation and enemy policy abstraction
1. Generate structured friendly root families.
2. Add enemy reply policy sets.
3. Add hotspot detection.

### Phase 4: Diverse beam implementation
1. Implement bucket classification.
2. Implement root-family quotas.
3. Implement novelty filtering.
4. Add iterative deepening and phase timing.

### Phase 5: Tactical local branching
1. Increase branch depth in hotspots.
2. Abstract distant snakes.
3. Add tactical extension diagnostics.

### Phase 6: Regression and tuning
1. Add new adversarial test maps.
2. Run ladder against Epic 7 baselines.
3. Tune only after architecture is complete.

---

## Definition of done

Epic 8 is done when:
- the new bot compiles and runs from its own source file
- the main move chooser is a shared-state adversarial beam search
- the beam retains diverse strategic lines
- nearby enemy punish moves are modeled explicitly
- the evaluator includes territory and apple ownership, not only local route safety
- logs explain chosen lines and preserved alternatives
- the bot stays inside the time budget
- the regression ladder and new adversarial tests are green or clearly characterized with documented blockers

---

## Final note to future implementers

If implementation starts to drift toward:
- “just add one more safety heuristic”
- “just isolate one snake and hope the rest keep going”
- “just keep the highest score states”

then Epic 8 is being violated.

The whole point of this epic is to finally build the missing architecture:
**a team-aware, adversarial, diversity-preserving beam search that can survive 8-snake simultaneous play under real-time constraints.**
