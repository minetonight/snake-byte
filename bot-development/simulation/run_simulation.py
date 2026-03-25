import subprocess
import re
import os

def parse_expected_scores(map_file):
    expected_p1 = None
    expected_p2 = None

    if not map_file:
        return expected_p1, expected_p2

    abs_map_path = os.path.abspath(map_file)
    if not os.path.exists(abs_map_path):
        return expected_p1, expected_p2

    with open(abs_map_path, 'r') as f:
        for line in f:
            if line.startswith("EXPECTED_SCORE_P1:"):
                expected_p1 = int(line.split(":")[1].strip())
            elif line.startswith("EXPECTED_SCORE_P2:"):
                expected_p2 = int(line.split(":")[1].strip())

    return expected_p1, expected_p2

def run_simulation(bot1_path, bot2_path, map_file=None, seed=None):
    # The directory where pom.xml is located
    winter_challenge_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../WinterChallenge2026-Exotec")
    
    # We pass the paths combined with ||| to handle spaces safely via Maven properties
    # mvn compile exec:java -Dexec.mainClass=HeadlessMain -Dexec.classpathScope=test -Dexec.args="python3 config/Boss.py|||python3 config/Boss.py" -q 
    # mvn compile exec:java -Dexec.mainClass=Main -Dexec.classpathScope=test -Dexec.args="python3 config/Boss.py|||python3 config/Boss.py|||6451822396277461000" -q 
    # mvn compile exec:java -Dexec.mainClass=Main -Dexec.classpathScope=test -Dexec.args="/home/aleks/Development/Python/snake-byte/bot-development/bots/epic4-solver-BFS-bot.exe|||/home/aleks/Development/Python/snake-byte/bot-development/bots/epic4-solver-BFS-bot.exe" -DcustomMapFile=/home/aleks/Development/Python/snake-byte/bot-development/test-maps/complex-pathing/11-bigmap-E45Sx-long-term-target.txt -q 
    # mvn compile exec:java -Dexec.mainClass=Main -Dexec.classpathScope=test -Dexec.args="/home/aleks/Development/Python/snake-byte/bot-development/bots/epic7-coop-reachable-frontier-bot.exe|||/home/aleks/Development/Python/snake-byte/bot-development/bots/epic7-coop-reachable-frontier-bot.exe" -DcustomMapFile=/home/aleks/Development/Python/snake-byte/bot-development/test-maps/pathing/08-scary-safe-apple.txt -q 
    # mvn compile exec:java -Dexec.mainClass=Main -Dexec.classpathScope=test -Dexec.args="/home/aleks/Development/Python/snake-byte/bot-development/bots/epic4.1-reachable-frontier-bot-prune-unsafe.exe|||/home/aleks/Development/Python/snake-byte/bot-development/bots/epic4-reachable-frontier-bot.exe" -DcustomMapFile=/home/aleks/Development/Python/snake-byte/bot-development/test-maps/multi-snake/12t-bigmap-E6Sx-team-long-term-many-targets.txt -q 
    command = [
        "mvn", "compile", "exec:java", 
        "-Dexec.mainClass=HeadlessMain", 
        "-Dexec.classpathScope=test",
        f'-Dexec.args={bot1_path}|||{bot2_path}|||{seed if seed is not None else ""}',
    ]

    expected_p1, expected_p2 = parse_expected_scores(map_file)

    if map_file:
        abs_map_path = os.path.abspath(map_file)
        print(f"\n[{os.path.basename(map_file)}]")
        command.append(f"-DcustomMapFile={abs_map_path}")
    # -DcustomMapFile=/home/aleks/Development/Python/snake-byte/bot-development/test-maps/complex-pathing/11-bigmap-E45Sx-long-term-target.txt

    command.append("-q")
    
    # Run the Java headless simulation from the correct directory
    process = subprocess.run(command, cwd=winter_challenge_dir, capture_output=True, text=True)
    
    output = process.stdout
    
    if process.returncode != 0:
        print("Error running simulation:")
        print(process.stderr)
        print("Stdout:", process.stdout)
        return 0, 0
    
    # Use Regex to extract the scores
    p1_match = re.search(r"Player 1 Score:\s*(-?\d+)", output)
    p2_match = re.search(r"Player 2 Score:\s*(-?\d+)", output)
    
    if not p1_match or not p2_match:
        print("❌ Could not find scores in output. Bot likely crashed or timed out!")
        print("--- STDOUT ---")
        print(output)
        print("--- STDERR ---")
        print(process.stderr)
        return -1, -1

    p1_score = int(p1_match.group(1))
    p2_score = int(p2_match.group(1))

    # Validation against expected score
    # Validation against expected score
    if expected_p1 is not None or expected_p2 is not None:
        if expected_p1 is not None:
            if p1_score < expected_p1:
                print(f"❌ FAILED P1: Expected {expected_p1}, got {p1_score}")
            elif p1_score > expected_p1:
                print(f"🔝 EXCEEDED P1: Expected {expected_p1}, got {p1_score}")  # new case 
            else:
                print(f"✅ PASSED P1: {p1_score}")

        if expected_p2 is not None:
            if p2_score < expected_p2:
                print(f"❌ FAILED P2: Expected {expected_p2}, got {p2_score}") 
            elif p2_score > expected_p2:
                print(f"🔝 EXCEEDED P2: Expected {expected_p2}, got {p2_score}")  # new case 
            else:
                print(f"✅ PASSED P2: {p2_score}")


    return p1_score, p2_score

