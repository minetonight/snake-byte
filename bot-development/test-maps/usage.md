You can place up to 4 snakes per player (8 total lines of 0 and 1). Just draw the snakes as contiguous lines of 0s (for Player 1) and 1s (for Player 2) on your text map! The engine will automatically stitch them together based on adjacency.

# Expected score
Each map defines the final expected score. If out bot(s) dont collect them, the test is a fail.

# subfolders
The root folder test-maps just checks if the bots survive for 200 turns.
The tactics subfolder checks for smart bot behaviour.
Split into two because the ai agent is running tests on all maps which is good and bad.

# LIMITation for equal snakes count
You must always ensure that both players have the exact same number of snakes on the map.

If you give Player 1 two snakes, you must give Player 2 two snakes. They can be different sizes, facing different directions, and placed anywhere on the map, but the total count per team must be equal so your bots can parse the standard input correctly!

## head definition
The Head will always be the block that is highest up. 
If it's a perfectly horizontal snake, the Head will be the furthest *left block*.

## directions
If you draw the snake vertically (like you did), the engine sorts the coordinates top-to-bottom. The head is at the top, so its body faces UP. WAIT will make it move *UP*.
If you draw the snake horizontally, the engine sorts left-to-right. The head will be on the left, so its body faces LEFT. WAIT will make it move *LEFT*.
you draw irregular shape the head is at????

# Running 
## when you need to see what happened in a game 
run the full server using Main.java instead ot Headless.java
```bash
cd WinterChallenge2026-Exotec/
mvn compile exec:java -Dexec.mainClass=Main -Dexec.classpathScope=test -DcustomMapFile=$(pwd)/../bot-development/test-maps/test_map_with2-eating.txt -Dexec.args="python ../bot-development/bots/Boss.py|||../bot-development/bots/bot-development/bots/test1-solver-bot.cpp"
# kill the server after the game result is ready
cp /tmp/codingame/game.json ../bot-development/read-logs-here/game-boss-vs-bot-on-map-eating.json
```
Go to read-logs-here and see reading-game-json.md.
Parse and read the file.

## sh script for catching errors
run test-all-maps-wth-boss.sh to test all *.txt maps in /test-maps.
the goal of the test is to see if any snakes are spawned disjointed. 
this tests if the maps are generating as expected.
look for 
```sh
java.lang.NullPointerException: discontinuous snake parsed from map.txt: (0, 0)
```
if none, you are good to run your bots on these maps. 

## python test for real tests
Edit and run run_simulation.py with your bot(s).
You are supposed to be bot 0 in all tests,
bot 1 is the benchmark dummy enemy.
If the expected score of the map and your score are different, the test is a fail. 
You are supposed to play against dummy BOSS.py that just does WAIT command.
### test all custom maps """
    test_all_maps(bot1, bot2)
    
### test standart codingame engine """
    for _ in range(5):
        print("Scores (P1, P2):", run_simulation(bot1, bot2))

### test single custom map simulation """


# bad maps - avoid
```
###############
#....@........#
#.......@.....#
#.......#..1..#
#..#0......1..#
#..00..##..1..#
#..0#......@..#
#..#@.........#
###############
    p1Spawns: [[(3, 5), (3, 6), (4, 4), (4, 5)]]
    Bird head at: (3, 5), facing: N
    p2Spawns: [[(11, 3), (11, 4), (11, 5)]]
    Bird head at: (11, 3), facing: N
    oof, the snake head is in the middle! and the snake is broken structure.
    #0
    h0
    0#
```

# analysys
```
###############
#....@........#
#.......@.....#
#.......#..1..#
#.......00.1..#
#......##0.1..#
#........0.@..#
#...@.........#
###############
    p1Spawns: [[(8, 4), (9, 4), (9, 5), (9, 6)]]
    Bird head at: (8, 4), facing: W
    p2Spawns: [[(11, 3), (11, 4), (11, 5)]]
    Bird head at: (11, 3), facing: N


###############
#....@........#
#......1@.....#
#......1#..1..#
#....0.1...1..#
#....0.##..1..#
#....0.....@..#
#.000#........#
###############
    islandO: [(5, 4), (5, 5), (5, 6)]
    islandO: [(4, 7), (3, 7), (2, 7)]
    island2: [(7, 2), (7, 3), (7, 4)]
    island2: [(11, 3), (11, 4), (11, 5)]



p1Spawns: [[(2, 4), (2, 5), (3, 5), (3, 6)]]
p2Spawns: [[(11, 3), (11, 4), (11, 5)]]
Bird 1360 head at: (2, 4), facing: N
Bird 1361 head at: (11, 3), facing: N
```

