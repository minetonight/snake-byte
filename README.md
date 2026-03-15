You must git pull here my fork of the engine, that enables custom maps for testing:
```bash
cd snake-byte
git clone https://github.com/minetonight/WinterChallenge2026-Exotec.git
```

# test if all maps are running
```bash
cd bot-developement/test-maps
./test-all-maps-with-boss.sh 
```

# test basic bots runninng on custom maps
```bash
cd ../../WinterChallenge2026-Exotec/
mvn compile exec:java -Dexec.mainClass=HeadlessMain -Dexec.classpathScope=test -DcustomMapFile=$(pwd)/../bot-development/test-maps/test_map_with2-eating.txt -Dexec.args="python ../bot-development/bots/Boss.py|||../bot-development/bots/bot-development/bots/test1-solver-bot.cpp"
```

## more tools 
see [usage.md](bot-development/test-maps/usage.md)

### related repos:
 - [gym-battlesnake](https://github.com/ArthurFirmino/gym-battlesnake)
 - [robosnake](https://github.com/smallsco/robosnake)
 - [sagemaker-battlesnake-ai](https://github.com/awslabs/sagemaker-battlesnake-ai)
