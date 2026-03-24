# enemy modelling
We need at least one move ahead look up for enemies: their tails leave a space free and their heads can have only up to three positions surviving. put enemy head on those three positions at once, keep body, remove tail, that must be decent enemy foresight no?

test map: 
bot-development/test-maps/enemies/10b-deadly tunnel - foresee enemies.txt

# corridor cases

Conclusion on corridor regression:

The bot can see the apple path.
It cannot reliably see or preserve the escape after growth in a one-cell corridor pocket.
After the apple step, search collapses to expanded=1, then fallback repeats a doomed move.
So the regression is mainly “post-growth/post-apple escape planning in tight corridors”, not general opponent prediction.

test maps:
pathing/\*corridor\*.txt

# curl in 2x2 box
a snake must be able to know its tail will free a cell and follow it. 
bot-development/test-maps/complex-pathing/07-do-curl_up-angled-snake.txt

# hint
also trying to simulate the opponent, but not spending too much time, so i just pick the best opponent move combination for each of my combinations according to same evaluation function * -1