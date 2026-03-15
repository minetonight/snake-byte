import sys
import math

# Auto-generated code below aims at helping you parse
# the standard input according to the problem statement.

my_id = int(input())
width = int(input())
height = int(input())
for i in range(height):
    row = input()
snakebots_per_player = int(input())
for i in range(snakebots_per_player):
    my_snakebot_id = int(input())
for i in range(snakebots_per_player):
    opp_snakebot_id = int(input())

# game loop
while True:
    power_source_count = int(input())
    for i in range(power_source_count):
        x, y = [int(j) for j in input().split()]
    snakebot_count = int(input())
    for i in range(snakebot_count):
        inputs = input().split()
        snakebot_id = int(inputs[0])
        body = inputs[1]

    # Write an action using print
    # To debug: print("Debug messages...", file=sys.stderr, flush=True)

    # print("0 UP;1 UP;")
    print("WAIT")
