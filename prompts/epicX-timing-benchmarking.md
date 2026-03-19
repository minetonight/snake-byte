write these takeaways down in an handoff for the project owner.  

do a timings benchmark and report how much of the allotted 69ms per turn do the bot use on map 11. print shortened table: if consecutive turns take similar time, report them as one row, when there is significant change in one turn, report that row individually. the output must be in added into #file:epicX-benchmarks.md 

* also report time per search method, which function takes what percent of the time, in another table.

# takeawys from benchmarking so far:
it is a pathfinding problem task, not the other proposed algorithms from the bots: 
PHASE_FORWARD_BFS_PCT
PHASE_BACKWARD_BFS_PCT
PHASE_LOCAL_COMBAT_PCT
PHASE_POWER_PLANNER_PCT
PHASE_PATH_AND_SCORING_PCT


and power_planner in both epic4-bfs and in epic3-deep-solver
always take 90% of whatever time is given.


phase_path_share_pct in BFS

# aleks optimisation ideas - 
the longer the snake, the more the pathfinding simplifies to a grid, not gravity. we need to know the next platform to reset our gravity fall.
- 
- what we really need is a snake looped detection: when we have more than 10 turn with UP only, set the target block and path 20 cells away in the direction of the nearest (grid distance) power cell. thus break looping snakes. 
- in the pruning algorithm - kill branches that keep the snake at the same place where it started from: hash of the snake is the same: thus we check for all body positions with a quick function. 


I want you to not test map 12, and focus on maps 05 new map 06 and map 11 is adapted with apples every 24 steps. 
    - dropped idea - What we really need is a snake looped detection: when we have more than 10 turn with UP only, set the target block and path 20 cells away in the direction of the nearest (grid distance) power cell. thus break looping snakes. 

What is our search like now, how do we evaluate better moves, what is our scoring algorithm like?
We need improvement of the evaluation of future moves by length of the snake. If the length is smaller, that move is scored lower.

In the pruning algorithm - give lower score to branches that keep the snake at the same place where it started from: some fast way to evaluate past and new states like if the hash of the snake is the same: thus we check for all body positions with a quick function. i presume that head coords will change and the vector will change if the snake is rotatiting out of confined space.

--------
progress: 
we are deep iterative search only now, see prompts/epic-4-results-algorithm-overview.md for details

the real question is why we mark apple at 39, 13 as reachable? what is our distance algorithm. can we create something like SPAM algorithm, simultaneuos mapping and positioning and add only the reachable apples found during the deep pathing.  
heuristic for when no apples are found in the accessible ply depth on the map: go toward the centre of the map. store that as long term goal and find path there. along the way the deep search algorithm will scan for apples and if found will override the goal. no? correct me for my hopes.