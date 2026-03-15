import subprocess
import re
import os
from mcp.server.fastmcp import FastMCP

# Create the MCP server
mcp = FastMCP("simulation-server")

@mcp.tool()
def run_simulation(bot1_path: str, bot2_path: str) -> str:
    """
    Run a match simulation between two bots for the WinterChallenge2026-Exotec.
    Extracts and returns the final scores for Player 1 and Player 2.
    
    Args:
        bot1_path: The command/path to run the first bot (e.g., 'python3 path/to/bot1.py')
        bot2_path: The command/path to run the second bot (e.g., 'python3 path/to/bot2.py')
    """
    # The directory where pom.xml is located
    # Based on the original script, it's relative to this new script's location
    winter_challenge_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "WinterChallenge2026-Exotec")
    
    # We pass the paths combined with ||| to handle spaces safely via Maven properties
    command = [
        "mvn", "compile", "exec:java", 
        "-Dexec.mainClass=HeadlessMain", 
        "-Dexec.classpathScope=test",
        f'-Dexec.args="{bot1_path}|||{bot2_path}"',
        "-q"
    ]
    
    try:
        # Run the Java headless simulation from the correct directory
        process = subprocess.run(command, cwd=winter_challenge_dir, capture_output=True, text=True)
        
        output = process.stdout
        
        if process.returncode != 0:
            error_msg = f"Error running simulation:\n{process.stderr}\nStdout: {process.stdout}"
            return error_msg
        
        # Use Regex to extract the scores
        p1_match = re.search(r"Player 1 Score:\s*(-?\d+)", output)
        p2_match = re.search(r"Player 2 Score:\s*(-?\d+)", output)
        
        p1_score = int(p1_match.group(1)) if p1_match else 0
        p2_score = int(p2_match.group(1)) if p2_match else 0
        
        return f"Match completed successfully.\nPlayer 1 Score: {p1_score}\nPlayer 2 Score: {p2_score}"
    except Exception as e:
        return f"An exception occurred while running the simulation: {str(e)}"

if __name__ == "__main__":
    # Starts the MCP server via stdio
    mcp.run(transport='stdio')
