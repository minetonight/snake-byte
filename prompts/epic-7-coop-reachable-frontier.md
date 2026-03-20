# Epic 7: Cooperative Reachable-Frontier Bot

**Goal:** Extend the clean reachable-frontier bot into a reliable multi-snake cooperative bot that avoids teammate pileups, shares apples, and keeps late-game space open.
**Primary implementation target:** [bot-development/bots/epic7-coop-reachable-frontier-bot.cpp](bot-development/bots/epic7-coop-reachable-frontier-bot.cpp)
**Current executable:** [bot-development/bots/epic7-coop-reachable-frontier-bot.exe](bot-development/bots/epic7-coop-reachable-frontier-bot.exe)

## Context

Epic 4 established the clean reachable-frontier model:

1. scan only truly reachable simulated states
2. choose apples from that reachable frontier
3. fall back to a persistent center-oriented goal when no reachable apple is available
4. use percentage-based turn budgets so several snakes can search within the same turn
5. please set logs output to snakebyte/WinterChallenge2026-Exotec or to bot-development/read-logs-here.

Epic 7 starts where Epic 4 stops:

- several friendly snakes can now search in the same turn
- but they still sometimes converge on the same center tile or same apple basin
- and on coop maps some snakes still take locally reachable but strategically bad apples that strand them

## User-facing problem statements

1. Late game: multiple friendly snakes target the same center tile and crash.
2. While moving toward center, snakes should notice reachable apples on the way and interrupt the center plan.
3. Many-target coop maps should distribute snakes instead of collapsing into one basin.
4. Maps `13` and `14` are real future work and should stay backlog items for now.

## Planning constraints for the next phase

1. **Step 1 is to restore the Epic 4.6 regression gate inside the active Epic 7 bot.**
	Before adding more shared-snake behavior, the active bot must again hold the important solo / frontier baseline maps.
2. **The cases in [prompts/epicX-enemy modelling.md](prompts/epicX-enemy%20modelling.md) are top priority planning items, not side notes.**
	In practice this means the next work must explicitly cover:
	- enemy foresight on [bot-development/test-maps/enemies/10b-deadly tunnel - foresee enemies.txt](bot-development/test-maps/enemies/10b-deadly%20tunnel%20-%20foresee%20enemies.txt)
	- corridor post-growth escape on the `pathing/*corridor*.txt` family
	- tail-follow / curl behavior on [bot-development/test-maps/complex-pathing/07-do-curl_up-angled-snake.txt](bot-development/test-maps/complex-pathing/07-do-curl_up-angled-snake.txt)
3. **Shared-snake planning comes after the solo snake passes most or all of its intended baseline maps again.**
	Cooperation should be built on a stable single-snake frontier, not used to mask unresolved solo pathing failures.
4. **Map organization has changed.**
	Many of the relevant shared-behavior validations now live under the `coop/` and `multi-snake/` folders and should be treated as the canonical grouped suites for team behavior.

## Current findings

### Already implemented in Epic 7

- renamed active source to [bot-development/bots/epic7-coop-reachable-frontier-bot.cpp](bot-development/bots/epic7-coop-reachable-frontier-bot.cpp)
- reserved center targets per turn
- reserved apple targets per turn
- direct-apple interruption before committing to center movement
- center goals restricted to valid empty / powerup cells
- reachable-frontier scans can now skip apple targets already reserved by earlier teammates
- later teammates inherit those reservations after earlier scan decisions
- suspicious low-escape apple checks were added for medium-tier cooperative searches

### What the logs still show

- some maps improved from “same apple” duplication to “different apple” allocation
- but early coop failures remain on `10a` and `10b`
- some snakes still collapse to `expanded=1` and `fallback_legal`
- some short-depth apples are still strategically bad even when locally reachable
- at least one class of coop maps appears to require behavior beyond simple per-turn reservation

## Story plan

### Story 7.0: Restore the Epic 4.6 regression gate inside Epic 7
**As a** maintainer of the active reachable-frontier bot,
**I want** the Epic 7 file to preserve the important Epic 4.6 solo/frontier regressions,
**So that** new cooperation logic is added on top of a stable base instead of on top of silent solo regressions.

**Acceptance criteria**
- Reconfirm the active Epic 4.6-style regression set inside the Epic 7 bot before treating coop work as stable.
- At minimum, hold or recover the key solo maps from `complex-pathing`, `pathing`, and `tactics` that represent frontier correctness, post-growth survival, and long-path planning.
- Any coop change must be checked against this regression gate before being considered complete.

### Story 7.0b: Enemy-model and corridor correctness as first-class blockers
**As a** developer,
**I want** the enemy-modelling and corridor notes already identified to be tracked as active blockers,
**So that** coop work does not proceed while obvious solo tactical failures are still unresolved.

**Acceptance criteria**
- [bot-development/test-maps/enemies/10b-deadly tunnel - foresee enemies.txt](bot-development/test-maps/enemies/10b-deadly%20tunnel%20-%20foresee%20enemies.txt) is part of the active near-term ladder.
- The corridor family under `pathing/` is treated as a standing post-growth escape regression cluster.
- [bot-development/test-maps/complex-pathing/07-do-curl_up-angled-snake.txt](bot-development/test-maps/complex-pathing/07-do-curl_up-angled-snake.txt) is treated as an explicit tail-release / curl capability check.

