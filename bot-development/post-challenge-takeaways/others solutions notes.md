# MCTS
Note:
>  we often argue about names of MCTS-like alogrithms here (DUCT, simultaneous UCT, UCT forest, multi-tree MCTS, ...) because they never mean the same thing in papers.
> we often call it (somewhat jokingly) smitsimax here when you keep stats per "agent", named after a CG user who was one of many who thought they were the first to come up with this specific variation (because of the naming mess)

# smitsi max

## article in CodinGame
https://www.codingame.com/playgrounds/36476/smitsimax



## Jasek (17th in Legend)

I used smitsimax, the MCTS tree for each snake separately. 
The depth was 8 at the beginning, but final version is depth 10. 
The selection part is heavily biased, during the selection the UCB is:

score/visits + C * sqrt(log(N)/visits) + X * isWall(move) + Y * isApple(move) + Z * isAlmostApple(move). 

Also there is e-greedy strategy in selection: 2% chance to choose entirely random move.

The parameters were tuned partly by CLOP, or optuna, but I ended up mostly with hand picking the values. 
C = 1.1, X = -1, Y = 1, Z = 0.25. 
I didn't entirely prune the wall moves because sometimes they could be beneficial. I tried to add other things into the selection, for example give lower score for snake collisions etc, but it didn't increase winrate even with trying different values.

The evaluation was neural network with tanh [-1,1] output. The network evaluation time is tiny in comparison to the whole code, and the inputs are:
- global stuff like width, height, wallDensity, apples count, rounds count
- sum of my body snakes
- sum of opp body snakes
- minManhattanDistance to nearest apple of all of my snakes
- minManhattanDistance to nearest apple of all of opp snakes
for each of my snakes, and opp snake:
- x, y position of the head
- minManhattanDistance to nearest apple for the snake
- body size of the snake
Normalized accordingly so the inputs are mostly in range (-1,1).
I also had 5 output buckets depending of the map size: height < 13 ? 0 : height < 16 ? 1 : height < 19 ? 2 : height < 22 ? 3 : 4.

The training was based self-play games and positions and win/lose labels. Win label is 1 * pow(0.999,roundsLeft), draw is 0 and lose is -1 * pow(0.999,roundsLeft), so near the end of game the scores would be more accurate. For example win is near 1 few rounds before end of the game, while in the beginning of the game the score to train is around 0.1

In the MCTS tree, I backpropagated the eval using the score from the network for my snakes, and -score for the network for opp snakes. But also for each invidual snake I modified the score based on wether it died, shrinked or got longer, something like: score = (1-delta) * score + delta * 1, delta meaning size difference of the snake x P, the P is 0.1 (also to be tuned).

The simulation part takes most of the time, then the tree traversal and lastly evaluation. I was heavily using AI chats to give me improvements on my code, to make it more efficient, and I modified the code with theirs hints that gave me 3x more simulations. I used free version of webchats, among them were deepseek, z.ai, chatgpt, claude ai and grok. They would keep telling me that having vector body in snakes is the bottleneck and they proposed deque or custom-made ring buffer, which was worse or at best neutral. In the end, I got something like 1500 iterations (MCTS traversal up to 10 turns) on medium map with 4 snakes per player.

## hints on smits max 
 - by Sheeesh
if you are doing smitsmax or a variant make sure you don't give shared reward for all snakes, because this will cause credit assignment problem, each snake should have it's own reward + shared reward(50% or something like that) between all 

# MCTS

## top 1 Legend (frictionlessBanana)

My solution uses a variant of MCTS. 
First, it is multi-tree MCTS / Smitsimax: each snake has its own tree. 
Second, the trees are not really trees: when similar states are encountered via different move sequences, they are combined into a single node. 
States are considered similar if the snake head is in the same location and the snake is facing the same direction (just came from the same neighbor location). When similar states are encountered on different turn numbers, I tried various approaches for whether they should use the same or different nodes, and eventually settled on using two nodes per similar state, chosen based on the turn number modulo 2. Thus the total tree size is bounded by 8 times the grid size.

Each MCTS iteration goes to a maximum depth of 12 moves, after which a static evaluation function is used. The evaluation includes win/lose, current score, distance to the nearest apple, 
snake elevation (preferring to be higher), 
and the number of apples closest to each player.

