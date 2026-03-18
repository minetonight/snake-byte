# idea
start from the apples and create heuristics maps for each apple for each snake length.

## creation heuristics, not mathematically sound;
the array is initialized with positive max_value<type>.
recursuvely, starting from an apple cell with best value = 0:
* recursion start:
    - within world borders
    - start from cell and first propagate downwards, then left then right;
    - if the new tested cell has a value that is lower than the value that we would give it, stating cell best value plus increments, then kill this branch of the recursion; 
    - if the new tested cell have no connection with stepping stone (platform or apple) within snake_len steps, then the apple/cell is unreachable from that cell and the recursion stops. 
    the cells on that explored path are not updated, they remain at max value.
    - if we touch a stepping stone, 
    + because of earlier rule, we have lower value than the old value of the stepping stone, so proceed;
    + set the starting cell best value to its best value for the iteration/recursion;
    + update the cells we touched within our path with the len/distance that we travelled from the starting cell
    + make a recursion from the current cell that is on a stepping stone, with its best value set to the distance from the original starting cell;

example map for apple reachable from len 3 and len 4
..@........
...#..#....
.......#...
.#.......#.
.......#...
...........
###########

for snake len 3:
.9012345...
.99#99#678.
.9.....#.9.
.#.......#.
.......#...
...........
###########
the apple at 

for snake len 4:
91012345999
323#99#6789
43456..#.9-
5#5678...#-
676789.#...
78789......
###########





snakes usage: 
-  if any of the adjacent to head cells have value smaller than max_value<type>; then choose the lowest and travel towards that cell.


thus we have data-structure of which apples are reachable by which snakes at which lengths:
astar_fromapples[apple_id][snake_len][map with value shortest len path to the apple_id]
a longet snake_len map starts as a copy of the smaller len maps if any of them exist, as snake of len 5-6 can reach at least as well as snake of len 3-4;

then a snake can check the maps for its len for the lowest value in all apple ids and go to the closest apple.
when an apple is eaten, dissapper from the list of apples, reset its map to max values. 

time complexity and async execution:

optimisation for snakes:
 - if we touch a snake head cell at the current turn, store that power cell in a snake target if the slot is free. and break the recursion for that apple. there are trade offs here, might be worth it.
 - fill the map in the free time of the turns;

Of course, the priority order of survivealbility and general play algorithm must be kept. this algorithm hopes to solve longterm pathfinding problem.