### Story 7.1: Cooperative target reservation
**As a** team of friendly snakes,
**I want** earlier snake decisions to reserve center and apple targets for later teammates,
**So that** we stop issuing identical long-term goals.

**Acceptance criteria**
- Later teammates should not choose an apple already claimed earlier in the same turn unless it is their persistent apple goal.
- Later teammates should not refresh to the same center anchor when other valid anchors exist.
- Logs should show diversified `target_x/target_y` selections on `11a`-style many-target openings.

### Story 7.2: Apple interruption during center travel
**As a** snake traveling toward a center fallback goal,
**I want** reachable apples along the route to preempt the center plan,
**So that** the bot converts travel turns into growth when safe.

**Acceptance criteria**
- A direct reachable apple may override a center goal.
- The interrupted apple target becomes the new persistent apple goal when selected.
- Center goals are cleared when a concrete apple route is committed.

### Story 7.3: Low-escape apple rejection
**As a** cooperative planner,
**I want** to reject locally reachable apples that strand the snake,
**So that** the team does not sacrifice one snake for a trivial short-term gain.

**Acceptance criteria**
- Apples with no safe simulated continuation are rejected.
- Medium-tier coop searches also re-check suspicious low-escape growth states.
- Logs should no longer show repeated `reachable_apple -> fallback_legal -> expanded=1` patterns after obvious bait apples where an alternative basin exists.

### Story 7.4: Stuck / shaft snake behavior
**As a** snake with only gravity-neutral or same-body moves,
**I want** explicit “hold / wait / shaft” behavior,
**So that** the bot does not treat every stable non-progress move as a planning failure.

**Acceptance criteria**
- Identify cases where the only legal move keeps the body hash effectively unchanged.
- Distinguish “safe hold” from “true dead state”.
- Use a deliberate hold policy when waiting preserves future team options better than random fallback.

### Story 7.5: Team-aware basin allocation
**As a** group of friendly snakes,
**I want** each snake biased toward different apple basins or lanes,
**So that** we cover more of the map and reduce crossovers.

**Acceptance criteria**
- Add a light basin / side / lane bias on top of per-turn apple reservation.
- Prefer local apples when two options are equivalent in safety.
- Avoid sending one snake across another snake’s natural apple corridor unless local options are exhausted.

### Story 7.6: Validation ladder for coop maps
**As a** developer,
**I want** an ordered validation ladder,
**So that** behavior is stabilized incrementally instead of by random map hopping.

**Active ladder**
1. Restore the solo / frontier regression gate from Epic 4.6 inside the active Epic 7 bot
2. Clear the top-priority tactical blockers from [prompts/epicX-enemy modelling.md](prompts/epicX-enemy%20modelling.md)
3. `coop/01-coop.txt` and `coop/01b-coop.txt` as baseline pass checks
4. `coop/10a-smallmap-2v2-E6Sx-many-targets.txt`
5. `coop/10b-smallmap-4v4-E6Sx-many-targets.txt`
6. `coop/11a-midmap-3v3-E6Sx-many-targets.txt`
7. `coop/12a-bigmap-2v2-E6Sx-many-targets..txt`
8. `coop/12b-bigmap-3v3-E6Sx-many-targets.txt`
9. `coop/12c-bigmap-4v4-E6Sx-many-targets.txt`
10. `multi-snake/12t-bigmap-E6Sx-team-long-term-many-targets.txt`

**Acceptance criteria**
- Do not move to shared-snake planning by default until the earlier solo/frontier blockers are understood.
- Do not move to later coop / multi-snake maps by default until earlier regressions are understood.
- Keep detailed log-based diagnoses for any map still failing.
- Re-run at least one non-coop regression after major coop logic changes.

## Backlog

### Backlog 7.B1: `13-lift-a-friend.txt`
This appears to require explicit teammate-assisted movement / lift behavior rather than only target diversification.

### Backlog 7.B2: `14-bigmap-coop-plan.txt`
This appears to be a larger cooperative planning problem and should wait until simpler reservation and basin-allocation work is stable.

## Immediate next technical focus

1. Restore the Epic 4.6 regression gate inside the active Epic 7 bot.
2. Solve the top-priority cases from [prompts/epicX-enemy modelling.md](prompts/epicX-enemy%20modelling.md):
	- enemy foresight tunnel case
	- corridor post-growth escape cases
	- curl / tail-release case
3. Separate “true no-path” from “stable hold / shaft” cases.
4. Continue reducing dead-end bait apples on coop maps.
5. Only then push deeper shared-snake planning on `coop/` and `multi-snake/` suites.
6. Add a light basin-allocation bias once target reservation is no longer the main blocker.

## Revised implementation order

1. Restore solo/frontier correctness in the active Epic 7 bot.
2. Clear the enemy-model / corridor / curl blockers.
3. Reconfirm that a single snake passes most or all intended solo maps.
4. Resume shared-snake planning on the reorganized `coop/` and `multi-snake/` suites.
5. Leave explicit lift and larger long-range team planning in backlog until the earlier steps are stable.

## Success definition for Epic 7

Epic 7 is successful when the reachable-frontier bot keeps the clean Epic 4 model but adds enough cooperation to make multi-snake behavior look intentional:

- teammates do not all rush the same center cell
- teammates do not all chase the same apple unless it is strategically necessary
- apples encountered during center travel are consumed opportunistically
- snakes avoid obvious low-escape bait apples
- later snakes still receive useful search time because the percentage-based timing model remains intact
