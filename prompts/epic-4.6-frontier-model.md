# Epic 4.6 Frontier Model

## Goal

Epic 4.6 turns the clean frontier bot into the next real successor to Epic 4 by making the frontier scan:

- adaptive instead of fixed-depth only
- safer on short gravity-path maps
- schedulable for teams of multiple snakes
- explicit about long-term apple goals versus center exploration fallback

The implementation target is:

- [bot-development/bots/epic4-reachable-frontier-bot.cpp](bot-development/bots/epic4-reachable-frontier-bot.cpp)

This file is the intended evolution path from:

- [bot-development/bots/epic4-solver-BFS-bot.cpp](bot-development/bots/epic4-solver-BFS-bot.cpp)
- [bot-development/bots/epic4-deep-search-only-bot.cpp](bot-development/bots/epic4-deep-search-only-bot.cpp)

---

## Story 1 — Adaptive frontier depth

### Problem
The current frontier bot still uses hard-coded scan limits. That is better than the older `20` cap, but still artificial.

### Change
Replace fixed-depth behavior with adaptive scan budgeting based on:

- remaining turn time
- map size
- number of our live snakes
- whether the snake already has a proven apple goal
- whether the snake is in a short direct-path situation or a long exploration situation

### Acceptance
- single-snake long maps can still reach about `24-26+` ply when time permits
- multi-snake turns automatically reduce depth without crashing or timing out
- logs print chosen depth and expansion budget per snake per turn

---

## Story 2 — Short-path protection layer

### Problem
Maps like `03` and `04` in `complex-pathing` regress when long-horizon apple-progress logic dominates short direct gravity paths.

### Change
Add a direct-apple protection layer before deep frontier progress ranking:

- build a cheap static wall-aware apple distance estimate
- if an apple is very near in direct path terms, prefer that apple as the progress target
- use this as a stabilizer for short and mid-path gravity maps

### Acceptance
- maps `03` and `04` stop regressing versus the older bot family
- short local apples beat center fallback and beat distant apple-progress bait

---

## Story 3 — Persistent proven apple goals

### Problem
The bot can discover a good apple route, then temporarily lose direct visibility during falling/transition turns and fall back too early.

### Change
Persist apple goals separately from center goals:

- remember the last proven apple target for some TTL
- keep scanning toward that target while it still exists
- clear it only when consumed, expired, or invalidated by state

### Acceptance
- proven apple paths survive short barren phases
- logs distinguish `apple_goal` from `center_goal`

---

## Story 4 — Partial apple progress ranking

### Problem
When no apple is fully collectible within the current frontier depth, the bot still needs a strong way to decide which apple-progress branch is best.

### Change
Rank partial progress using multiple terms:

- nearest visible apple distance
- followup count / space after the frontier state
- frontier depth where progress was achieved
- static path closeness to that apple from the current state
- optional bonus for apples that later become fully reachable inside iterative deepening

### Acceptance
- `apple_progress` stops acting like a weak center-fallback surrogate
- long paths improve without breaking short ones

---

## Story 5 — Team scan scheduling

### Problem
A single-snake frontier scan can afford deep search. A 4-snake team cannot give every snake full depth each turn.

### Change
Introduce per-turn team scheduling:

- assign one or two snakes as deep-scan priority snakes
- give the rest shallow scans or cached-goal maintenance
- prioritize collectors, trapped snakes, and contested target snakes

### Acceptance
- no naive 4x duplication of the full single-snake scan
- map `12` no longer catastrophically fails from timeout / starvation behavior
- logs show each snake’s scan class: deep / medium / shallow

---

## Story 6 — Frontier logging and diagnostics

### Problem
The new bot is understandable only if the scan budget and target mode are visible in logs.

### Change
Extend `FRONTIER_DECISION` logging with:

- depth limit
- expansion limit
- per-snake scan tier
- apple-goal vs center-goal mode
- whether direct-apple protection fired
- remaining time headroom estimate

### Acceptance
- debugging no longer depends on reading code to infer scan settings
- regressions can be classified quickly as ranking, reachability, or budgeting issues

---

## Story 7 — Regression gate for Epic 4.6

### Problem
The clean frontier file must not improve one map by silently regressing a cluster of old maps.

### Change
Use a standing regression set focused on:

- `complex-pathing/03`
- `complex-pathing/04`
- `complex-pathing/05`
- `complex-pathing/06`
- `complex-pathing/10`
- `complex-pathing/11`
- `complex-pathing/12`

### Acceptance
- `05` and `06` stay passing
- `03` and `04` recover
- `11` reaches expected on both sides or exceeds on at least one while matching on the other
- `12` becomes non-catastrophic and clearly improved

---

## Backlog — Survival-aware contested frontier paths

### Problem
The frontier bot can still choose `apple_progress` lines that are geometrically promising but strategically losing because another snake controls the same arrival cell or corridor.

This shows up when a shorter snake walks into a head-to-head it cannot survive, even though the isolated frontier step itself looks valid.

### Change
Add survival-aware contest logic to frontier ranking:

- penalize frontier branches that converge into cells likely to be entered by an equal-or-longer opponent head
- penalize narrow approach corridors already occupied or controlled by a longer snake
- prefer branches that preserve escape options when an apple lane is contested
- distinguish isolated reachability from contested survivability in logs

### Acceptance
- shorter snakes stop dying in obviously losing head-to-head races
- `apple_progress` no longer overrides survival when the destination corridor is opponent-controlled
- short contested maps improve without regressing long uncontested map `11`

---

## Implementation order

1. Adaptive frontier depth
2. Short-path protection layer
3. Persistent apple goals
4. Better partial apple-progress ranking
5. Team scan scheduling
6. Logging upgrades
7. Regression sweep and tuning

---

## Current status snapshot

At the moment, the clean frontier bot has already shown:

- strong long-range single-snake performance on map `11`
- a major jump from the earliest clean-file result
- remaining weaknesses on short-path regressions (`03`, `04`)
- insufficient team scaling on map `12`

Epic 4.6 is the work needed to turn that promising clean prototype into the main successor line.
