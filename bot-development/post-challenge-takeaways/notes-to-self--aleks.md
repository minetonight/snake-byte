# Humans still have bigger context than LLMs
LLM has 1 mil tokens, 4 MB of memory total. With 100KB src and game rules and Java engine and two replys and the conversation is compressed. 
So always stay in the loop and know what is being implemented.

Or make a fully no-human-in-the-loop flow.
No in between, giving directions and prompts without overview of the outputs.

# Ask explanations after each code implementation
Or at least at the end of an epic. AI agents share what they implement story by story, dont be lazy to accept all they do.
They take shortcuts and hardcode.

## Learn from the code and the challenge
The bots can do what the user can do, just faster.

# Don't assume correct engine recreation.
I had issues with the engine recreation, and found them at days 7 and on day 14 it still persisted. Double check core logic that will define the final results. 

I also had an issue with simulations that did not allow for head to follow tail, as it was not properly marked as emptied cell. That was another core assumption I missed early on.