Move selection in MCTS is biased by a policy that favors certain moves over others. Moves that collide with a wall or a snake (other than the tail) are not considered at all. Otherwise, the rules consider things like whether the destination cell contains an apple, the tail of a snake that can eat an apple, or an empty cell that another snake can move into. There is a preference for moving closer to the nearest apple, for not moving too far to the left or right, and for moving to a cell that has support under it.

Distance to apples is determined by a simple BFS with no awareness of gravity, snake length, etc. On the first turn, I perform a BFS from each apple and then build a data structure where each grid cell stores a list of apples in order of increasing distance. At the beginning of each turn, I remove any eaten apples from the beginning of the lists. During the search, I have to scan the list until I find the first apple that wasn't eaten during the search.

To calculate the number of apples closest to each player, I use Manhattan distance from each snake head to a set of up to 64 apples chosen at the beginning of the turn and not updated during the search. Calculations are done using AVX2: The x/y coordinates and distances fit in 8 bits, so I can process 32 apples at once using 256-bit registers.

My simulation uses a 64x48 grid with walls around the edges. Walls, apples, and snakes are all represented in the grid to be able to quickly check whether a cell contains those things. To make falling faster, snakes keep track of how far from the tail they are supported by a wall or apple. On CG servers I simulate around 50,000 turns per 50ms (it varies a lot). That is with only about 1/3 of my runtime spent on simulation.

## radkhal

Hey everyone, loved reading the write-ups! I'm sitting around 3rd right now, but waiting to see what happens in the final run before celebrating too much lol.

My setup is basically a decoupled MCTS (each snake has its own tree) with a search depth that dynamically scales from 11 up to 20 as the board clears out.

To keep the search from doing dumb things, I heavily rely on hand-crafted priorities. It penalizes moving UP unnecessarily, prefers cells with physical support, and calculates risks for head-to-heads based on snake lengths.

For pathing, instead of calculating it every turn, I run a single Gravity-Aware BFS on Turn 1. From every apple, I flood-fill the grid, making moving UP cost 2 points while sideways/down costs 1. This spits out a massive static distance map, giving me sweet O(1) lookups during the MCTS rollouts.

For performance, I optimized the grid using a 1D array with a version counter, so I never have to waste time clearing the board between rollouts. Honestly, the absolute headache of this game was handling gravity correctly—I ended up writing a custom BFS just to detect clusters of "inter-coiled" snakes so that if a knot of snakes has no ground beneath it, the whole group drops together.

... Eval is definitely the hardest part  Raw iterations don't mean much if the bot is prioritizing the wrong things. Getting the apple control and safety penalties right in the eval took me forever to balance lol

## Jasec
see file [jasecs-bot-overview.txt](jasecs-bot-overview.txt)

## iodrisi (91st Legend)

Main Takeaway

The opponent model was the ceiling. A 1-step heuristic predicting opponents while searching 12 deep creates phantom
paths that no eval fix can solve. Needed MCTS or DUCT for a real jump — started DUCT too late.


# Beam search

## BlasterPoard (№2-4 Legend)
#2, according to the leaderboard (really ~#4)

I used beam search, which was a mistake. It fails at interacting with the opponents, and in high legend, some bots insisted on interacting.

A beam can either search for a single snake's path, or multiple snakes if they can reach the same square in <= 3 turns or if a head's next to a tail (to not hit a friend's tail when he's eating). 
I also have ~half-width beams for enemies which are never joined together. 

Eval = lengths of snakes and sum of c * p/(1+distance) for every apple and every snake in beam, where c is a constant and p is the probability that the apple has not been eaten on the previous turns, calculated from the other beams.