def test_all_maps(bot1, bot2, subfolder=None):
    base_dir = os.path.dirname(os.path.abspath(__file__))
    test_maps_dir = os.path.join(base_dir, '../test-maps')
    if subfolder:
        test_maps_dir = os.path.join(test_maps_dir, subfolder)

    
    print(f"--- Running simulations for all maps in {test_maps_dir} ---")
    
    for filename in sorted(os.listdir(test_maps_dir)):
        if filename.endswith(".txt"):
            map_path = os.path.join(test_maps_dir, filename)
            scores = run_simulation(bot1, bot2, map_file=map_path)
            print(f"Final Scores (P1, P2): {scores}")

def manualMode():
    # Example usage:
    # Assuming you have two python files in the same directory as this script.
    # Note: Using absolute paths is safer since the working directory during execution is inside the java project.
    base_dir = os.path.dirname(os.path.abspath(__file__))
    bot1 = f"python3 {os.path.join(base_dir, 'python ../bots/my_bot_1.py')}"
    
    bot1 = f"{os.path.join(base_dir, '../bots/epic-2-hovering-flying-snakes.exe')}"
    bot1 = f"{os.path.join(base_dir, '../bots/epic2-solver-bot.exe')}"
    bot1 = f"{os.path.join(base_dir, '../bots/epic3-solver-bot.exe')}"
    bot1 = f"{os.path.join(base_dir, '../bots/epic4-solver-bot.exe')}"
    bot1 = f"{os.path.join(base_dir, '../bots/epic7-coop-reachable-frontier-bot.exe')}"

    # bot2 = "../bots/rightBoss.py" 
    # bot2 = "../bots/leftBoss.py" 
    bot2 = "../bots/Boss.py" 
    bot2 = f"python3 {os.path.join(base_dir, bot2)}"
    bot2 = bot1
    
    """ test all custom maps """
    # test_all_maps(bot1, bot2)
    test_all_maps(bot1, bot2, "complex-pathing")
    # test_all_maps(bot1, bot2, "coop")
    test_all_maps(bot1, bot2, "enemies")
    # test_all_maps(bot1, bot2, "multi-snake")
    test_all_maps(bot1, bot2, "pathing")
    test_all_maps(bot1, bot2, "tactics")
    

    """ test standart codingame engine """
    ###### for _ in range(5):
    #     print("Scores (P1, P2):", run_simulation(bot1, bot2))

    """ test single custom map simulation """
    # my_test_map = os.path.join(base_dir, '../test-maps/test_map_with2.txt')
    # print("Scores (P1, P2):", run_simulation(bot1, bot2, map_file=my_test_map))
    # my_test_map2 = os.path.join(base_dir, '../test-maps/test_map_with3.txt')
    # print("Scores (P1, P2):", run_simulation(bot1, bot2, map_file=my_test_map2))
    # angled_snake_map = os.path.join(base_dir, '../test-maps/test_map_with2-up-angled-snake.txt')
    # print("Scores (P1, P2):", run_simulation(bot1, bot2, map_file=angled_snake_map))

"""
usage
$ cd bot-development/simulation/
$ python3 run_simulation.py "/home/aleks/Development/Python/snake-byte/bot-development/your_new_bot.exe" "python3 /home/aleks/Development/Python/snake-byte/bot-development/bots/Boss.py" --map /home/aleks/Development/Python/snake-byte/bot-development/test-maps/test_map_with2-eating.txt
"""
if __name__ == "__main__":
    import argparse
    import sys
    
    if len(sys.argv) < 2:
        print("Less than two arguments given, running in manual mode: ")
        manualMode()
    else:
        parser = argparse.ArgumentParser(description="Run SnakeByte simulation.")
        parser.add_argument("bot1", help="Command and absolute Path for bot 1")
        parser.add_argument("bot2", help="Command and absolute Path for bot 2")
        parser.add_argument("--map", dest="customMap", help="Optional path to custom map file; disables --seed param")
        parser.add_argument("--seed", type=int, help="Optional seed for the simulation; disables --map param")
        
        args = parser.parse_args()
        
        if args.customMap and args.seed:
            print("Ambiguous call with map and seed - these arguments cannot be used together.")
            sys.exit(1)
        
        print("Scores (P1, P2):", run_simulation(args.bot1, args.bot2, map_file=args.customMap, seed=args.seed))
