# Epic 3: Search & Heuristics

## Planning
- [x] Review Epic 3 requirements and baseline code
- [x] Create implementation plan and request user review

## Execution
- [x] Add runtime profiling and time management (73ms budget)
- [x] Implement Voronoi Territory Division (BFS from heads to powerups)
- [x] Implement Modified A* Pathfinding with Gravity (Reuse `GameState::simulate`)
- [x] Implement Decentralized Priority Engine (Survive with Flood Fill > Exclusive > Contested > Block)
- [x] Integrate routing and decision-logic within game loop

## Verification
- [x] Run pathfinding test 1: `pathing/01 check gravity short path-right side.txt` (PASSED: P1=4, P2=3)
- [x] Run pathfinding test 2: `pathing/02 check gravity short path-left side.txt` (PASSED: P1=4, P2=3)
- [x] Verify time management stops search < 73ms (turn timer reset each loop; all search loops guarded by `out_of_time()` at 69ms)
- [x] Update documentation/results for Epic 3

## Notes
- The left-side short gravity map present in `test-maps/pathing` is `02 check gravity short path-left side.txt`.