As I said before, beam can't really handle opponents, so I put a band-aid on it in form of proof number search that runs for the first 8ms of every turn. I run it for every possible action that my snake can do, against up to 2 of opponents' snakes, and I try to disprove the move (disproven = will lose snake's length). It doesn't handle simultaneous moves - my snake moves first, then the opponents. Disproven moves are forbidden in the beam search. It's still not enough to fix the beam search, because it doesn't handle other stuff, like boosting enemies up, a combination of my snakes' moves killing one of them, standing on enemies' snakes, ...

Before the rerun, seeing that codingame's VMs were in a catastrophic state, I lowered my time limit, capped search depth, and added more conditions that sometimes shorten my time limit - in particular, when I'm losing, I try to make the game longer while using only ~20ms, trying to make the opponent time out. This helped me win ~5% games (but losing ~1% in bot's strength), thus securing #2 spot on the leaderboard. That's what CG will turn into unless someone looks into the problems with VMs. Some people lost 11+% games to timeouts, compared to my 1.8%, and the only thing they did wrong was playing the game the way it was *supposed* to work.

## Whatever using Gemini 3.1
See file [beam-search.txt](beam-search-bot-overview-by-whatever.txt)

# neural networks:

## Sheeesh PPO
[Sheeesh git gist](https://gist.github.com/Sheee-sh/ae2dd0474412d604025fbedf883e89e0)
[and post mortem](https://gist.github.com/Sheee-sh/8a1d43b26961e0991e3b185a44828ae5)

# RHEA (Rolling Horizon Evolution Algorithm)
## Mindrak (top 10 in Java)

Postmortem: RHEA Snakebird (Java)
**Final Rank:** 202 (Peak 135) | **Java Rank:** Top 10

Overall, I’m happy with the result. This contest was heavily simulation dependent, and RHEA (Rolling Horizon Evolution Algorithm) was great for emergent cooperative behavior.

### The Architecture
The agent uses a **Rolling Horizon Evolution Algorithm**. Instead of a search tree, I evolved a population of action sequences across turns.

* **Population Size:** 20 (Tested 5; ranking was nearly identical).
* **Horizon:** Dynamic (Depth 4 → 10, expanding once per turn).
* **Generations:** 20–70 per turn (highly variable due to server load/physics).
* **Evolution:** 20% Elite, 50% Crossover, 30% Random.
* **Reuse:** Full population shift each turn (Rolling Horizon).

### Legend Push
The single biggest ELO boost was ensuring that target tiles were empty.
This massively reduced garbage results and allowed the RHEA to focus on strategy.

### Heuristic & Emergent Behavior
The heuristic was the main factor driving results. I moved from a very simple to a competitive distance model which included current length of snakes, distance to nearest fruit, and prioritized the snake closest to a fruit.  This resulted in emergent cooperative and competitive actions.

**Emergent results:**
Because I modeled all snakes simultaneously in the simulation, I saw 3 snakes coordinate to create a ladder to a fruit or surround and destroy all of the opponent's snakes <https://www.codingame.com/replay/880470532>. None of this was hard-coded; it emerged naturally from the evaluation function.

### Performance
* **BitSet Impact:** Early on, using java BitSet gave me **20k+ sims/turn**. However, my strategy code was very unoptimized and dropped me down to closer to 2k sims/turn.
* **Opponent Modeling:** I treated opponents as purely random. In hindsight, I spent more CPU cycles on this and less thought than I should have.  If I had a lot more time for the contest I might have used a lightweight Neural Network (<100 nodes) for opponent prediction or precomputed full distance maps for varying snake lengths.

### Final Thoughts
I dropped from 135 to 202 in the final hours. I suspect it was a mix of the "final rush" from other players and Java's performance variance under heavy server load. 

**Favorite takeaway:** The emergent behavior from RHEA was amazing.  I enjoyed it way more than if I had a higher rank where my snakes just raced the enemy.

# Hybrid solutions
## Delineate - #6 in Legend  

I started with an MCTS/DUCT-like approach, that did great with local fighting but was really poor at long-term planning and pathfinding.

Then I tried a BFS-like approach: each snake pretends like it's the only one on the board, but it tracks everything perfectly (gravity, apple eating, etc). This was much better at pathfinding and longer-term planning, but this was really weak at local fighting.

Then I experimented with beam search a bit to get better multi-snake coordination. In the end I combined all three approaches. It was quite messy, but I tried to capture the strengths of each algorithm.

The overall pipeline each turn looks like:
1) beam search for any pair of snakes that are close together ("pair plans")
2) a BFS-type search for the remaining snakes (with pruning, this could allow a snake to make 20-25 move "plans")
3) all these "plans" were then fed into a MCTS/DUCT to bias it (so it tried to stick with a snake's original plans but the plans could change if the MCTS/DUCT strongly preferred something else)

The server latency spikes/timeout issues at the end were very frustrating, and it was an unfortunate end to what otherwise was a fun competition.

> Q: in your DUCT, was it a tree per snake, or just one huge tree?

A: at every game state, each snake keeps it's own statistics/samples by itself (I think that's a tree per snake? but each snake just has up to 3 moves it's sampling from as opposed to 3^4 (81) joint moves) (I do sequentially sample the moves per snake, so if snake0 decides to move to a square, then snakes1-3 can't move there)

> Q: how did u use the BFS/beam plans into the DUCT?

A: This part was quite messy / probably not theoretically sound. The "snake plans" were used in a few ways:
1) when doing rollouts (I do "smart random" rollouts to depth 8 before evaluating the state): if a snake is on its preset plan, it will continue to follow its plan
2) when picking which "best move" to take at the root node at the end, instead of using the most visited action, it looks at the average ev of each action and then adds a small bonus to the action that matches its plan if it has one

