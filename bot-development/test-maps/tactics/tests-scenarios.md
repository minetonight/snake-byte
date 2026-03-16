# SnakeByte – 15 High-Value Test Scenarios

These tactical scenarios are designed to expose weaknesses in SnakeByte bots such as:

- shallow search depth
- incorrect collision handling
- missing gravity cascade simulation
- greedy power-source strategies
- lack of territory evaluation

They can be implemented as **unit tests or scenario puzzles** for validating an AI agent.

---

# 1. Delayed Death Traps

## 1. Poison Powerup

**Setup**

Your snake is supported by a power source.  
Eating it removes the only support.

**Result**

After eating:
- snake grows
- the support disappears
- snake falls
- falls off map within 2–3 turns

**Correct behavior**

Do **NOT** eat the power source.

**Bots that fail**

Greedy bots maximizing immediate growth.

---

## 2. Enemy Poison Powerup

**Setup**

Enemy snake is supported by a power source.

If you eat it:
- enemy loses support
- enemy falls and dies

**Correct behavior**

Eat the power source even if enemy is closer.

**Bots that fail**

Bots optimizing only **distance to powerups**.

---

## 3. Double Delayed Collapse

**Setup**

Structure:


enemy
your snake
platform
power source
void


Eating the power removes support for both snakes.

But:
- enemy falls first
- you land on enemy

**Correct behavior**

Eat the power source.

**Bots that fail**

Bots that do not simulate **full gravity cascades**.

---

# 2. Head Collision Logic

## 4. Equal Head Collision

**Setup**

Two heads move into the same empty cell.

**Expected outcome**


both heads destroyed
new head becomes next body segment
snakes survive if length ≥ 3


**Bots that fail**

Bots that incorrectly kill both snakes.

---

## 5. Power Source Head Collision

**Setup**

Two heads collide on a cell containing a power source.

**Expected outcome**


both snakes eat power source (+1 length)
both lose head (-1 length)
net change = 0


**Bots that fail**

Bots giving the resource to only one snake.

---

## 6. Head vs Body Trade

**Setup**

Your head collides with enemy body.

**Expected outcome**


your head destroyed
enemy body unaffected


However this may still be beneficial if the new head position blocks the enemy.

**Bots that fail**

Bots assuming **any head loss is always bad**.

---

# 3. Territory Control Failures

## 7. Corridor Lock

**Setup**


########
#......#
#..P...#
#......#
########


Enemy reaches corridor first.

**Result**

Enemy blocks exit.

**Correct behavior**

Do NOT chase the power source.

**Bots that fail**

Bots without **space evaluation**.

---

## 8. Self-Territory Trap

**Setup**

Your snake creates a closed loop with a power source inside.

**Correct behavior**

Do not enter if available space < snake length.

**Bots that fail**

Bots without **reachable area evaluation**.

---

## 9. Choke Point Denial

**Setup**

Two map regions connected by one tile.

Most power sources are on enemy side.

**Correct behavior**

Occupy the choke point to deny access.

**Bots that fail**

Bots that always chase resources instead of **area control**.

---

# 4. Gravity Mechanics

## 10. Moving Platform Trap

**Setup**

Enemy snake is standing on top of your snake.

If you move away:


enemy loses support
enemy falls


**Correct behavior**

Move even if there is no immediate reward.

**Bots that fail**

Bots ignoring **support dependency**.

---

## 11. Gravity Cascade

**Setup**

Three layers:


enemy
your snake
platform


Removing the platform causes:


enemy falls
your snake falls


Gravity must repeat until stable.

**Bots that fail**

Bots applying gravity **only once**.

---

## 12. Fall Outcome Detection

**Setup**

Snake falls after support removal.

Two possible outcomes:

Case A:

lands on body → survives


Case B:

falls beyond grid → dies


**Correct behavior**

Simulate complete fall path.

**Bots that fail**

Bots that stop gravity simulation too early.

---

# 5. Resource Race Logic

## 13. Equal Distance Race

Both snakes reach a power source in the same turn.

Possible outcomes:


both go → head collision
both avoid → nobody gains


Correct decision depends on:
- snake length
- future board state
- nearby resources

**Bots that fail**

Bots with fixed rules like:


always chase
always avoid


---

## 14. Forced Intercept

Enemy reaches a power source faster.

But you can intercept the path before it reaches it.

**Correct behavior**
Block the path rather than racing.

**Bots that fail**
Bots evaluating **only target distance**.
---

# 6. Sacrificial Kill
## 15. Suicide Elimination

**Setup**


wall
enemy snake length 3
your snake length 4


You can collide with the enemy.

Result:
enemy eliminated
you lose head but survive


**Correct behavior**
Sacrifice head to eliminate opponent.

**Bots that fail**
Bots maximizing **immediate length only**.