This wasn't perfect, and the ev metric is very noisy with not that many duct iterations

# On workflows

I finished 164 in legend, using Rust. As always I really enjoyed the challenge, and the community. Thanks Codingame for those events, and big thanks for avoiding the Chrismas holidays and post-poning it to march.
the bot

I used a beamsearch of width 50 and max depth of 20, for each individual snakes. I first run it for opponent snakes, using a total of 5ms (thus stopping between depth 5~10). The remaining 40ms are for my snakes. At each search, I retain the best sequence of moves for the snake, and use it in the next searches for other snakes. I also kept 15 random nodes per depth level trying to add some diversity to the search.

I simulate the whole game at each step, using the already computed moves of preceeding snakes, or default direction for non processed snakes (they keep going on the same direction).

For evaluation, as always it is a mess composed of the number dead snakes (only mine), the sum of my snake’s body parts and the manathan distance of each head to the closest powersource. The manathan distances are computed only once at the begining of a turn and never updated during the search.

I spent some time working on the engine, and was able to simulate around ~30000 turn in 50ms. workflow

over the years I tried to streamline my workflow to avoid wasting time during challenges. With this challenge I am happy about the way I was able to write and test the game engine, improving its performance, test my bots locally etc…

- I started using a bundler to split my code and make it easier to manage. big thanks to @MathieuSoysal for [cg-bundler](https://github.com/MathieuSoysal/CG-Bundler)
- I used cargo test to add unit testing for various parts, in particular for the engine. Cargo really help reducing the friction to write tests by simplifying the process. I reworked the input read part to be able to read either from stdin or from a Vec<String>, which allowed me to easily take inputs from the web IDE and transform it into a test case. This helped me a lot when I was optimizing the engine, because I was not afraid of breaking things along the way. I even used llvm-cov to assess coverage of my tests and make sure everything worked properly.
- For local testing I use the referee with additional CommandLineInterface.java to produce an executable that I can use with [psyleague](https://github.com/FakePsyho/psyleague). That allows to run multiple bots in a local arena and quickly see how they perform. For bot using the 50ms, the local referee often time report timeout, so to avoid that I change the turn limit in the referee to 100ms, but since it hits the global 30s maximum time limit, I had to recompile [Codingame engine](https://github.com/CodinGame/codingame-game-engine) to increase this limit also. The only downside with local arena is that using too many similar bots sometime does not reflect how a bot can perform in real arena, so I had to often cleanup the bot pool to mitigate this.


# heuristics / path finding
### hack
My breakthrough was when I ran a simple (yet very efficient) projection target, nothing more than a direct line of sight (±1 x/y tolerance) to the nearest supported cell. This naturally encouraged my "blind" snakes to reach a support that would get them closer to a target, and once they're there, they either "see" the now closer target or the following closer supported cell.
No kidding this change alone bumped me from 18 to 21.5 elo score (from 300th to top 50th in gold)

### A*
advanced A* pathfinding with snake length as decay, so need to jump from platform to platform to get higher than snake length

# tools
## replays DL
Q: any automated way to fetch replays of my battles? I want to use them for "unit tests and copying them one by one manually does not seem right

https://github.com/FrequentlyMissedDeadlines/cgsubmit

## cpp optimisations
* **Compiler Flags:** Forced `#pragma GCC optimize "Ofast, unroll-loops"` and target architectures (AVX2, BMI2).
```cpp
#pragma GCC optimize("Ofast,unroll-loops,omit-frame-pointer,inline")
#pragma GCC target("movbe,avx,avx2,fma,sse4.2,popcnt,bmi,bmi2,lzcnt")
```
* **Precomputed Math:** Inverse distances (`1.0f / dist`) are precomputed in a lookup table to turn costly divisions into fast array lookups during BFS and heuristic scoring.
* **Bitwise Limits:** Heavy use of bitwise operations for modulo (`& 16383` instead of `% 16384`) in circular queues.

## bots comparison
[cg-bundler](https://github.com/MathieuSoysal/CG-Bundler)
[psyleague](https://github.com/FakePsyho/psyleague)
[Codingame engine](https://github.com/CodinGame/codingame-game-engine)