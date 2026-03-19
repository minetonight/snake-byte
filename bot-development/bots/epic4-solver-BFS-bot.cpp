#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <queue>
#include <algorithm>
#include <fstream>
#include <unistd.h>
#include <csignal>
#include <exception>
#include <cstdint>
#include <climits>
#include <unordered_map>
#include <unordered_set>
using namespace std;
using namespace std::chrono;

#ifndef BOT_LOG_PREFIX
#define BOT_LOG_PREFIX "epic4_bfs_bot_log_"
#endif

#ifndef EPIC4_ALGO_MODE
#define EPIC4_ALGO_MODE 0
#endif

struct BotModeConfig {
    static constexpr int HYBRID = 0;
    static constexpr int DEEP_ONLY = 1;
    static constexpr int BFS_ONLY = 2;
    static constexpr int HEURISTIC_ONLY = 3;

    static constexpr bool PURE_DEEP_ONLY = (EPIC4_ALGO_MODE == DEEP_ONLY);
    static constexpr bool PURE_BFS_ONLY = (EPIC4_ALGO_MODE == BFS_ONLY);
    static constexpr bool PURE_HEURISTIC_ONLY = (EPIC4_ALGO_MODE == HEURISTIC_ONLY);
    static constexpr bool HYBRID_DEFAULT = (EPIC4_ALGO_MODE == HYBRID);
};

ofstream make_log_stream() {
    return ofstream(string(BOT_LOG_PREFIX) + to_string(getpid()) + ".txt", ios::app);
}

ofstream mylog = make_log_stream();

void crash_signal_handler(int sig) {
    cerr << "FATAL_SIGNAL " << sig << endl;
    mylog << "FATAL_SIGNAL " << sig << '\n';
    mylog.flush();
    _exit(128 + sig);
}

void bot_terminate_handler() {
    cerr << "UNCAUGHT_EXCEPTION" << endl;
    mylog << "UNCAUGHT_EXCEPTION\n";
    mylog.flush();
    abort();
}

// --- TIME MANAGEMENT ---
auto turn_start_time = high_resolution_clock::now();

struct BotTuning {
    static constexpr int TURN_TOTAL_MS_BUDGET = 73;
    static constexpr bool SIMPLE_LONG_RANGE_PATHING = true;
    static constexpr int TURN_WRAPUP_BUFFER_PCT = 5;
    static constexpr int TURN_PHASE_FORWARD_BFS_PCT = 15;
    static constexpr int TURN_PHASE_BACKWARD_BFS_PCT = 20;
    static constexpr int TURN_PHASE_LOCAL_COMBAT_PCT = 20;
    static constexpr int TURN_PHASE_POWER_PLANNER_PCT = 25;
    static constexpr int TURN_PHASE_PATH_AND_SCORING_PCT = 15;
    static constexpr int TURN_MIN_UTILIZATION_PCT = 90;
    static constexpr int LOCAL_COMBAT_LENGTH_WEIGHT = 140;
    static constexpr int LOCAL_COMBAT_LOST_SEGMENT_WEIGHT = 220;
    static constexpr int LOCAL_COMBAT_H2H_BONUS = 500;
    static constexpr int LOCAL_COMBAT_HDIST_PENALTY = 20;

    static constexpr int DANGER_HARD_BASE_PENALTY = 14000;
    static constexpr int DANGER_HARD_WEIGHT = 1200;
    static constexpr int DANGER_SOFT_WEIGHT = 600;
    static constexpr int FLOODFILL_DEATH_PENALTY = 20000;
    static constexpr int FLOODFILL_SURVIVE_BONUS = 200;
    static constexpr int POWERUP_BONUS = 32000;
    static constexpr int ENCLOSED_POWERUP_PENALTY = 100000;

    static constexpr int VORONOI_LENGTH_DELTA_WEIGHT = 50;
    static constexpr int VORONOI_MY_EXCLUSIVE_WEIGHT = 20;
    static constexpr int VORONOI_OPP_EXCLUSIVE_WEIGHT = 20;

    static constexpr int ROLE_COLLECTOR_POWER_BONUS = 7000;
    static constexpr int ROLE_COLLECTOR_NEAR_ENEMY_PENALTY = 2200;
    static constexpr int ROLE_SUPPORT_NEAR_ALLY_BONUS = 1800;
    static constexpr int ROLE_SUPPORT_NEAR_ALLY_FALLOFF = 350;
    static constexpr int ROLE_SUPPORT_POWER_NEAR_ALLY_PENALTY = 1400;
    static constexpr int ROLE_DEFENDER_ENEMY_PROX_BONUS = 1600;
    static constexpr int ROLE_DEFENDER_RETREAT_PENALTY = 900;
    static constexpr int ROLE_SUFFOCATOR_APPROACH_BONUS = 1500;
    static constexpr int ROLE_SUFFOCATOR_CLOSE_BONUS = 700;
    static constexpr int ROLE_KILLER_SHORT_ENEMY_BONUS = 2800;
    static constexpr int ROLE_KILLER_SHORT_ENEMY_FALLOFF = 600;
    static constexpr int ROLE_KILLER_POWER_PENALTY = 800;

    static constexpr int FOLLOWUP_COUNT_WEIGHT = 3000;
    static constexpr int FOLLOWUP_ZERO_PENALTY = 20000;
    static constexpr int FOLLOWUP_ONE_PENALTY = 9000;
    static constexpr int ADJ_POWERUP_BONUS = 7000;
    static constexpr int SHORTER_SNAKE_FUTURE_PENALTY = 20000;
    static constexpr int SAME_STATE_OPPORTUNITY_PENALTY = 20000;
    static constexpr int SIM_DEATH_PENALTY = 50000;
    static constexpr int SIDE_EXIT_PENALTY = 35000;
    static constexpr int FLOOR_EXIT_PENALTY = 50000;
    static constexpr int OPEN_EDGE_EXIT_PENALTY = 25000;

    static constexpr int DELAYED_DEATH_PENALTY = 70000;
    static constexpr int DELAYED_LOW_FOLLOWUP_PENALTY = 13000;
    static constexpr int DELAYED_NO_FOLLOWUP_PENALTY = 28000;

    static constexpr int TARGET_COLLECTOR_DIST_WEIGHT = 170;
    static constexpr int TARGET_SUPPORT_DIST_WEIGHT = 120;
    static constexpr int TARGET_REACHED_BONUS = 1600;
    static constexpr int SUPPORT_ROTATE_AWAY_BONUS = 2200;
    static constexpr int SUPPORT_STAY_NEAR_COLLECT_PENALTY = 3200;
    static constexpr int DANGER_MAP_PROJECTION_DEPTH = 3;
};

enum TurnPhaseId {
    PHASE_FORWARD_BFS = 0,
    PHASE_BACKWARD_BFS = 1,
    PHASE_LOCAL_COMBAT = 2,
    PHASE_POWER_PLANNER = 3,
    PHASE_PATH_AND_SCORING = 4,
    PHASE_COUNT = 5,
};

static constexpr const char* kPhaseNames[PHASE_COUNT] = {
    "forward_bfs",
    "backward_bfs",
    "local_combat",
    "power_planner",
    "path_scoring"
};

int g_turn_hard_deadline_ms = 69;
int g_current_phase_deadline_ms = INT_MAX;
int g_phase_budget_ms[PHASE_COUNT] = {0};
long long g_phase_used_us[PHASE_COUNT] = {0};
int g_phase_cumulative_deadline_ms[PHASE_COUNT] = {0};

static int elapsed_turn_ms() {
    auto now = high_resolution_clock::now();
    return static_cast<int>(duration_cast<milliseconds>(now - turn_start_time).count());
}

static void begin_turn_timing_budget() {
    int wrapup_ms = (BotTuning::TURN_TOTAL_MS_BUDGET * BotTuning::TURN_WRAPUP_BUFFER_PCT) / 100;
    int allocated_ms = BotTuning::TURN_TOTAL_MS_BUDGET - wrapup_ms;
    if (allocated_ms < 1) allocated_ms = 1;

    const int phase_pct[PHASE_COUNT] = {
        BotTuning::TURN_PHASE_FORWARD_BFS_PCT,
        BotTuning::TURN_PHASE_BACKWARD_BFS_PCT,
        BotTuning::TURN_PHASE_LOCAL_COMBAT_PCT,
        BotTuning::TURN_PHASE_POWER_PLANNER_PCT,
        BotTuning::TURN_PHASE_PATH_AND_SCORING_PCT,
    };

    int used_alloc = 0;
    for (int i = 0; i < PHASE_COUNT; ++i) {
        g_phase_budget_ms[i] = (BotTuning::TURN_TOTAL_MS_BUDGET * phase_pct[i]) / 100;
        used_alloc += g_phase_budget_ms[i];
        g_phase_used_us[i] = 0;
    }

    while (used_alloc > allocated_ms) {
        int pick = -1;
        int max_budget = -1;
        for (int i = 0; i < PHASE_COUNT; ++i) {
            if (g_phase_budget_ms[i] > max_budget && g_phase_budget_ms[i] > 1) {
                max_budget = g_phase_budget_ms[i];
                pick = i;
            }
        }
        if (pick == -1) break;
        g_phase_budget_ms[pick]--;
        used_alloc--;
    }

    while (used_alloc < allocated_ms) {
        int pick = 0;
        for (int i = 1; i < PHASE_COUNT; ++i) {
            if (g_phase_budget_ms[i] < g_phase_budget_ms[pick]) pick = i;
        }
        g_phase_budget_ms[pick]++;
        used_alloc++;
    }

    int cumulative = 0;
    for (int i = 0; i < PHASE_COUNT; ++i) {
        cumulative += g_phase_budget_ms[i];
        g_phase_cumulative_deadline_ms[i] = cumulative;
    }

    g_turn_hard_deadline_ms = BotTuning::TURN_TOTAL_MS_BUDGET - 1;
    g_current_phase_deadline_ms = g_turn_hard_deadline_ms;
}

struct PhaseBudgetScope {
    int prev_deadline;
    int phase_idx;
    high_resolution_clock::time_point start_tp;

    explicit PhaseBudgetScope(TurnPhaseId phase)
        : prev_deadline(g_current_phase_deadline_ms), phase_idx(static_cast<int>(phase)), start_tp(high_resolution_clock::now()) {
        int phase_deadline = g_phase_cumulative_deadline_ms[phase_idx];
        if (phase_deadline <= 0) phase_deadline = g_turn_hard_deadline_ms;
        g_current_phase_deadline_ms = min(prev_deadline, phase_deadline);
    }

    ~PhaseBudgetScope() {
        long long spent = duration_cast<microseconds>(high_resolution_clock::now() - start_tp).count();
        if (spent > 0) g_phase_used_us[phase_idx] += spent;
        g_current_phase_deadline_ms = prev_deadline;
    }
};

bool out_of_time() {
    int ms_passed = elapsed_turn_ms();
    int deadline_ms = min(g_turn_hard_deadline_ms, g_current_phase_deadline_ms);
    return ms_passed >= deadline_ms;
}

bool has_planner_time_budget(int ms_limit) {
    int ms_passed = elapsed_turn_ms();
    int deadline_ms = min(g_turn_hard_deadline_ms, g_current_phase_deadline_ms);
    return ms_passed < min(ms_limit, deadline_ms);
}

struct GameState;
static int shortest_path_distance_walls_only(const GameState& state, int start_pos, int goal_pos);

unordered_map<int, int> g_persistent_target_by_snake;
unordered_map<int, bool> g_persistent_target_is_power;
unordered_map<uint64_t, int> g_blocked_target_until_turn;
unordered_map<int, int> g_target_progress_last_target;
unordered_map<int, int> g_target_progress_last_dist;
unordered_map<int, int> g_target_progress_stall_turns;
unordered_map<int, int> g_target_same_target_turns;
int g_turn_counter = 0;

struct SnakePathCache {
    int target_pos = -1;
    bool target_is_power = false;
    int expected_start_head = -1;
    int next_step = 0;
    int last_dist = 999999;
    int stall_turns = 0;
    int exact_retry_cooldown = 0;
    int last_head_pos = -1;
    int no_move_turns = 0;
    int same_target_turns = 0;
    vector<int> actions;
    vector<int> expected_heads;
};

unordered_map<int, SnakePathCache> g_path_cache_by_snake;

static uint64_t make_snake_target_key(int snake_id, int target_pos) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(snake_id)) << 32) |
           static_cast<uint32_t>(target_pos);
}

static bool is_target_temporarily_blocked(int snake_id, int target_pos) {
    auto it = g_blocked_target_until_turn.find(make_snake_target_key(snake_id, target_pos));
    if (it == g_blocked_target_until_turn.end()) return false;
    return it->second > g_turn_counter;
}

static void block_target_for_turns(int snake_id, int target_pos, int turns) {
    if (target_pos < 0) return;
    g_blocked_target_until_turn[make_snake_target_key(snake_id, target_pos)] = g_turn_counter + max(1, turns);
}

static void clear_target_progress_state(int snake_id) {
    g_target_progress_last_target.erase(snake_id);
    g_target_progress_last_dist.erase(snake_id);
    g_target_progress_stall_turns.erase(snake_id);
    g_target_same_target_turns.erase(snake_id);
}

// --- CONSTANTS ---
constexpr int16_t CELL_WALL = -1;
constexpr int16_t CELL_EMPTY = 0;
constexpr int16_t CELL_POWERUP = 3;
constexpr int16_t CELL_SNAKE_BASE = 10; // Snake IDs are 10 + id

// --- DYNAMIC DIMENSIONS ---
int world_width;
int world_height;
int total_powerups_count;
int max_len;
int max_width;
int max_height;
int grid_size;
bool map_has_open_left_edge = false;
bool map_has_open_right_edge = false;
bool map_has_open_floor_edge = false;

struct Snake {
    int id;
    int length;
    int head_idx;
    int tail_idx;
    bool is_alive;
    // Circular buffer to hold body coordinates (1D indices).
    // Initialized to max possible length to avoid reallocation.
    vector<int> body; 
};

inline int ring_size(const Snake& s) {
    return static_cast<int>(s.body.size());
}

inline int infer_previous_action(const Snake& s) {
    if (s.length > 1) {
        int h_pos = s.body[s.head_idx];
        int n_pos = s.body[(s.head_idx + 1) % ring_size(s)];

        if (h_pos == n_pos - max_width) return 0; // UP
        if (h_pos == n_pos + max_width) return 1; // DOWN
        if (h_pos == n_pos - 1) return 2; // LEFT
        if (h_pos == n_pos + 1) return 3; // RIGHT
    }
    return 0; // Default UP for length-1 snake
}

inline const char* action_to_string(int action) {
    if (action == 0) return "UP";
    if (action == 1) return "DOWN";
    if (action == 2) return "LEFT";
    if (action == 3) return "RIGHT";
    return "UP";
}

inline int opposite_action(int action) {
    if (action == 0) return 1;
    if (action == 1) return 0;
    if (action == 2) return 3;
    if (action == 3) return 2;
    return action;
}

inline bool is_backward_action(const Snake& s, int action) {
    if (s.length <= 1) return false;
    return action == opposite_action(infer_previous_action(s));
}

inline uint64_t snake_body_hash(const Snake& s) {
    uint64_t h = 1469598103934665603ULL;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(s.length));
    h *= 1099511628211ULL;
    for (int i = 0; i < s.length; ++i) {
        int pos = s.body[(s.head_idx + i) % ring_size(s)];
        h ^= static_cast<uint64_t>(static_cast<uint32_t>(pos + 0x9e3779b9));
        h *= 1099511628211ULL;
    }
    return h;
}

struct GameState {
    vector<int16_t> grid;
    vector<Snake> my_snakes;
    vector<Snake> opp_snakes;

    // --- PHYSICS ENGINE ---
    
    // Check if a single cell touches the ground or another grounded element
    inline bool is_cell_grounded(int pos, const vector<bool>& grounded_snakes) const {
        int below_idx = pos + max_width; // Y+1
        if (below_idx >= grid_size) return true; // Safety floor
        
        int16_t below_cell = grid[below_idx];
        if (below_cell == CELL_WALL || below_cell == CELL_POWERUP) return true;
        
        if (below_cell >= CELL_SNAKE_BASE) {
            int snake_id = below_cell - CELL_SNAKE_BASE;
            // Java Engine strictly prevents a bird from supporting ITSELF in mid-air
            // A snake cannot be physically grounded by resting on its own tail
            if (grounded_snakes[snake_id]) return true;
        }
        return false;
    }

    inline void apply_gravity() {
        bool something_fell = true;
        
        // Use an array indexed by snake ID to track grounded states efficiently.
        // Assuming max snake ID is ~2000 per engine. 
        // We can just size it based on max possible ID.
        // For codingame we can use a small map or assume max IDs < 10000.
        // To be safe and fast, evaluate grounding iteratively over the live snakes vector.
        
        int infinite_loop_guard = 0;
        int airborne_debug_size = 0;
        while (something_fell) {
            infinite_loop_guard++;
            if (infinite_loop_guard > 50) {
                cerr << "INFINITE LOOP DETECTED IN GRAVITY! airborne size=" << airborne_debug_size << endl;
                break;
            }
            something_fell = false;
            
            // Gather all live airborne snakes
            vector<Snake*> airborne;
            for (auto& s : my_snakes) if (s.is_alive) airborne.push_back(&s);
            for (auto& s : opp_snakes) if (s.is_alive) airborne.push_back(&s);
            airborne_debug_size = airborne.size();
            
            vector<bool> grounded(10000, false); // Quick lookup for grounded IDs
            bool something_got_grounded = true;
            
            // 1. Resolve Transitive Grounding (Identical to Java engine)
            while (something_got_grounded) {
                something_got_grounded = false;
                
                for (auto it = airborne.begin(); it != airborne.end();) {
                    Snake* s = *it;
                    bool is_s_grounded = false;
                    for (int i = 0; i < s->length; ++i) {
                        int pos = s->body[(s->head_idx + i) % ring_size(*s)];
                        if (is_cell_grounded(pos, grounded)) {
                            is_s_grounded = true;
                            break;
                        }
                    }
                    
                    if (is_s_grounded) {
                        grounded[s->id] = true;
                        something_got_grounded = true;
                        // Remove from airborne
                        it = airborne.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            
            // 2. Erase grid representation of airborne snakes before moving them
            // This prevents them from tripping over each other's old positions as they fall simultaneously
            for (Snake* s : airborne) {
                for (int i = 0; i < s->length; ++i) {
                    int pos = s->body[(s->head_idx + i) % ring_size(*s)];
                    if (pos < grid_size) {
                        grid[pos] = CELL_EMPTY;
                    }
                }
            }
            
            // 3. Fall all airborne snakes uniformly
            for (Snake* s : airborne) {
                something_fell = true;
                
                // Shift all body parts down
                for (int i = 0; i < s->length; ++i) {
                    int b_idx = (s->head_idx + i) % ring_size(*s);
                    s->body[b_idx] += max_width; 
                }
                
                // Java Out-of-bounds parity: ONLY kill if ALL parts are off the bottom of the map
                // In Java: bird.body.stream().allMatch(part -> part.getY() >= grid.height + 1)
                // In our physics: mapped `world_height` starts at Y=`max_len`. 
                // Bottom of world is `max_len + world_height`. Java checks >= height + 1 (meaning fully off).
                // Translating: Y >= max_len + world_height + 1. Which simplifies to Y >= max_height - max_len + 1.
                bool all_out = true;
                for (int i = 0; i < s->length; ++i) {
                    int pos = s->body[(s->head_idx + i) % ring_size(*s)];
                    int y = pos / max_width;
                    if (y < max_len + world_height + 1) { // 1 padding row below map is valid
                        all_out = false;
                        break;
                    }
                }
                
                if (all_out) {
                    s->is_alive = false;
                }
            }
            
            // 4. Restamp living airborne snakes back to grid
            for (Snake* s : airborne) {
                if (s->is_alive) {
                    for (int i = 0; i < s->length; ++i) {
                        int pos = s->body[(s->head_idx + i) % ring_size(*s)];
                        if (pos < grid_size) {
                            grid[pos] = CELL_SNAKE_BASE + s->id;
                        }
                    }
                }
            }
        }
    }
    
    // 0: UP, 1: DOWN, 2: LEFT, 3: RIGHT, 4: WAIT
    inline void apply_movement(const vector<int>& my_actions, const vector<int>& opp_actions) {
        auto move_snakes = [&](vector<Snake>& snakes, const vector<int>& actions) {
            for (size_t i = 0; i < snakes.size(); ++i) {
                Snake& s = snakes[i];
                if (!s.is_alive) continue;
                
                int action = actions[i];
                if (action == 4) { // WAIT: maintain previous direction
                    // Direction is inferred from difference between head and neck
                    if (s.length > 1) {
                        int h_pos = s.body[s.head_idx];
                        int n_pos = s.body[(s.head_idx + 1) % ring_size(s)];
                        
                        if (h_pos == n_pos - max_width) action = 0; // UP
                        else if (h_pos == n_pos + max_width) action = 1; // DOWN
                        else if (h_pos == n_pos - 1) action = 2; // LEFT
                        else if (h_pos == n_pos + 1) action = 3; // RIGHT
                    } else {
                        action = 0; // Default UP if length 1 and WAIT
                    }
                }
                
                int h_pos = s.body[s.head_idx];
                int hx = h_pos % max_width;
                int hy = h_pos / max_width;
                
                int n_hx = hx;
                int n_hy = hy;
                
                if (action == 0) n_hy -= 1; // UP
                else if (action == 1) n_hy += 1; // DOWN
                else if (action == 2) n_hx -= 1; // LEFT
                else if (action == 3) n_hx += 1; // RIGHT
                
                // Note: Snake can legally go out of bounds UP/LEFT/RIGHT, 
                // but gravity handles out of bounds DOWN. 
                // Bounds enforcement technically just destroys the snake if it hits grid borders 
                // since they are padded with enough empty space to not happen legally.
                
                int n_pos = n_hy * max_width + n_hx;
                
                int tail_pos = s.body[s.tail_idx];
                
                // Shift head pointer backward (circularly)
                int new_head_idx = (s.head_idx - 1 + ring_size(s)) % ring_size(s);
                s.body[new_head_idx] = n_pos; // Set new head here
                
                // Shift tail pointer backward (effectively cutting off old tail)
                s.tail_idx = (s.tail_idx - 1 + ring_size(s)) % ring_size(s);
                s.head_idx = new_head_idx;
                
                // Update grid: remove old tail, place new head
                // Wait on updating new head because of collisions and powerups!
                // For now, only remove tail from grid to clear it for others moves
                grid[tail_pos] = CELL_EMPTY;
            }
        };
        
        move_snakes(my_snakes, my_actions);
        move_snakes(opp_snakes, opp_actions);
    }

    // Helper function to resolve collisions and powerups
    inline void resolve_collisions() {
        // "These collisions are resolved simultaneously for all snakebots."
        
        // 1. Gather all new head positions
        vector<Snake*> all_snakes;
        for (auto& s : my_snakes) if (s.is_alive) all_snakes.push_back(&s);
        for (auto& s : opp_snakes) if (s.is_alive) all_snakes.push_back(&s);
        
        vector<int> head_positions(all_snakes.size());
        vector<bool> to_destroy(all_snakes.size(), false);
        vector<bool> ate_powerup(all_snakes.size(), false);
        
        for (size_t i = 0; i < all_snakes.size(); ++i) {
            head_positions[i] = all_snakes[i]->body[all_snakes[i]->head_idx];
        }
        
        // 2. Check what each head landed on
        for (size_t i = 0; i < all_snakes.size(); ++i) {
            Snake* s = all_snakes[i];
            int h_pos = head_positions[i];
            
            // Check grid bounds death (safety)
            if (h_pos < 0 || h_pos >= grid_size) {
                to_destroy[i] = true;
                continue;
            }
            
            int cell_val = grid[h_pos];
            if (cell_val == CELL_WALL || cell_val >= CELL_SNAKE_BASE) {
                // Hit wall or body
                to_destroy[i] = true;
            } else if (cell_val == CELL_POWERUP) {
                ate_powerup[i] = true;
            }
            
            // Head-to-Head collisions on empty or powerup cells
            for (size_t j = i + 1; j < all_snakes.size(); ++j) {
                if (head_positions[i] == head_positions[j]) {
                    to_destroy[i] = true;
                    to_destroy[j] = true;
                    // BUT if it's a powerup:
                    // "If multiple snakebot heads collide on a cell containing a power source, 
                    // that power source is considered eaten by each of those snakebots!
                    // In the same turn each snake receives +1 size... but loses -1 size by being beheaded."
                    // -> Handled by setting BOTH ate_powerup and to_destroy to true!
                }
            }
        }
        
        // 3. Apply Destruction and Growth
        for (size_t i = 0; i < all_snakes.size(); ++i) {
            Snake* s = all_snakes[i];
            
            if (ate_powerup[i]) {
                // Grow: The tail does not move this turn (we basically revert the tail cut)
                // wait, we already cut the tail in movement phase, so to grow we just add a tail segment back
                // To do this simply: shift tail_idx forward
                s->tail_idx = (s->tail_idx + 1) % ring_size(*s);
                s->length++;
                // We'll restamp the tail underneath
            }
            
            if (to_destroy[i]) {
                // Head is destroyed
                s->length--;
                // New head is the next segment
                s->head_idx = (s->head_idx + 1) % ring_size(*s);
                
                if (s->length < 3) {
                    s->is_alive = false;
                    // Clear entirely from grid
                    for (int k = 0; k < s->length; ++k) {
                        int b_idx = (s->head_idx + k) % ring_size(*s);
                        grid[s->body[b_idx]] = CELL_EMPTY;
                    }
                }
            }
        }
        
        // 4. Clear consumed powerups
        for (size_t i = 0; i < all_snakes.size(); ++i) {
            if (ate_powerup[i]) {
                grid[head_positions[i]] = CELL_EMPTY;
            }
        }
        
        // 5. Restamp all living snake bodies to grid
        for (auto* s : all_snakes) {
            if (!s->is_alive) continue;
            for (int k = 0; k < s->length; ++k) {
                int b_idx = (s->head_idx + k) % ring_size(*s);
                grid[s->body[b_idx]] = CELL_SNAKE_BASE + s->id;
            }
        }
    }
    
    // Simulates one entire turn
    inline void simulate(const vector<int>& my_actions, const vector<int>& opp_actions) {
        apply_movement(my_actions, opp_actions);
        apply_gravity();
        resolve_collisions();
    }
    
    // --- EVALUATION ENGINE ---
    
    struct VoronoiResult {
        int my_exclusive_powerups = 0; 
        int opp_exclusive_powerups = 0;
        int contested_powerups = 0;
        int length_delta = 0; // expected length advantage
    };

    VoronoiResult calculate_voronoi() const {
        VoronoiResult result;
        
        // Single BFS queue doing multi-source expansion.
        // Format: {distance, {player (0=me, 1=opp), pos}}
        queue<pair<int, pair<int, int>>> q;
        
        // Track distances per player. We don't need a massive array per player, 
        // just an array tracking the shortest distance and who reached it.
        // grid_size array where value is:
        //  Lower 16 bits = min distance
        //  Upper 16 bits = owner (0 = me, 1 = opp, 2 = contested)
        vector<uint32_t> visited(grid_size, 0xFFFFFFFF);
        
        // Push all heads
        for (const auto& s : my_snakes) {
            if (!s.is_alive) continue;
            int h_pos = s.body[s.head_idx];
            q.push({0, {0, h_pos}});
            visited[h_pos] = 0; // Dist 0, Player 0
        }
        for (const auto& s : opp_snakes) {
            if (!s.is_alive) continue;
            int h_pos = s.body[s.head_idx];
            q.push({0, {1, h_pos}});
            visited[h_pos] = (1 << 16) | 0; // Dist 0, Player 1
        }
        
        // standard 4-way movement
        int dx[] = {0, 0, -1, 1};
        int dy[] = {-1, 1, 0, 0};
        
        while (!q.empty()) {
            if (out_of_time()) break;
            
            auto curr = q.front();
            q.pop();
            
            int dist = curr.first;
            int player = curr.second.first;
            int pos = curr.second.second;
            
            int cx = pos % max_width;
            int cy = pos / max_width;
            
            for (int i = 0; i < 4; ++i) {
                int nx = cx + dx[i];
                int ny = cy + dy[i];
                int n_pos = ny * max_width + nx;
                
                if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
                
                // Solid cells block BFS in SnakeByte except for Powerups
                int16_t cell = grid[n_pos];
                if (cell == CELL_WALL || cell >= CELL_SNAKE_BASE) continue;
                
                int new_dist = dist + 1;
                uint32_t existing = visited[n_pos];
                uint32_t ex_dist = existing & 0xFFFF;
                uint32_t ex_player = existing >> 16;
                
                if (new_dist < ex_dist) {
                    visited[n_pos] = (player << 16) | new_dist;
                    q.push({new_dist, {player, n_pos}});
                    
                    if (cell == CELL_POWERUP) {
                        if (player == 0) {
                            result.my_exclusive_powerups++;
                            result.length_delta++;
                        } else {
                            result.opp_exclusive_powerups++;
                            result.length_delta--;
                        }
                    }
                } else if (new_dist == ex_dist && ex_player != player && ex_player != 2) {
                    // Contested
                    visited[n_pos] = (2 << 16) | new_dist;
                    if (cell == CELL_POWERUP) {
                        // It was strictly given to whoever got here first in the queue.
                        // We need to undo their exclusive point and mark it contested.
                        if (ex_player == 0) {
                            result.my_exclusive_powerups--;
                            result.length_delta--;
                        } else {
                            result.opp_exclusive_powerups--;
                            result.length_delta++;
                        }
                        result.contested_powerups++;
                    }
                }
            }
        }
        return result;
    }

    // --- DECISION HEURISTICS ---

    // Flood fill to determine if moving to a specific start_pos leaves enough room
    // for `required_space` (usually length / 2 or full length).
    // Accounts for gravity: space must be accessible via legal moves including falling.
    bool survives_flood_fill(int start_pos, int required_space) const {
        if (required_space <= 1) return true; // trivial

        vector<bool> visited(grid_size, false);
        queue<int> q;
        q.push(start_pos);
        visited[start_pos] = true;

        int space_found = 1;

        int dx[] = {0, 0, -1, 1};
        int dy[] = {-1, 1, 0, 0};

        while (!q.empty()) {
            if (out_of_time()) return true; // Optimistic return if out of time
            
            int pos = q.front();
            q.pop();

            int cx = pos % max_width;
            int cy = pos / max_width;

            for (int i = 0; i < 4; ++i) {
                int nx = cx + dx[i];
                int ny = cy + dy[i];
                int n_pos = ny * max_width + nx;

                if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
                
                int16_t cell = grid[n_pos];
                // Can move into empty or powerup
                if (cell != CELL_EMPTY && cell != CELL_POWERUP) continue;

                if (!visited[n_pos]) {
                    visited[n_pos] = true;
                    space_found++;
                    if (space_found >= required_space) return true;
                    q.push(n_pos);
                    // Note: This basic flood fill doesn't perfectly model gravity during the fill itself,
                    // but it approximates if the general area has enough raw volume disconnected by walls/bodies.
                }
            }
        }
        return space_found >= required_space;
    }

    const Snake* find_my_snake_by_id(int snake_id) const {
        for (const auto& s : my_snakes) {
            if (s.id == snake_id) {
                return &s;
            }
        }
        return nullptr;
    }

    const Snake* find_opp_snake_by_id(int snake_id) const {
        for (const auto& s : opp_snakes) {
            if (s.id == snake_id) {
                return &s;
            }
        }
        return nullptr;
    }

    bool has_adjacent_powerup(int head_pos) const {
        int hx = head_pos % max_width;
        int hy = head_pos / max_width;
        int dx[] = {0, 0, -1, 1};
        int dy[] = {-1, 1, 0, 0};

        for (int i = 0; i < 4; ++i) {
            int nx = hx + dx[i];
            int ny = hy + dy[i];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
            if (grid[ny * max_width + nx] == CELL_POWERUP) {
                return true;
            }
        }
        return false;
    }

    int count_safe_followups(const Snake& s) const {
        if (!s.is_alive || s.length <= 0) return 0;

        int head_pos = s.body[s.head_idx];
        int hx = head_pos % max_width;
        int hy = head_pos / max_width;
        int safe_count = 0;
        int dx[] = {0, 0, -1, 1};
        int dy[] = {-1, 1, 0, 0};

        for (int a = 0; a < 4; ++a) {
            if (is_backward_action(s, a)) continue;
            int nx = hx + dx[a];
            int ny = hy + dy[a];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;

            int n_pos = ny * max_width + nx;
            int16_t next_cell = grid[n_pos];
            if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;

            if (survives_flood_fill(n_pos, max(2, s.length / 2))) {
                safe_count++;
            }
        }

        return safe_count;
    }
};

inline int first_legal_action_basic(const GameState& state, const Snake& s) {
    int head_pos = s.body[s.head_idx];
    int hx = head_pos % max_width;
    int hy = head_pos / max_width;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    for (int a = 0; a < 4; ++a) {
        if (is_backward_action(s, a)) continue;
        int nx = hx + dx[a];
        int ny = hy + dy[a];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
        int n_pos = ny * max_width + nx;
        int16_t next_cell = state.grid[n_pos];
        if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;
        return a;
    }
    return infer_previous_action(s);
}

static vector<int> infer_default_actions(const vector<Snake>& snakes) {
    vector<int> actions(snakes.size(), 0);
    for (size_t i = 0; i < snakes.size(); ++i) {
        actions[i] = infer_previous_action(snakes[i]);
    }
    return actions;
}

static int find_my_snake_index(const GameState& state, int snake_id) {
    for (size_t i = 0; i < state.my_snakes.size(); ++i) {
        if (state.my_snakes[i].id == snake_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static int find_opp_snake_index(const GameState& state, int snake_id) {
    for (size_t i = 0; i < state.opp_snakes.size(); ++i) {
        if (state.opp_snakes[i].id == snake_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

struct DangerMapResult {
    vector<uint16_t> danger_map;
    int high_risk_threshold = 2;
};

enum class SnakeRole {
    Collector = 0,
    Support = 1,
    Defender = 2,
    Suffocator = 3,
    Killer = 4
};

static DangerMapResult build_enemy_danger_map(const GameState& state, int projection_depth) {
    DangerMapResult result;
    result.danger_map.assign(grid_size, 0);

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    for (const auto& opp : state.opp_snakes) {
        if (!opp.is_alive || opp.length <= 0) continue;

        int start = opp.body[opp.head_idx];
        if (start < 0 || start >= grid_size) continue;
        result.danger_map[start] += static_cast<uint16_t>(projection_depth + 1);

        vector<int16_t> best_depth(grid_size, -1);
        queue<pair<int, int>> q;
        q.push({start, 0});
        best_depth[start] = 0;

        while (!q.empty()) {
            if (out_of_time()) return result;

            auto curr = q.front();
            q.pop();
            int pos = curr.first;
            int depth = curr.second;

            if (depth >= projection_depth) continue;

            int cx = pos % max_width;
            int cy = pos / max_width;

            for (int i = 0; i < 4; ++i) {
                int nx = cx + dx[i];
                int ny = cy + dy[i];
                if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;

                int n_pos = ny * max_width + nx;
                int16_t cell = state.grid[n_pos];
                if (cell == CELL_WALL || cell >= CELL_SNAKE_BASE) continue;

                int nd = depth + 1;
                if (best_depth[n_pos] != -1 && best_depth[n_pos] <= nd) continue;
                best_depth[n_pos] = static_cast<int16_t>(nd);

                result.danger_map[n_pos] += static_cast<uint16_t>(projection_depth + 1 - nd);
                q.push({n_pos, nd});
            }
        }
    }

    return result;
}

static vector<SnakeRole> assign_dynamic_roles(const GameState& state, const vector<int>& powerup_positions, int turn_counter) {
    vector<SnakeRole> roles(state.my_snakes.size(), SnakeRole::Defender);

    auto manhattan = [](int p1, int p2) {
        int x1 = p1 % max_width;
        int y1 = p1 / max_width;
        int x2 = p2 % max_width;
        int y2 = p2 / max_width;
        return abs(x1 - x2) + abs(y1 - y2);
    };

    for (size_t i = 0; i < state.my_snakes.size(); ++i) {
        const Snake& s = state.my_snakes[i];
        if (!s.is_alive || s.length <= 0) continue;

        int head = s.body[s.head_idx];
        int nearest_power = 999999;
        for (int p : powerup_positions) nearest_power = min(nearest_power, manhattan(head, p));

        int nearest_enemy = 999999;
        int nearest_short_enemy = 999999;
        for (const auto& opp : state.opp_snakes) {
            if (!opp.is_alive || opp.length <= 0) continue;
            int d = manhattan(head, opp.body[opp.head_idx]);
            nearest_enemy = min(nearest_enemy, d);
            if (opp.length <= 3) nearest_short_enemy = min(nearest_short_enemy, d);
        }

        int nearest_ally = 999999;
        for (size_t j = 0; j < state.my_snakes.size(); ++j) {
            if (i == j) continue;
            const Snake& ally = state.my_snakes[j];
            if (!ally.is_alive || ally.length <= 0) continue;
            nearest_ally = min(nearest_ally, manhattan(head, ally.body[ally.head_idx]));
        }

        SnakeRole role = SnakeRole::Defender;
        if (nearest_short_enemy <= 6) {
            role = SnakeRole::Killer;
        } else if (nearest_enemy <= 3) {
            role = SnakeRole::Suffocator;
        } else if (nearest_power <= 5) {
            role = SnakeRole::Collector;
        } else if (state.my_snakes.size() > 1 && nearest_ally <= 4) {
            role = SnakeRole::Support;
        }

        if (role == SnakeRole::Defender && state.my_snakes.size() > 1 && ((turn_counter + static_cast<int>(i)) & 1)) {
            role = SnakeRole::Support;
        }

        roles[i] = role;
    }

    return roles;
}

struct TargetPlanResult {
    vector<int> target_positions;
    vector<bool> target_is_power;
};

static int manhattan_dist_pos(int p1, int p2) {
    int x1 = p1 % max_width;
    int y1 = p1 / max_width;
    int x2 = p2 % max_width;
    int y2 = p2 / max_width;
    return abs(x1 - x2) + abs(y1 - y2);
}

static bool is_playable_cell(int pos) {
    if (pos < 0 || pos >= grid_size) return false;
    int x = (pos % max_width) - max_len;
    int y = (pos / max_width) - max_len;
    return x >= 0 && x < world_width && y >= 0 && y < world_height;
}

static bool is_reusable_target_cell(const GameState& state, int pos) {
    if (!is_playable_cell(pos)) return false;
    int16_t cell = state.grid[pos];
    return cell != CELL_WALL && cell < CELL_SNAKE_BASE;
}

static int gravity_settle_token(const vector<int16_t>& nav_grid, int pos) {
    if (!is_playable_cell(pos)) return -1;
    while (true) {
        int below = pos + max_width;
        if (below < 0 || below >= grid_size) return -1;
        if (!is_playable_cell(below)) return -1;
        int16_t below_cell = nav_grid[below];
        if (below_cell == CELL_WALL || below_cell == CELL_POWERUP) return pos;
        pos = below;
    }
}

static vector<int16_t> build_navigation_grid_without_snakes(const GameState& state) {
    vector<int16_t> nav_grid = state.grid;
    for (int i = 0; i < grid_size; ++i) {
        if (nav_grid[i] >= CELL_SNAKE_BASE) nav_grid[i] = CELL_EMPTY;
    }
    return nav_grid;
}

static GameState isolate_state_for_single_snake_planning(const GameState& state, int snake_id) {
    GameState isolated;
    isolated.grid = state.grid;

    const Snake* self = state.find_my_snake_by_id(snake_id);
    if (self == nullptr) return isolated;

    for (int i = 0; i < grid_size; ++i) {
        if (isolated.grid[i] >= CELL_SNAKE_BASE) {
            int occ_id = isolated.grid[i] - CELL_SNAKE_BASE;
            isolated.grid[i] = (occ_id == snake_id) ? CELL_EMPTY : CELL_WALL;
        }
    }

    isolated.my_snakes.push_back(*self);
    isolated.opp_snakes.clear();

    const Snake& own = isolated.my_snakes[0];
    for (int k = 0; k < own.length; ++k) {
        int b_idx = (own.head_idx + k) % ring_size(own);
        int pos = own.body[b_idx];
        if (pos >= 0 && pos < grid_size) isolated.grid[pos] = CELL_SNAKE_BASE + own.id;
    }

    return isolated;
}

static inline uint64_t hash_combine_u64(uint64_t seed, uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

static uint64_t encode_single_snake_plan_hash(const GameState& state, const Snake& s) {
    uint64_t h = 1469598103934665603ULL;
    h = hash_combine_u64(h, static_cast<uint64_t>(s.length));
    h = hash_combine_u64(h, static_cast<uint64_t>(s.head_idx));
    h = hash_combine_u64(h, static_cast<uint64_t>(s.tail_idx));
    for (int k = 0; k < s.length; ++k) {
        int b_idx = (s.head_idx + k) % ring_size(s);
        h = hash_combine_u64(h, static_cast<uint64_t>(s.body[b_idx] + 1));
    }
    for (int i = 0; i < grid_size; ++i) {
        if (state.grid[i] == CELL_POWERUP) {
            h = hash_combine_u64(h, static_cast<uint64_t>(i + 1000003));
        }
    }
    return h;
}

static bool build_exact_full_body_path_to_target(
    const GameState& state,
    const Snake& s,
    int target_pos,
    vector<int>& out_actions,
    vector<int>& out_heads
) {
    out_actions.clear();
    out_heads.clear();

    if (target_pos < 0 || target_pos >= grid_size) return false;
    GameState start_state = isolate_state_for_single_snake_planning(state, s.id);
    if (start_state.my_snakes.empty()) return false;

    const Snake& start_snake = start_state.my_snakes[0];
    int start_head = start_snake.body[start_snake.head_idx];
    if (start_head == target_pos) return false;

    struct SearchNode {
        GameState state;
        int parent_idx;
        int action;
        int head_pos;
        int g;
    };
    struct OpenNode {
        int f;
        int g;
        int idx;
    };
    struct OpenCmp {
        bool operator()(const OpenNode& a, const OpenNode& b) const {
            if (a.f != b.f) return a.f > b.f;
            return a.g > b.g;
        }
    };

    vector<SearchNode> nodes;
    nodes.reserve(1024);
    nodes.push_back({start_state, -1, -1, start_head, 0});

    unordered_map<uint64_t, int> best_g;
    best_g.reserve(2048);
    best_g[encode_single_snake_plan_hash(start_state, start_snake)] = 0;

    priority_queue<OpenNode, vector<OpenNode>, OpenCmp> open;
    open.push({manhattan_dist_pos(start_head, target_pos), 0, 0});

    int expansions = 0;
    int goal_idx = -1;
    const int max_expansions = 800;
    const int max_depth = 60;

    while (!open.empty()) {
        if (out_of_time()) return false;
        OpenNode cur_open = open.top();
        open.pop();

        SearchNode cur = nodes[cur_open.idx];
        const Snake& cur_snake = cur.state.my_snakes[0];
        uint64_t cur_key = encode_single_snake_plan_hash(cur.state, cur_snake);
        auto best_it = best_g.find(cur_key);
        if (best_it == best_g.end() || best_it->second != cur.g) continue;

        if (++expansions > max_expansions) break;
        if (cur.head_pos == target_pos) {
            goal_idx = cur_open.idx;
            break;
        }
        if (cur.g >= max_depth) continue;

        for (int a = 0; a < 4; ++a) {
            if (is_backward_action(cur_snake, a)) continue;

            GameState next_state = cur.state;
            vector<int> my_actions = infer_default_actions(next_state.my_snakes);
            vector<int> opp_actions;
            my_actions[0] = a;
            next_state.simulate(my_actions, opp_actions);

            const Snake* next_snake = next_state.find_my_snake_by_id(s.id);
            if (next_snake == nullptr || !next_snake->is_alive) continue;

            int next_head = next_snake->body[next_snake->head_idx];
            if (!is_playable_cell(next_head)) continue;

            uint64_t next_key = encode_single_snake_plan_hash(next_state, *next_snake);
            int next_g = cur.g + 1;
            auto it = best_g.find(next_key);
            if (it != best_g.end() && it->second <= next_g) continue;
            best_g[next_key] = next_g;

            int next_idx = static_cast<int>(nodes.size());
            nodes.push_back({next_state, cur_open.idx, a, next_head, next_g});
            int h = manhattan_dist_pos(next_head, target_pos);
            open.push({next_g + h, next_g, next_idx});
        }
    }

    if (goal_idx == -1) return false;

    vector<int> rev_actions;
    vector<int> rev_heads;
    for (int idx = goal_idx; idx != -1; idx = nodes[idx].parent_idx) {
        if (nodes[idx].action != -1) {
            rev_actions.push_back(nodes[idx].action);
            rev_heads.push_back(nodes[idx].head_pos);
        }
    }
    reverse(rev_actions.begin(), rev_actions.end());
    reverse(rev_heads.begin(), rev_heads.end());
    out_actions.swap(rev_actions);
    out_heads.swap(rev_heads);
    return !out_actions.empty();
}

static bool build_head_only_gravity_path_to_target(
    const GameState& state,
    const Snake& s,
    int target_pos,
    vector<int>& out_actions,
    vector<int>& out_heads
) {
    out_actions.clear();
    out_heads.clear();

    if (target_pos < 0 || target_pos >= grid_size) return false;
    int start_pos = s.body[s.head_idx];
    if (start_pos == target_pos) return false;

    vector<int16_t> nav_grid = build_navigation_grid_without_snakes(state);
    if (!is_playable_cell(start_pos) || !is_playable_cell(target_pos)) return false;
    if (nav_grid[target_pos] == CELL_WALL) return false;

    struct PlannerNode {
        int f;
        int g;
        int pos;
        int last_action;
    };
    struct PlannerCmp {
        bool operator()(const PlannerNode& a, const PlannerNode& b) const {
            if (a.f != b.f) return a.f > b.f;
            return a.g > b.g;
        }
    };

    const int action_count = 4;
    const int node_count = grid_size * action_count;
    const int INF = 1e9;
    vector<int> best_g(node_count, INF);
    vector<int> parent(node_count, -1);
    vector<int8_t> parent_action(node_count, -1);
    vector<int> head_after(node_count, -1);
    priority_queue<PlannerNode, vector<PlannerNode>, PlannerCmp> pq;

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    int start_last_action = infer_previous_action(s);
    int start_x = start_pos % max_width;
    int start_y = start_pos / max_width;

    int expansions = 0;
    for (int a = 0; a < 4; ++a) {
        if (s.length > 1 && a == opposite_action(start_last_action)) continue;
        int nx = start_x + dx[a];
        int ny = start_y + dy[a];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
        int n_pos = ny * max_width + nx;
        if (!is_playable_cell(n_pos)) continue;
        if (nav_grid[n_pos] == CELL_WALL) continue;
        int settled = gravity_settle_token(nav_grid, n_pos);
        if (settled == -1) continue;
        int idx = settled * action_count + a;
        int g = 1;
        int h = manhattan_dist_pos(settled, target_pos);
        if (g < best_g[idx]) {
            best_g[idx] = g;
            parent[idx] = -1;
            parent_action[idx] = static_cast<int8_t>(a);
            head_after[idx] = settled;
            pq.push({g + h, g, settled, a});
        }
    }

    int goal_idx = -1;
    while (!pq.empty()) {
        if (out_of_time()) return false;
        PlannerNode cur = pq.top();
        pq.pop();
        int cur_idx = cur.pos * action_count + cur.last_action;
        if (cur.g != best_g[cur_idx]) continue;
        if (++expansions > 6000) break;
        if (cur.pos == target_pos) {
            goal_idx = cur_idx;
            break;
        }

        int cx = cur.pos % max_width;
        int cy = cur.pos / max_width;
        for (int a = 0; a < 4; ++a) {
            if (a == opposite_action(cur.last_action)) continue;
            int nx = cx + dx[a];
            int ny = cy + dy[a];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
            int n_pos = ny * max_width + nx;
            if (!is_playable_cell(n_pos)) continue;
            if (nav_grid[n_pos] == CELL_WALL) continue;
            int settled = gravity_settle_token(nav_grid, n_pos);
            if (settled == -1) continue;

            int next_g = cur.g + 1;
            if (next_g > 160) continue;
            int next_idx = settled * action_count + a;
            if (next_g >= best_g[next_idx]) continue;

            best_g[next_idx] = next_g;
            parent[next_idx] = cur_idx;
            parent_action[next_idx] = static_cast<int8_t>(a);
            head_after[next_idx] = settled;
            int h = manhattan_dist_pos(settled, target_pos);
            pq.push({next_g + h, next_g, settled, a});
        }
    }

    if (goal_idx == -1) return false;

    vector<int> rev_actions;
    vector<int> rev_heads;
    for (int idx = goal_idx; idx != -1; idx = parent[idx]) {
        rev_actions.push_back(parent_action[idx]);
        rev_heads.push_back(head_after[idx]);
    }
    reverse(rev_actions.begin(), rev_actions.end());
    reverse(rev_heads.begin(), rev_heads.end());
    out_actions.swap(rev_actions);
    out_heads.swap(rev_heads);
    return !out_actions.empty();
}

static bool build_gravity_aware_path_to_target(
    const GameState& state,
    const Snake& s,
    int target_pos,
    vector<int>& out_actions,
    vector<int>& out_heads
) {
    int dist = manhattan_dist_pos(s.body[s.head_idx], target_pos);
    if (dist <= 16 && build_exact_full_body_path_to_target(state, s, target_pos, out_actions, out_heads)) {
        return true;
    }
    return build_head_only_gravity_path_to_target(state, s, target_pos, out_actions, out_heads);
}

static int gravity_aware_distance_to_target(
    const GameState& state,
    const Snake& s,
    int target_pos
) {
    if (target_pos < 0 || target_pos >= grid_size) return 999999;
    if (!s.is_alive || s.length <= 0) return 999999;
    if (s.body[s.head_idx] == target_pos) return 0;

    vector<int> probe_actions;
    vector<int> probe_heads;
    if (!build_gravity_aware_path_to_target(state, s, target_pos, probe_actions, probe_heads)) {
        return 999999;
    }
    return static_cast<int>(probe_actions.size());
}

static void invalidate_snake_target_cache(int snake_id) {
    g_path_cache_by_snake.erase(snake_id);
    clear_target_progress_state(snake_id);
}

static int cached_gravity_path_action(const GameState& state, const Snake& s, int target_pos, bool target_is_power) {
    if (target_pos == -1) {
        invalidate_snake_target_cache(s.id);
        return -1;
    }

    int head_pos = s.body[s.head_idx];
    int dist = manhattan_dist_pos(head_pos, target_pos);
    auto it = g_path_cache_by_snake.find(s.id);
    if (it == g_path_cache_by_snake.end() || it->second.target_pos != target_pos || it->second.target_is_power != target_is_power) {
        SnakePathCache fresh;
        fresh.target_pos = target_pos;
        fresh.target_is_power = target_is_power;
        fresh.expected_start_head = head_pos;
        fresh.last_dist = dist;
        fresh.last_head_pos = head_pos;
        fresh.same_target_turns = 0;
        g_path_cache_by_snake[s.id] = fresh;
        it = g_path_cache_by_snake.find(s.id);
    }

    SnakePathCache& cache = it->second;
    cache.same_target_turns++;
    if (cache.last_head_pos == head_pos) cache.no_move_turns++;
    else cache.no_move_turns = 0;
    cache.last_head_pos = head_pos;

    int remaining_powerups = 0;
    for (int p = 0; p < grid_size; ++p) {
        if (state.grid[p] == CELL_POWERUP) remaining_powerups++;
    }

    if (target_is_power && dist <= 3 && remaining_powerups > 1 && cache.same_target_turns >= 20) {
        block_target_for_turns(s.id, target_pos, 8);
        g_persistent_target_by_snake.erase(s.id);
        g_persistent_target_is_power.erase(s.id);
        invalidate_snake_target_cache(s.id);
        return -1;
    }

    if (target_is_power && dist > 0 && cache.same_target_turns >= 45) {
        g_persistent_target_by_snake.erase(s.id);
        g_persistent_target_is_power.erase(s.id);
        invalidate_snake_target_cache(s.id);
        return -1;
    }

    if (cache.no_move_turns >= 6) {
        g_persistent_target_by_snake.erase(s.id);
        g_persistent_target_is_power.erase(s.id);
        invalidate_snake_target_cache(s.id);
        return -1;
    }

    if (cache.exact_retry_cooldown > 0) cache.exact_retry_cooldown--;
    if (dist <= 16 && dist >= cache.last_dist) cache.stall_turns++;
    else cache.stall_turns = 0;
    cache.last_dist = dist;

    if (cache.stall_turns >= 7) {
        g_persistent_target_by_snake.erase(s.id);
        g_persistent_target_is_power.erase(s.id);
        invalidate_snake_target_cache(s.id);
        return -1;
    }

    bool aligned = false;
    if (cache.next_step == 0) aligned = (cache.expected_start_head == head_pos);
    else if (cache.next_step - 1 < static_cast<int>(cache.expected_heads.size())) aligned = (cache.expected_heads[cache.next_step - 1] == head_pos);

    if (!aligned && !cache.expected_heads.empty()) {
        int scan_start = max(0, cache.next_step - 2);
        int scan_end = min(static_cast<int>(cache.expected_heads.size()) - 1, cache.next_step + 8);
        for (int i = scan_start; i <= scan_end; ++i) {
            if (cache.expected_heads[i] == head_pos) {
                cache.next_step = i + 1;
                aligned = true;
                break;
            }
        }
    }

    if (!aligned || cache.next_step >= static_cast<int>(cache.actions.size())) {
        cache.actions.clear();
        cache.expected_heads.clear();
        cache.next_step = 0;
        cache.expected_start_head = head_pos;
        bool built = false;
        if (cache.stall_turns > 0 && dist <= 18 && cache.exact_retry_cooldown == 0 && has_planner_time_budget(45)) {
            built = build_exact_full_body_path_to_target(state, s, target_pos, cache.actions, cache.expected_heads);
            if (!built) cache.exact_retry_cooldown = 2;
            else cache.exact_retry_cooldown = 0;
        }
        if (!built) {
            built = build_gravity_aware_path_to_target(state, s, target_pos, cache.actions, cache.expected_heads);
        }
        if (!built) {
            return -1;
        }
    }

    if (cache.next_step >= static_cast<int>(cache.actions.size())) return -1;
    return cache.actions[cache.next_step];
}

static void consume_cached_gravity_path_step(int snake_id) {
    auto it = g_path_cache_by_snake.find(snake_id);
    if (it == g_path_cache_by_snake.end()) return;
    it->second.next_step++;
}

static int choose_support_cell_near_anchor(
    const GameState& state,
    int support_head,
    int anchor_pos,
    const unordered_set<int>& reserved_targets,
    const DangerMapResult& danger
) {
    int best_pos = -1;
    int best_score = -999999;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    int ax = anchor_pos % max_width;
    int ay = anchor_pos / max_width;
    for (int i = 0; i < 4; ++i) {
        int nx = ax + dx[i];
        int ny = ay + dy[i];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
        int npos = ny * max_width + nx;
        if (!is_reusable_target_cell(state, npos)) continue;
        if (reserved_targets.count(npos)) continue;

        int score = 0;
        score -= manhattan_dist_pos(support_head, npos) * 100;
        score -= static_cast<int>(danger.danger_map[npos]) * 300;

        if (score > best_score) {
            best_score = score;
            best_pos = npos;
        }
    }

    if (best_pos == -1 && is_reusable_target_cell(state, anchor_pos) && !reserved_targets.count(anchor_pos)) {
        best_pos = anchor_pos;
    }

    if (best_pos == -1 && is_reusable_target_cell(state, support_head) && !reserved_targets.count(support_head)) {
        best_pos = support_head;
    }

    return best_pos;
}

static int shortest_path_distance_walls_only(const GameState& state, int start_pos, int goal_pos) {
    if (start_pos < 0 || start_pos >= grid_size || goal_pos < 0 || goal_pos >= grid_size) return 999999;
    if (start_pos == goal_pos) return 0;

    vector<int16_t> dist(grid_size, -1);
    queue<int> q;
    dist[start_pos] = 0;
    q.push(start_pos);

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    while (!q.empty()) {
        if (out_of_time()) return 999999;
        int pos = q.front();
        q.pop();
        int px = pos % max_width;
        int py = pos / max_width;
        int16_t base_dist = dist[pos];

        for (int a = 0; a < 4; ++a) {
            int nx = px + dx[a];
            int ny = py + dy[a];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
            int n_pos = ny * max_width + nx;
            if (!is_playable_cell(n_pos)) continue;
            if (dist[n_pos] != -1) continue;
            int16_t cell = state.grid[n_pos];
            if (cell == CELL_WALL) continue;
            dist[n_pos] = static_cast<int16_t>(base_dist + 1);
            if (n_pos == goal_pos) return dist[n_pos];
            q.push(n_pos);
        }
    }

    return 999999;
}

static TargetPlanResult assign_persistent_targets(
    const GameState& state,
    const vector<int>& powerup_positions,
    const vector<SnakeRole>& base_roles,
    const DangerMapResult& danger,
    const vector<vector<int16_t>>& fwd_bfs_by_idx
) {
    TargetPlanResult result;
    size_t n = state.my_snakes.size();
    result.target_positions.assign(n, -1);
    result.target_is_power.assign(n, false);

    unordered_set<int> live_my_ids;
    vector<size_t> alive_indexes;
    for (size_t i = 0; i < n; ++i) {
        const Snake& s = state.my_snakes[i];
        if (!s.is_alive || s.length <= 0) continue;
        live_my_ids.insert(s.id);
        alive_indexes.push_back(i);
    }
    bool single_snake_targeting = (alive_indexes.size() <= 1);

    vector<int> stale_ids;
    for (const auto& kv : g_persistent_target_by_snake) {
        if (!live_my_ids.count(kv.first)) stale_ids.push_back(kv.first);
    }
    for (int id : stale_ids) {
        g_persistent_target_by_snake.erase(id);
        g_persistent_target_is_power.erase(id);
        g_path_cache_by_snake.erase(id);
    }

    if (single_snake_targeting && !alive_indexes.empty() && !powerup_positions.empty()) {
        size_t idx = alive_indexes[0];
        const Snake& s = state.my_snakes[idx];
        int head = s.body[s.head_idx];
        vector<int> probe_actions;
        vector<int> probe_heads;

        auto it_prev = g_persistent_target_by_snake.find(s.id);
        if (it_prev != g_persistent_target_by_snake.end() && g_persistent_target_is_power[s.id]) {
            int prev_target = it_prev->second;
            bool still_exists = false;
            for (int p : powerup_positions) {
                if (p == prev_target) {
                    still_exists = true;
                    break;
                }
            }
            if (still_exists
                && prev_target != head
                && !is_target_temporarily_blocked(s.id, prev_target)
                && build_gravity_aware_path_to_target(state, s, prev_target, probe_actions, probe_heads)) {
                result.target_positions[idx] = prev_target;
                result.target_is_power[idx] = true;
                return result;
            }
        }

        int best_target = -1;
        int best_dist = 999999;
        bool found_gravity_path = false;
        for (int p : powerup_positions) {
            if (p == head) continue;
            if (is_target_temporarily_blocked(s.id, p)) continue;
            if (build_gravity_aware_path_to_target(state, s, p, probe_actions, probe_heads)) {
                int d = static_cast<int>(probe_actions.size());
                if (!found_gravity_path || d < best_dist) {
                    best_dist = d;
                    best_target = p;
                    found_gravity_path = true;
                }
                continue;
            }
            if (found_gravity_path) continue;

            int d = shortest_path_distance_walls_only(state, head, p);
            if (d < best_dist) {
                best_dist = d;
                best_target = p;
            }
        }

        if (best_target != -1) {
            result.target_positions[idx] = best_target;
            result.target_is_power[idx] = true;
            g_persistent_target_by_snake[s.id] = best_target;
            g_persistent_target_is_power[s.id] = true;
        }

        return result;
    }

    unordered_set<int> available_power(powerup_positions.begin(), powerup_positions.end());
    unordered_set<int> reserved_targets;

    // Path distance helper: BFS distance if available, else Manhattan fallback.
    // bfs_dist_to(snake_idx, from_pos, to_pos)
    auto bfs_dist_to = [&](size_t idx, int from_pos, int to_pos) -> int {
        if (to_pos < 0 || to_pos >= grid_size) return 999999;
        if (idx < fwd_bfs_by_idx.size() && !fwd_bfs_by_idx[idx].empty()) {
            int16_t d = fwd_bfs_by_idx[idx][to_pos];
            return (d >= 0) ? static_cast<int>(d) : 999999;
        }
        return manhattan_dist_pos(from_pos, to_pos);
    };

    auto pos_world_y = [&](int pos) -> int {
        return (pos / max_width) - max_len;
    };

    auto collector_candidate_cost = [&](size_t idx, int head, int target_pos) -> int {
        int d = bfs_dist_to(idx, head, target_pos);
        if (d >= 999999) return d;

        int penalty = 0;
        bool large_map = (world_width * world_height >= 500);
        if (!single_snake_targeting && large_map && powerup_positions.size() >= 3) {
            int hy = pos_world_y(head);
            int ty = pos_world_y(target_pos);
            int low_band = (world_height * 2) / 3;
            int top_band = world_height / 5;

            if (hy >= low_band && ty <= top_band) {
                bool has_mid_alternative = false;
                for (int p : powerup_positions) {
                    if (p == target_pos) continue;
                    if (is_target_temporarily_blocked(state.my_snakes[idx].id, p)) continue;
                    int py = pos_world_y(p);
                    if (py <= top_band || py >= low_band) continue;
                    int alt_d = bfs_dist_to(idx, head, p);
                    if (alt_d < 999999 && alt_d <= d + 10) {
                        has_mid_alternative = true;
                        break;
                    }
                }
                if (has_mid_alternative) penalty += 5000;
            }
        }

        return d + penalty;
    };

    vector<size_t> collector_indexes;
    for (size_t i : alive_indexes) {
        if (base_roles[i] == SnakeRole::Collector) collector_indexes.push_back(i);
    }

    if (powerup_positions.size() == 1 && !alive_indexes.empty()) {
        collector_indexes.clear();
        int single_power = powerup_positions[0];
        int best_idx = static_cast<int>(alive_indexes[0]);
        int best_dist = 999999;
        for (size_t i : alive_indexes) {
            const Snake& s = state.my_snakes[i];
            int head = s.body[s.head_idx];
            int d = bfs_dist_to(i, head, single_power);
            if (d == 0) continue; // already standing on it — must leave first
            if (is_target_temporarily_blocked(s.id, single_power)) continue;
            if (d < best_dist) {
                best_dist = d;
                best_idx = static_cast<int>(i);
            }
        }
        if (best_dist < 999999) {
            collector_indexes.push_back(static_cast<size_t>(best_idx));
        }
    } else if (collector_indexes.empty() && !powerup_positions.empty() && !alive_indexes.empty()) {
        int best_idx = static_cast<int>(alive_indexes[0]);
        int best_dist = 999999;
        for (size_t i : alive_indexes) {
            const Snake& s = state.my_snakes[i];
            int head = s.body[s.head_idx];
            for (int p : powerup_positions) {
                int d = collector_candidate_cost(i, head, p);
                if (d == 0) continue; // skip own head position
                if (is_target_temporarily_blocked(s.id, p)) continue;
                if (d < best_dist) {
                    best_dist = d;
                    best_idx = static_cast<int>(i);
                }
            }
        }
        if (best_dist < 999999) {
            collector_indexes.push_back(static_cast<size_t>(best_idx));
        }
    }

    unordered_set<size_t> collector_index_set(collector_indexes.begin(), collector_indexes.end());

    for (size_t i : collector_indexes) {
        const Snake& s = state.my_snakes[i];
        auto it = g_persistent_target_by_snake.find(s.id);
        if (it == g_persistent_target_by_snake.end()) continue;
        bool was_power = g_persistent_target_is_power[s.id];
        int prev_target = it->second;
        if (was_power && available_power.count(prev_target) && is_reusable_target_cell(state, prev_target) && !reserved_targets.count(prev_target) && !is_target_temporarily_blocked(s.id, prev_target)) {
            // Re-check: don't reuse a target that we're already standing on
            int head = s.body[s.head_idx];
            if (manhattan_dist_pos(head, prev_target) == 0) continue;
            result.target_positions[i] = prev_target;
            result.target_is_power[i] = true;
            available_power.erase(prev_target);
            reserved_targets.insert(prev_target);
        }
    }

    for (size_t i : collector_indexes) {
        if (result.target_positions[i] != -1) continue;
        const Snake& s = state.my_snakes[i];
        int head = s.body[s.head_idx];
        int chosen = -1;
        int best_dist = 999999;

        if (!available_power.empty()) {
            for (int p : available_power) {
                int d = collector_candidate_cost(i, head, p);
                if (d == 0) continue; // skip own head position
                if (is_target_temporarily_blocked(s.id, p)) continue;
                if (d < best_dist) {
                    best_dist = d;
                    chosen = p;
                }
            }
            if (chosen != -1) available_power.erase(chosen);
        } else if (!powerup_positions.empty()) {
            for (int p : powerup_positions) {
                int d = collector_candidate_cost(i, head, p);
                if (d == 0) continue; // skip own head position
                if (is_target_temporarily_blocked(s.id, p)) continue;
                if (d < best_dist && !reserved_targets.count(p)) {
                    best_dist = d;
                    chosen = p;
                }
            }
        }

        if (chosen != -1) {
            result.target_positions[i] = chosen;
            result.target_is_power[i] = true;
            reserved_targets.insert(chosen);
        }
    }

    vector<size_t> support_indexes;
    for (size_t i : alive_indexes) {
        if (!collector_index_set.count(i)) support_indexes.push_back(i);
    }

    for (size_t i : support_indexes) {
        const Snake& s = state.my_snakes[i];
        auto it = g_persistent_target_by_snake.find(s.id);
        if (it != g_persistent_target_by_snake.end()) {
            int prev_target = it->second;
            bool was_power = g_persistent_target_is_power[s.id];
            if (!was_power && is_reusable_target_cell(state, prev_target) && !reserved_targets.count(prev_target)) {
                result.target_positions[i] = prev_target;
                result.target_is_power[i] = false;
                reserved_targets.insert(prev_target);
                continue;
            }
        }

        int head = s.body[s.head_idx];
        int anchor = -1;
        int anchor_dist = 999999;

        for (size_t ci : collector_indexes) {
            int collector_target = result.target_positions[ci];
            if (collector_target == -1) continue;
            int d = manhattan_dist_pos(head, collector_target);
            if (d < anchor_dist) {
                anchor_dist = d;
                anchor = collector_target;
            }
        }

        if (anchor == -1 && !powerup_positions.empty()) {
            for (int p : powerup_positions) {
                int d = manhattan_dist_pos(head, p);
                if (d < anchor_dist) {
                    anchor_dist = d;
                    anchor = p;
                }
            }
        }

        if (anchor == -1) {
            anchor = head;
        }

        int support_target = choose_support_cell_near_anchor(state, head, anchor, reserved_targets, danger);
        if (support_target != -1) {
            result.target_positions[i] = support_target;
            result.target_is_power[i] = false;
            reserved_targets.insert(support_target);
        }
    }

    // Final guard: never keep a target that is the snake's current head cell.
    for (size_t i : alive_indexes) {
        const Snake& s = state.my_snakes[i];
        int head = s.body[s.head_idx];
        int tpos = result.target_positions[i];
        if (tpos == -1 || tpos != head) continue;

        if (result.target_is_power[i]) {
            int chosen = -1;
            int best_dist = 999999;
            for (int p : powerup_positions) {
                int d = manhattan_dist_pos(head, p);
                if (d == 0) continue;
                if (is_target_temporarily_blocked(s.id, p)) continue;
                if (d < best_dist) {
                    best_dist = d;
                    chosen = p;
                }
            }
            if (chosen != -1) {
                result.target_positions[i] = chosen;
                result.target_is_power[i] = true;
            } else {
                result.target_positions[i] = -1;
                result.target_is_power[i] = false;
            }
        } else {
            result.target_positions[i] = -1;
            result.target_is_power[i] = false;
        }
    }

    for (size_t i : alive_indexes) {
        const Snake& s = state.my_snakes[i];
        int tpos = result.target_positions[i];
        if (tpos != -1) {
            g_persistent_target_by_snake[s.id] = tpos;
            g_persistent_target_is_power[s.id] = result.target_is_power[i];
        }
    }

    return result;
}
static int delayed_powerup_risk_penalty(const GameState& state, int snake_id, int initial_action, int horizon_steps) {
    if (horizon_steps <= 0) return 0;
    int my_idx = find_my_snake_index(state, snake_id);
    if (my_idx == -1) return 0;

    GameState probe = state;
    vector<int> my_actions = infer_default_actions(probe.my_snakes);
    vector<int> opp_actions = infer_default_actions(probe.opp_snakes);
    my_actions[my_idx] = initial_action;
    probe.simulate(my_actions, opp_actions);

    const Snake* me = probe.find_my_snake_by_id(snake_id);
    if (me == nullptr || !me->is_alive) {
        return BotTuning::DELAYED_DEATH_PENALTY;
    }

    int penalty = 0;
    for (int step = 0; step < horizon_steps; ++step) {
        if (out_of_time()) break;

        me = probe.find_my_snake_by_id(snake_id);
        if (me == nullptr || !me->is_alive) {
            penalty += BotTuning::DELAYED_DEATH_PENALTY;
            break;
        }

        int followups = probe.count_safe_followups(*me);
        if (followups == 0) penalty += BotTuning::DELAYED_NO_FOLLOWUP_PENALTY;
        else if (followups == 1) penalty += BotTuning::DELAYED_LOW_FOLLOWUP_PENALTY;

        int head = me->body[me->head_idx];
        int hx = head % max_width;
        int hy = head / max_width;
        int best_a = -1;
        int best_eval = -999999;
        int dx[] = {0, 0, -1, 1};
        int dy[] = {-1, 1, 0, 0};

        for (int a = 0; a < 4; ++a) {
            if (is_backward_action(*me, a)) continue;
            int nx = hx + dx[a];
            int ny = hy + dy[a];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
            int npos = ny * max_width + nx;
            int16_t cell = probe.grid[npos];
            if (cell == CELL_WALL || cell >= CELL_SNAKE_BASE) continue;

            GameState next_probe = probe;
            vector<int> m_actions = infer_default_actions(next_probe.my_snakes);
            vector<int> o_actions = infer_default_actions(next_probe.opp_snakes);
            int nidx = find_my_snake_index(next_probe, snake_id);
            if (nidx == -1) continue;
            m_actions[nidx] = a;
            next_probe.simulate(m_actions, o_actions);
            const Snake* nme = next_probe.find_my_snake_by_id(snake_id);
            if (nme == nullptr || !nme->is_alive) continue;

            int eval = next_probe.count_safe_followups(*nme) * 1000;
            if (next_probe.has_adjacent_powerup(nme->body[nme->head_idx])) eval += 1200;
            if (eval > best_eval) {
                best_eval = eval;
                best_a = a;
            }
        }

        if (best_a == -1) {
            penalty += BotTuning::DELAYED_NO_FOLLOWUP_PENALTY;
            break;
        }

        vector<int> m_actions = infer_default_actions(probe.my_snakes);
        vector<int> o_actions = infer_default_actions(probe.opp_snakes);
        int nidx = find_my_snake_index(probe, snake_id);
        if (nidx == -1) {
            penalty += BotTuning::DELAYED_DEATH_PENALTY;
            break;
        }
        m_actions[nidx] = best_a;
        probe.simulate(m_actions, o_actions);
    }

    return penalty;
}

static int evaluate_local_combat(
    const GameState& state,
    int my_snake_id,
    int opp_snake_id,
    int root_my_length,
    int root_opp_length
) {
    const Snake* self = state.find_my_snake_by_id(my_snake_id);
    const Snake* opp = state.find_opp_snake_by_id(opp_snake_id);

    if (self == nullptr || !self->is_alive) return -70000;
    if (opp == nullptr || !opp->is_alive) return 70000;

    int score = 0;
    score += (self->length - opp->length) * BotTuning::LOCAL_COMBAT_LENGTH_WEIGHT;

    int my_lost = max(0, root_my_length - self->length);
    int opp_lost = max(0, root_opp_length - opp->length);
    score -= my_lost * BotTuning::LOCAL_COMBAT_LOST_SEGMENT_WEIGHT;
    score += opp_lost * BotTuning::LOCAL_COMBAT_LOST_SEGMENT_WEIGHT;

    int my_head = self->body[self->head_idx];
    int opp_head = opp->body[opp->head_idx];
    int my_hx = my_head % max_width;
    int my_hy = my_head / max_width;
    int opp_hx = opp_head % max_width;
    int opp_hy = opp_head / max_width;
    int hdist = abs(my_hx - opp_hx) + abs(my_hy - opp_hy);

    if (hdist <= 1) score += BotTuning::LOCAL_COMBAT_H2H_BONUS;
    score -= hdist * BotTuning::LOCAL_COMBAT_HDIST_PENALTY;

    return score;
}

static int local_alpha_beta_value(
    const GameState& state,
    int my_snake_id,
    int opp_snake_id,
    int root_my_length,
    int root_opp_length,
    int depth,
    int alpha,
    int beta
) {
    if (out_of_time() || depth <= 0) {
        return evaluate_local_combat(state, my_snake_id, opp_snake_id, root_my_length, root_opp_length);
    }

    const Snake* self = state.find_my_snake_by_id(my_snake_id);
    const Snake* opp = state.find_opp_snake_by_id(opp_snake_id);
    if (self == nullptr || opp == nullptr || !self->is_alive || !opp->is_alive) {
        return evaluate_local_combat(state, my_snake_id, opp_snake_id, root_my_length, root_opp_length);
    }

    int my_idx = find_my_snake_index(state, my_snake_id);
    int opp_idx = find_opp_snake_index(state, opp_snake_id);
    if (my_idx == -1 || opp_idx == -1) {
        return evaluate_local_combat(state, my_snake_id, opp_snake_id, root_my_length, root_opp_length);
    }

    vector<int> default_my_actions = infer_default_actions(state.my_snakes);
    vector<int> default_opp_actions = infer_default_actions(state.opp_snakes);

    int best = -999999;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    int self_head = self->body[self->head_idx];
    int sx = self_head % max_width;
    int sy = self_head / max_width;

    for (int my_a = 0; my_a < 4; ++my_a) {
        if (is_backward_action(*self, my_a)) continue;

        int mnx = sx + dx[my_a];
        int mny = sy + dy[my_a];
        if (mnx < 0 || mnx >= max_width || mny < 0 || mny >= max_height) continue;
        int my_npos = mny * max_width + mnx;
        int16_t my_cell = state.grid[my_npos];
        if (my_cell == CELL_WALL || my_cell >= CELL_SNAKE_BASE) continue;

        int worst = 999999;

        int opp_head = opp->body[opp->head_idx];
        int ox = opp_head % max_width;
        int oy = opp_head / max_width;

        for (int opp_a = 0; opp_a < 4; ++opp_a) {
            if (out_of_time()) break;
            if (is_backward_action(*opp, opp_a)) continue;

            int onx = ox + dx[opp_a];
            int ony = oy + dy[opp_a];
            if (onx < 0 || onx >= max_width || ony < 0 || ony >= max_height) continue;
            int opp_npos = ony * max_width + onx;
            int16_t opp_cell = state.grid[opp_npos];
            if (opp_cell == CELL_WALL || opp_cell >= CELL_SNAKE_BASE) continue;

            GameState next_state = state;
            vector<int> sim_my_actions = default_my_actions;
            vector<int> sim_opp_actions = default_opp_actions;
            sim_my_actions[my_idx] = my_a;
            sim_opp_actions[opp_idx] = opp_a;
            next_state.simulate(sim_my_actions, sim_opp_actions);

            int value = local_alpha_beta_value(
                next_state,
                my_snake_id,
                opp_snake_id,
                root_my_length,
                root_opp_length,
                depth - 1,
                alpha,
                beta
            );
            worst = min(worst, value);
            beta = min(beta, worst);
            if (beta <= alpha) break;
        }

        if (worst == 999999) {
            GameState next_state = state;
            vector<int> sim_my_actions = default_my_actions;
            vector<int> sim_opp_actions = default_opp_actions;
            sim_my_actions[my_idx] = my_a;
            next_state.simulate(sim_my_actions, sim_opp_actions);
            worst = evaluate_local_combat(next_state, my_snake_id, opp_snake_id, root_my_length, root_opp_length);
        }

        best = max(best, worst);
        alpha = max(alpha, best);
        if (beta <= alpha) break;
    }

    if (best == -999999) {
        return evaluate_local_combat(state, my_snake_id, opp_snake_id, root_my_length, root_opp_length);
    }

    return best;
}

static int choose_local_alpha_beta_action_depth(const GameState& state, int my_snake_id, int depth, bool& completed_depth) {
    const Snake* self = state.find_my_snake_by_id(my_snake_id);
    if (self == nullptr || !self->is_alive) {
        completed_depth = false;
        return -1;
    }
    if (state.opp_snakes.empty()) {
        completed_depth = false;
        return -1;
    }

    int my_head = self->body[self->head_idx];
    int my_hx = my_head % max_width;
    int my_hy = my_head / max_width;

    int target_opp_id = state.opp_snakes[0].id;
    int best_opp_dist = 999999;
    for (const auto& opp : state.opp_snakes) {
        if (!opp.is_alive) continue;
        int opp_head = opp.body[opp.head_idx];
        int d = abs(my_hx - (opp_head % max_width)) + abs(my_hy - (opp_head / max_width));
        if (d < best_opp_dist) {
            best_opp_dist = d;
            target_opp_id = opp.id;
        }
    }

    const Snake* target_opp = state.find_opp_snake_by_id(target_opp_id);
    if (target_opp == nullptr || !target_opp->is_alive) {
        completed_depth = false;
        return -1;
    }

    int my_idx = find_my_snake_index(state, my_snake_id);
    if (my_idx == -1) {
        completed_depth = false;
        return -1;
    }

    vector<int> default_my_actions = infer_default_actions(state.my_snakes);
    vector<int> default_opp_actions = infer_default_actions(state.opp_snakes);
    int opp_idx = find_opp_snake_index(state, target_opp_id);
    if (opp_idx == -1) {
        completed_depth = false;
        return -1;
    }

    int best_action = -1;
    int best_value = -999999;
    bool interrupted = false;

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    int sx = my_hx;
    int sy = my_hy;

    for (int a = 0; a < 4; ++a) {
        if (out_of_time()) {
            interrupted = true;
            break;
        }
        if (is_backward_action(*self, a)) continue;

        int nx = sx + dx[a];
        int ny = sy + dy[a];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
        int n_pos = ny * max_width + nx;
        int16_t next_cell = state.grid[n_pos];
        if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;

        int value = -999999;
        int worst = 999999;

        int ox = target_opp->body[target_opp->head_idx] % max_width;
        int oy = target_opp->body[target_opp->head_idx] / max_width;

        for (int opp_a = 0; opp_a < 4; ++opp_a) {
            if (out_of_time()) {
                interrupted = true;
                break;
            }
            if (is_backward_action(*target_opp, opp_a)) continue;

            int onx = ox + dx[opp_a];
            int ony = oy + dy[opp_a];
            if (onx < 0 || onx >= max_width || ony < 0 || ony >= max_height) continue;
            int opp_npos = ony * max_width + onx;
            int16_t opp_cell = state.grid[opp_npos];
            if (opp_cell == CELL_WALL || opp_cell >= CELL_SNAKE_BASE) continue;

            GameState next_state = state;
            vector<int> sim_my_actions = default_my_actions;
            vector<int> sim_opp_actions = default_opp_actions;
            sim_my_actions[my_idx] = a;
            sim_opp_actions[opp_idx] = opp_a;
            next_state.simulate(sim_my_actions, sim_opp_actions);

            int v = local_alpha_beta_value(
                next_state,
                my_snake_id,
                target_opp_id,
                self->length,
                target_opp->length,
                depth - 1,
                -999999,
                999999
            );

            worst = min(worst, v);
        }

        if (interrupted) break;

        if (worst == 999999) {
            GameState next_state = state;
            vector<int> sim_my_actions = default_my_actions;
            vector<int> sim_opp_actions = default_opp_actions;
            sim_my_actions[my_idx] = a;
            next_state.simulate(sim_my_actions, sim_opp_actions);
            worst = evaluate_local_combat(next_state, my_snake_id, target_opp_id, self->length, target_opp->length);
        }
        value = worst;

        if (value > best_value) {
            best_value = value;
            best_action = a;
        }
    }

    completed_depth = !interrupted;
    return best_action;
}

static int choose_local_alpha_beta_action_iterative(const GameState& state, int my_snake_id, int min_depth, int max_depth) {
    int fallback_action = -1;
    int last_completed_action = -1;

    for (int depth = min_depth; depth <= max_depth; ++depth) {
        if (out_of_time()) break;
        bool completed_depth = false;
        int action = choose_local_alpha_beta_action_depth(state, my_snake_id, depth, completed_depth);
        if (action != -1) {
            fallback_action = action;
            if (completed_depth) {
                last_completed_action = action;
            }
        }
        if (!completed_depth) break;
    }

    if (last_completed_action != -1) return last_completed_action;
    return fallback_action;
}

static int choose_local_alpha_beta_action_budgeted(
    const GameState& state,
    int my_snake_id,
    int min_depth,
    int start_max_depth
) {
    int best_action = -1;
    int rolling_max_depth = max(min_depth, start_max_depth);
    const int safety_cap_depth = 64;

    while (!out_of_time() && rolling_max_depth <= safety_cap_depth) {
        int candidate = choose_local_alpha_beta_action_iterative(state, my_snake_id, min_depth, rolling_max_depth);
        if (candidate != -1) best_action = candidate;
        rolling_max_depth++;
    }

    return best_action;
}

static int min_steps_to_powerup_gain(const GameState& state, int snake_id, int start_length, int depth_left, uint64_t root_hash) {
    if (out_of_time()) return 999999;

    const Snake* self = state.find_my_snake_by_id(snake_id);
    if (self == nullptr || !self->is_alive) return 999999;
    if (self->length > start_length) return 0;
    if (depth_left <= 0) return 999999;

    int my_idx = find_my_snake_index(state, snake_id);
    if (my_idx == -1) return 999999;

    vector<int> default_my_actions = infer_default_actions(state.my_snakes);
    vector<int> default_opp_actions = infer_default_actions(state.opp_snakes);

    int head_pos = self->body[self->head_idx];
    int hx = head_pos % max_width;
    int hy = head_pos / max_width;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    int best = 999999;

    for (int a = 0; a < 4; ++a) {
        if (is_backward_action(*self, a)) continue;

        int nx = hx + dx[a];
        int ny = hy + dy[a];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;

        int n_pos = ny * max_width + nx;
        int16_t next_cell = state.grid[n_pos];
        if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;

        GameState next_state = state;
        vector<int> sim_my_actions = default_my_actions;
        vector<int> sim_opp_actions = default_opp_actions;
        sim_my_actions[my_idx] = a;
        next_state.simulate(sim_my_actions, sim_opp_actions);

        const Snake* next_self = next_state.find_my_snake_by_id(snake_id);
        if (next_self == nullptr || !next_self->is_alive) continue;
        if (next_self->length < start_length) continue;
        if (next_self->length == start_length && snake_body_hash(*next_self) == root_hash) continue;
        if (next_self->length > start_length) {
            best = min(best, 1);
            continue;
        }

        int rec = min_steps_to_powerup_gain(next_state, snake_id, start_length, depth_left - 1, root_hash);
        if (rec < 999999) {
            best = min(best, 1 + rec);
        }
    }

    return best;
}

static int first_action_to_powerup_gain(const GameState& state, int snake_id, int max_depth) {
    const Snake* self = state.find_my_snake_by_id(snake_id);
    if (self == nullptr || !self->is_alive) return -1;
    uint64_t root_hash = snake_body_hash(*self);

    int my_idx = find_my_snake_index(state, snake_id);
    if (my_idx == -1) return -1;

    vector<int> default_my_actions = infer_default_actions(state.my_snakes);
    vector<int> default_opp_actions = infer_default_actions(state.opp_snakes);

    int head_pos = self->body[self->head_idx];
    int hx = head_pos % max_width;
    int hy = head_pos / max_width;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    int best_action = -1;
    int best_steps = 999999;

    for (int a = 0; a < 4; ++a) {
        if (is_backward_action(*self, a)) continue;

        int nx = hx + dx[a];
        int ny = hy + dy[a];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;

        int n_pos = ny * max_width + nx;
        int16_t next_cell = state.grid[n_pos];
        if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;

        GameState next_state = state;
        vector<int> sim_my_actions = default_my_actions;
        vector<int> sim_opp_actions = default_opp_actions;
        sim_my_actions[my_idx] = a;
        next_state.simulate(sim_my_actions, sim_opp_actions);

        const Snake* next_self = next_state.find_my_snake_by_id(snake_id);
        if (next_self == nullptr || !next_self->is_alive) continue;
        if (next_self->length < self->length) continue;
        if (next_self->length == self->length && snake_body_hash(*next_self) == root_hash) continue;

        int steps = 999999;
        if (next_self->length > self->length) {
            steps = 1;
        } else {
            steps = min_steps_to_powerup_gain(next_state, snake_id, next_self->length, max_depth - 1, root_hash);
            if (steps < 999999) {
                steps += 1;
            }
        }

        if (steps < best_steps) {
            best_steps = steps;
            best_action = a;
        }
    }

    return best_steps < 999999 ? best_action : -1;
}

static int first_action_to_powerup_gain_budgeted(
    const GameState& state,
    int snake_id,
    int start_depth
) {
    int best_action = -1;
    int max_depth = max(1, start_depth);
    const int safety_cap_depth = 128;

    while (!out_of_time() && max_depth <= safety_cap_depth) {
        int candidate = first_action_to_powerup_gain(state, snake_id, max_depth);
        if (candidate != -1) best_action = candidate;
        max_depth++;
    }

    return best_action;
}

static int bfs_distance_snake_head_to_cell(const GameState& state, const Snake& s, int target_pos) {
    if (target_pos < 0 || target_pos >= grid_size) return 999999;
    if (!s.is_alive || s.length <= 0) return 999999;

    int head_pos = s.body[s.head_idx];
    if (head_pos == target_pos) return 0;

    vector<int16_t> dist(grid_size, -1);
    queue<int> q;
    dist[head_pos] = 0;
    q.push(head_pos);

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    while (!q.empty()) {
        if (out_of_time()) return 999999;
        int pos = q.front();
        q.pop();
        int px = pos % max_width;
        int py = pos / max_width;
        int16_t base_dist = dist[pos];

        for (int a = 0; a < 4; ++a) {
            int nx = px + dx[a];
            int ny = py + dy[a];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
            int n_pos = ny * max_width + nx;
            if (!is_playable_cell(n_pos)) continue;
            if (dist[n_pos] != -1) continue;
            int16_t cell = state.grid[n_pos];
            if (cell == CELL_WALL) continue;
            dist[n_pos] = static_cast<int16_t>(base_dist + 1);
            if (n_pos == target_pos) return dist[n_pos];
            q.push(n_pos);
        }
    }

    return 999999;
}

static int min_dist_to_target_after_steps(
    const GameState& state,
    int snake_id,
    int target_pos,
    int depth_left
) {
    if (out_of_time()) return 999999;

    const Snake* self = state.find_my_snake_by_id(snake_id);
    if (self == nullptr || !self->is_alive) return 999999;

    bool use_gravity_metric = state.opp_snakes.empty();
    int current_dist = use_gravity_metric
        ? gravity_aware_distance_to_target(state, *self, target_pos)
        : bfs_distance_snake_head_to_cell(state, *self, target_pos);
    if (current_dist <= 0) return max(0, current_dist);
    if (depth_left <= 0) return current_dist;

    int my_idx = find_my_snake_index(state, snake_id);
    if (my_idx == -1) return current_dist;

    vector<int> default_my_actions = infer_default_actions(state.my_snakes);
    vector<int> default_opp_actions = infer_default_actions(state.opp_snakes);

    int head_pos = self->body[self->head_idx];
    int hx = head_pos % max_width;
    int hy = head_pos / max_width;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    int best = current_dist;

    for (int a = 0; a < 4; ++a) {
        if (is_backward_action(*self, a)) continue;
        int nx = hx + dx[a];
        int ny = hy + dy[a];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
        int n_pos = ny * max_width + nx;
        int16_t next_cell = state.grid[n_pos];
        if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;

        GameState next_state = state;
        vector<int> sim_my_actions = default_my_actions;
        vector<int> sim_opp_actions = default_opp_actions;
        sim_my_actions[my_idx] = a;
        next_state.simulate(sim_my_actions, sim_opp_actions);

        const Snake* next_self = next_state.find_my_snake_by_id(snake_id);
        if (next_self == nullptr || !next_self->is_alive) continue;

        int rec = min_dist_to_target_after_steps(next_state, snake_id, target_pos, depth_left - 1);
        if (rec < best) best = rec;
        if (best == 0) break;
    }

    return best;
}

static int first_action_to_target_progress(const GameState& state, int snake_id, int target_pos, int max_depth) {
    const Snake* self = state.find_my_snake_by_id(snake_id);
    if (self == nullptr || !self->is_alive) return -1;

    int my_idx = find_my_snake_index(state, snake_id);
    if (my_idx == -1) return -1;

    vector<int> default_my_actions = infer_default_actions(state.my_snakes);
    vector<int> default_opp_actions = infer_default_actions(state.opp_snakes);

    int head_pos = self->body[self->head_idx];
    int hx = head_pos % max_width;
    int hy = head_pos / max_width;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    int best_action = -1;
    int best_dist = 999999;
    int best_immediate_dist = 999999;
    bool use_gravity_metric = state.opp_snakes.empty();

    for (int a = 0; a < 4; ++a) {
        if (is_backward_action(*self, a)) continue;

        int nx = hx + dx[a];
        int ny = hy + dy[a];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;

        int n_pos = ny * max_width + nx;
        int16_t next_cell = state.grid[n_pos];
        if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;

        GameState next_state = state;
        vector<int> sim_my_actions = default_my_actions;
        vector<int> sim_opp_actions = default_opp_actions;
        sim_my_actions[my_idx] = a;
        next_state.simulate(sim_my_actions, sim_opp_actions);

        const Snake* next_self = next_state.find_my_snake_by_id(snake_id);
        if (next_self == nullptr || !next_self->is_alive) continue;

        int immediate_dist = use_gravity_metric
            ? gravity_aware_distance_to_target(next_state, *next_self, target_pos)
            : bfs_distance_snake_head_to_cell(next_state, *next_self, target_pos);
        int projected_dist = min_dist_to_target_after_steps(next_state, snake_id, target_pos, max(0, max_depth - 1));

        if (projected_dist < best_dist || (projected_dist == best_dist && immediate_dist < best_immediate_dist)) {
            best_dist = projected_dist;
            best_immediate_dist = immediate_dist;
            best_action = a;
        }
    }

    return best_action;
}

static int first_action_to_target_progress_budgeted(
    const GameState& state,
    int snake_id,
    int target_pos,
    int start_depth
) {
    int best_action = -1;
    int max_depth = max(1, start_depth);
    const int safety_cap_depth = 24;

    while (!out_of_time() && max_depth <= safety_cap_depth) {
        int candidate = first_action_to_target_progress(state, snake_id, target_pos, max_depth);
        if (candidate != -1) best_action = candidate;
        max_depth++;
    }

    return best_action;
}

// BFS shortest path from given snake's head to target_pos.
// Returns the first action (0=UP 1=DOWN 2=LEFT 3=RIGHT) to take, or -1 if
// no path exists or the target is already at the head position.
// Only navigates through playable cells (not padding zones).
static int bfs_first_action_to_cell(const GameState& state, const Snake& s, int target_pos, bool allow_snake_cells = false) {
    if (target_pos < 0 || target_pos >= grid_size) return -1;
    int head_pos = s.body[s.head_idx];
    if (head_pos == target_pos) return -1; // already there — caller decides what to do

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    int hx = head_pos % max_width;
    int hy = head_pos / max_width;

    // first_dir[pos] = first action taken from head to reach pos (-1 = unvisited)
    vector<int8_t> first_dir(grid_size, -1);
    queue<int> q;

    // Seed with direct neighbours
    for (int a = 0; a < 4; ++a) {
        if (is_backward_action(s, a)) continue;
        int nx = hx + dx[a];
        int ny = hy + dy[a];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
        int n_pos = ny * max_width + nx;
        if (!is_playable_cell(n_pos)) continue; // stay within playable area only
        int16_t cell = state.grid[n_pos];
        if (cell == CELL_WALL) continue;
        if (!allow_snake_cells && cell >= CELL_SNAKE_BASE) continue;
        if (n_pos == target_pos) return a; // one step away
        if (first_dir[n_pos] != -1) continue;
        first_dir[n_pos] = static_cast<int8_t>(a);
        q.push(n_pos);
    }

    while (!q.empty()) {
        if (out_of_time()) return -1;
        int pos = q.front(); q.pop();
        int px = pos % max_width;
        int py = pos / max_width;
        int8_t fd = first_dir[pos];

        for (int a = 0; a < 4; ++a) {
            int nx = px + dx[a];
            int ny = py + dy[a];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
            int n_pos = ny * max_width + nx;
            if (first_dir[n_pos] != -1) continue;
            if (!is_playable_cell(n_pos)) continue; // stay within playable area only
            int16_t cell = state.grid[n_pos];
            if (cell == CELL_WALL) continue;
            if (!allow_snake_cells && cell >= CELL_SNAKE_BASE) continue;
            if (n_pos == target_pos) return static_cast<int>(fd); // found
            first_dir[n_pos] = fd;
            q.push(n_pos);
        }
    }
    return -1; // no path
}

// Wall-aware BFS from a snake's head. Returns dist[pos] = steps to reach pos,
// -1 = unreachable. Treats walls as hard obstacles; snake bodies are treated as
// soft/passable for long-term planning and filtered by simulation safety checks.
// Used for real path-distance target selection (replaces Manhattan approximation).
static vector<int16_t> build_forward_bfs_dist_from_head(const GameState& state, const Snake& s) {
    vector<int16_t> dist(grid_size, -1);
    if (!s.is_alive || s.length <= 0) return dist;

    int head_pos = s.body[s.head_idx];
    if (head_pos < 0 || head_pos >= grid_size) return dist;
    dist[head_pos] = 0;

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    queue<int> q;
    q.push(head_pos);

    while (!q.empty()) {
        if (out_of_time()) break;
        int pos = q.front(); q.pop();
        int16_t d = dist[pos];
        int px = pos % max_width;
        int py = pos / max_width;
        for (int a = 0; a < 4; ++a) {
            int nx = px + dx[a];
            int ny = py + dy[a];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
            int n_pos = ny * max_width + nx;
            if (!is_playable_cell(n_pos)) continue;
            if (dist[n_pos] != -1) continue;
            int16_t cell = state.grid[n_pos];
            if (cell == CELL_WALL) continue;
            dist[n_pos] = static_cast<int16_t>(d + 1);
            q.push(n_pos);
        }
    }
    return dist;
}

struct BackwardPowerBFSResult {
    vector<int16_t> best_dist_to_power;
    int visited_states = 0;
    bool completed = true;
};

// Build a backward multi-source BFS from all powerups.
// State is (cell_position, unsupported_steps_since_last_support).
// Unsupported steps are bounded by snake_len to model gap crossing limits.
static BackwardPowerBFSResult build_backward_power_bfs_map(const GameState& state, int snake_len) {
    BackwardPowerBFSResult result;
    result.best_dist_to_power.assign(grid_size, -1);

    if (snake_len <= 0 || grid_size <= 0) {
        return result;
    }

    const int stride = snake_len + 1;
    vector<int16_t> state_dist(grid_size * stride, -1);
    queue<int> q;

    auto has_support_below = [&](int pos) {
        int below = pos + max_width;
        if (below >= grid_size) return true;
        int16_t below_cell = state.grid[below];
        return below_cell == CELL_WALL || below_cell == CELL_POWERUP || below_cell >= CELL_SNAKE_BASE;
    };

    auto push_state = [&](int pos, int unsupported_steps, int16_t dist) {
        int idx = pos * stride + unsupported_steps;
        if (state_dist[idx] != -1) return;
        state_dist[idx] = dist;
        q.push(idx);
        result.visited_states++;
        if (result.best_dist_to_power[pos] == -1 || dist < result.best_dist_to_power[pos]) {
            result.best_dist_to_power[pos] = dist;
        }
    };

    for (int pos = 0; pos < grid_size; ++pos) {
        if (!is_playable_cell(pos)) continue;
        if (state.grid[pos] == CELL_POWERUP) {
            push_state(pos, 0, 0);
        }
    }

    if (q.empty()) {
        return result;
    }

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    while (!q.empty()) {
        if (out_of_time()) {
            result.completed = false;
            break;
        }

        int packed = q.front();
        q.pop();

        int pos = packed / stride;
        int unsupported_steps = packed % stride;
        int16_t dist = state_dist[packed];

        int px = pos % max_width;
        int py = pos / max_width;

        for (int a = 0; a < 4; ++a) {
            int nx = px + dx[a];
            int ny = py + dy[a];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
            int n_pos = ny * max_width + nx;
            if (!is_playable_cell(n_pos)) continue;

            int16_t n_cell = state.grid[n_pos];
            if (n_cell == CELL_WALL) continue;

            int next_unsupported_steps = has_support_below(n_pos) ? 0 : (unsupported_steps + 1);
            if (next_unsupported_steps > snake_len) continue;

            push_state(n_pos, next_unsupported_steps, static_cast<int16_t>(dist + 1));
        }
    }

    return result;
}



int main() {
    signal(SIGSEGV, crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);
    signal(SIGFPE, crash_signal_handler);
    signal(SIGILL, crash_signal_handler);
    std::set_terminate(bot_terminate_handler);
#ifdef LOCAL_TEST
    run_local_tests();
#endif
    int my_id;
    cin >> my_id;
    cin >> world_width;
    cin >> world_height;

    // In the first turn, we don't know total powerups yet, but we read the initial state.
    // Let's store the initial rows to parse them later once we know the dimensions.
    vector<string> initial_rows(world_height);
    cin >> ws;
    for (int i = 0; i < world_height; i++) {
        getline(cin, initial_rows[i]);
    }
    
    int snakebots_per_player;
    cin >> snakebots_per_player;
    
    vector<int> my_snakebots;
    int my_snakebot_id;
    for (int i = 0; i < snakebots_per_player; i++) {
        cin >> my_snakebot_id;
        my_snakebots.push_back(my_snakebot_id);
    }
    int opp_snakebot_id;
    for (int i = 0; i < snakebots_per_player; i++) {
        cin >> opp_snakebot_id;
    }
    
    // Commands array and counter omitted for logic bot
    
    // game loop
    bool is_first_turn = true;
    int counter = 0;
    GameState state;
    vector<int16_t> static_walls;

    auto loop_start = high_resolution_clock::now();  // Global loop start
    while (1) {
        counter++;
        g_turn_counter = counter;
        auto iter_start = high_resolution_clock::now();  // Per-iteration start
        turn_start_time = iter_start;
        begin_turn_timing_budget();
        
        int power_source_count = 0;
        if (!(cin >> power_source_count)) {
            break;
        }

        vector<int> powerup_positions;
        powerup_positions.reserve(power_source_count);

        if (is_first_turn) {
            total_powerups_count = power_source_count;
            max_len = 3 + total_powerups_count;
            max_width = (2 * max_len) + world_width;
            max_height = (2 * max_len) + world_height;
            grid_size = max_width * max_height;

            state.grid.assign(grid_size, CELL_EMPTY);

            // Now parse the initial_rows map into the center of the new grid.
            int start_x = max_len;
            int start_y = max_len; // Put it in the middle vertically as well
            
            for (int y = 0; y < world_height; y++) {
                for (int x = 0; x < world_width; x++) {
                    if (initial_rows[y][x] == '#') {
                        state.grid[(start_y + y) * max_width + (start_x + x)] = CELL_WALL;
                    }
                }
            }
            static_walls = state.grid;

            map_has_open_left_edge = false;
            map_has_open_right_edge = false;
            map_has_open_floor_edge = false;
            for (int y = 0; y < world_height; ++y) {
                int left_pos = (max_len + y) * max_width + max_len;
                int right_pos = (max_len + y) * max_width + (max_len + world_width - 1);
                if (static_walls[left_pos] != CELL_WALL) map_has_open_left_edge = true;
                if (static_walls[right_pos] != CELL_WALL) map_has_open_right_edge = true;
            }
            for (int x = 0; x < world_width; ++x) {
                int bottom_pos = (max_len + world_height - 1) * max_width + (max_len + x);
                if (static_walls[bottom_pos] != CELL_WALL) map_has_open_floor_edge = true;
            }
            is_first_turn = false;
        }

        // Reset dynamic state (snakes, powerups) while preserving static walls.
        state.grid = static_walls;

        for (int i = 0; i < power_source_count; i++) {
            int x = 0;
            int y = 0;
            cin >> x >> y;
            
            int sx = max_len + x;
            int sy = max_len + y;
            int spos = sy * max_width + sx;
            state.grid[spos] = CELL_POWERUP;
            powerup_positions.push_back(spos);
        }

        int snakebot_count = 0;
        cin >> snakebot_count;
        
        // Reset dynamic snake states arrays since we overwrite parsed data
        state.my_snakes.clear();
        state.opp_snakes.clear();
        
        for (int i = 0; i < snakebot_count; i++) {
            int snakebot_id;
            string body_str;
            if (!(cin >> snakebot_id >> body_str)) {
                break;
            }

            Snake s;
            s.id = snakebot_id;
            s.is_alive = true;
            s.body.assign(max_len, -1);
            
            // Parse body "x,y:x2,y2"
            int body_count = 0;
            size_t pos = 0;
            while (pos < body_str.length()) {
                size_t comma = body_str.find(',', pos);
                size_t colon = body_str.find(':', comma);
                if (colon == string::npos) colon = body_str.length();
                
                int x = stoi(body_str.substr(pos, comma - pos));
                int y = stoi(body_str.substr(comma + 1, colon - comma - 1));
                
                int sx = max_len + x;
                int sy = max_len + y;
                
                if (body_count < max_len) {
                    s.body[body_count++] = sy * max_width + sx;
                }
                state.grid[sy * max_width + sx] = CELL_SNAKE_BASE + s.id;
                pos = colon + 1;
            }
            s.length = body_count;
            if (s.length <= 0) {
                continue;
            }
            s.head_idx = 0;
            s.tail_idx = s.length - 1;
            // Add to the correct player's list
            bool is_mine = false;
            for (int id : my_snakebots) {
                if (id == s.id) {
                    is_mine = true;
                    break;
                }
            }
            if (is_mine) {
                state.my_snakes.push_back(s);
            } else {
                state.opp_snakes.push_back(s);
            }
        }

        // --- DEBUG Dump Grid State on Turn 1 --- (Removed to save ms)

        // --- DECISION LOGIC ---
        
        // 1. Calculate Voronoi board control
        GameState::VoronoiResult v_res = state.calculate_voronoi();

        DangerMapResult danger = build_enemy_danger_map(state, BotTuning::DANGER_MAP_PROJECTION_DEPTH);
        vector<SnakeRole> snake_roles = assign_dynamic_roles(state, powerup_positions, counter);

        // Pre-compute wall-aware distance maps for each snake (once per turn)
        vector<vector<int16_t>> fwd_bfs_by_snake_idx(state.my_snakes.size());
        {
            PhaseBudgetScope phase_scope(PHASE_FORWARD_BFS);
            for (size_t i = 0; i < state.my_snakes.size(); ++i) {
                if (!state.my_snakes[i].is_alive || out_of_time()) continue;
                fwd_bfs_by_snake_idx[i] = build_forward_bfs_dist_from_head(state, state.my_snakes[i]);
            }
        }

        // Pre-compute backward BFS (distance to nearest powerup from any cell) per snake length
        vector<BackwardPowerBFSResult> bk_bfs_by_snake_idx(state.my_snakes.size());
        if (!powerup_positions.empty()) {
            PhaseBudgetScope phase_scope(PHASE_BACKWARD_BFS);
            GameState bk_state = state;
            bk_state.opp_snakes.clear();
            for (size_t i = 0; i < state.my_snakes.size(); ++i) {
                if (!state.my_snakes[i].is_alive || out_of_time()) continue;
                bk_bfs_by_snake_idx[i] = build_backward_power_bfs_map(bk_state, state.my_snakes[i].length);
            }
        }

        TargetPlanResult target_plan = assign_persistent_targets(state, powerup_positions, snake_roles, danger, fwd_bfs_by_snake_idx);

        auto manhattan = [](int p1, int p2) {
            int x1 = p1 % max_width;
            int y1 = p1 / max_width;
            int x2 = p2 % max_width;
            int y2 = p2 / max_width;
            return abs(x1 - x2) + abs(y1 - y2);
        };
        
        // 2. Decide actions for each of my snakes independently
        vector<string> action_strs;
        vector<int> current_my_actions(state.my_snakes.size(), 0);
        vector<int> default_my_actions(state.my_snakes.size(), 0);
        for (size_t i = 0; i < state.my_snakes.size(); ++i) {
            default_my_actions[i] = infer_previous_action(state.my_snakes[i]);
        }
        vector<int> default_opp_actions(state.opp_snakes.size(), 0);
        for (size_t i = 0; i < state.opp_snakes.size(); ++i) {
            default_opp_actions[i] = infer_previous_action(state.opp_snakes[i]);
        }
        int dx[] = {0, 0, -1, 1};
        int dy[] = {-1, 1, 0, 0};

        auto append_action_with_target = [&](size_t idx, int action) {
            const Snake& snake = state.my_snakes[idx];
            string cmd = to_string(snake.id) + " " + action_to_string(action);
            if (idx < target_plan.target_positions.size()) {
                int target_pos = target_plan.target_positions[idx];
                if (target_pos >= 0) {
                    int tx = (target_pos % max_width) - max_len;
                    int ty = (target_pos / max_width) - max_len;
                    cmd += " " + to_string(tx) + " " + to_string(ty);
                }
            }
            action_strs.push_back(cmd);
        };
        
        for (size_t s_idx = 0; s_idx < state.my_snakes.size(); ++s_idx) {
            Snake& s = state.my_snakes[s_idx];
            if (!s.is_alive) continue;
            
            int head_pos = s.body[s.head_idx];
            int hx = head_pos % max_width;
            int hy = head_pos / max_width;
            int fallback_action = infer_previous_action(s);
            int best_action = fallback_action;
            int best_score = -999999;
            SnakeRole role = snake_roles[s_idx];
            int target_pos = (s_idx < target_plan.target_positions.size()) ? target_plan.target_positions[s_idx] : -1;
            bool target_is_power = (s_idx < target_plan.target_is_power.size()) ? target_plan.target_is_power[s_idx] : false;
            bool overlap_enemy_danger = (head_pos >= 0 && head_pos < grid_size && danger.danger_map[head_pos] > 0);
            int nearest_enemy_head_dist = 999999;
            int target_stall_turns = 0;
            bool single_snake_mode = (state.my_snakes.size() == 1 && state.opp_snakes.size() <= 1);
            bool simple_single_pathing = (BotTuning::SIMPLE_LONG_RANGE_PATHING && single_snake_mode);
            for (const auto& opp : state.opp_snakes) {
                if (!opp.is_alive || opp.length <= 0) continue;
                int opp_head = opp.body[opp.head_idx];
                int d = abs(hx - (opp_head % max_width)) + abs(hy - (opp_head / max_width));
                nearest_enemy_head_dist = min(nearest_enemy_head_dist, d);
            }

            if (target_is_power && target_pos != -1
                && s_idx < fwd_bfs_by_snake_idx.size() && !fwd_bfs_by_snake_idx[s_idx].empty()) {
                int dist_to_target = fwd_bfs_by_snake_idx[s_idx][target_pos];
                if (dist_to_target >= 0) {
                    int last_target = -1;
                    int last_dist = -1;
                    auto it_t = g_target_progress_last_target.find(s.id);
                    if (it_t != g_target_progress_last_target.end()) last_target = it_t->second;
                    auto it_d = g_target_progress_last_dist.find(s.id);
                    if (it_d != g_target_progress_last_dist.end()) last_dist = it_d->second;

                    int stall = 0;
                    auto it_s = g_target_progress_stall_turns.find(s.id);
                    if (it_s != g_target_progress_stall_turns.end()) stall = it_s->second;

                    int same_target_turns = 0;
                    auto it_same = g_target_same_target_turns.find(s.id);
                    if (it_same != g_target_same_target_turns.end()) same_target_turns = it_same->second;

                    if (last_target != target_pos) {
                        stall = 0;
                        same_target_turns = 0;
                    } else if (last_dist >= 0 && dist_to_target >= last_dist) {
                        stall++;
                        same_target_turns++;
                    } else {
                        stall = 0;
                        same_target_turns++;
                    }

                    g_target_progress_last_target[s.id] = target_pos;
                    g_target_progress_last_dist[s.id] = dist_to_target;
                    g_target_progress_stall_turns[s.id] = stall;
                    g_target_same_target_turns[s.id] = same_target_turns;
                    target_stall_turns = stall;

                    if (same_target_turns >= 36 && powerup_positions.size() > 1) {
                        block_target_for_turns(s.id, target_pos, 18);
                        g_persistent_target_by_snake.erase(s.id);
                        g_persistent_target_is_power.erase(s.id);
                        clear_target_progress_state(s.id);
                        invalidate_snake_target_cache(s.id);
                        target_pos = -1;
                        target_is_power = false;
                        target_stall_turns = 0;
                    }

                    if (stall >= 8) {
                        if (powerup_positions.size() > 1) {
                            block_target_for_turns(s.id, target_pos, 12);
                            g_persistent_target_by_snake.erase(s.id);
                            g_persistent_target_is_power.erase(s.id);
                            clear_target_progress_state(s.id);
                            invalidate_snake_target_cache(s.id);
                            target_pos = -1;
                            target_is_power = false;
                            target_stall_turns = 0;
                        } else {
                            g_target_progress_stall_turns[s.id] = 8;
                            target_stall_turns = 8;
                        }
                    }
                }
            }

            if (out_of_time()) {
                current_my_actions[s_idx] = fallback_action;
                append_action_with_target(s_idx, fallback_action);
                continue;
            }

            if (BotModeConfig::PURE_DEEP_ONLY) {
                GameState pure_plan_state = state;
                pure_plan_state.opp_snakes.clear();
                int pure_action = -1;
                int candidate_action = -1;
                uint64_t current_hash = snake_body_hash(s);
                int pure_depth = (world_width >= 24 || world_height >= 18) ? 20 : 14;
                bool pure_use_power_gain = !powerup_positions.empty();
                const char* pure_mode = "fallback_legal";
                {
                    PhaseBudgetScope phase_scope(PHASE_POWER_PLANNER);
                    if (pure_use_power_gain) {
                        pure_action = first_action_to_powerup_gain_budgeted(pure_plan_state, s.id, pure_depth);
                        if (pure_action != -1) pure_mode = "power_gain_v3";
                    } else if (target_pos != -1) {
                        pure_action = first_action_to_target_progress_budgeted(pure_plan_state, s.id, target_pos, pure_depth);
                        if (pure_action != -1) pure_mode = "target_progress_v3";
                    } else {
                        pure_action = first_action_to_powerup_gain_budgeted(pure_plan_state, s.id, pure_depth);
                        if (pure_action != -1) pure_mode = "power_gain_no_target_v3";
                    }
                }
                if (pure_action == -1
                    && (target_pos == -1 || target_stall_turns == 0)
                    && s_idx < bk_bfs_by_snake_idx.size()
                    && !bk_bfs_by_snake_idx[s_idx].best_dist_to_power.empty()
                    && !out_of_time()) {
                    PhaseBudgetScope phase_scope(PHASE_PATH_AND_SCORING);
                    const auto& bk_dist = bk_bfs_by_snake_idx[s_idx].best_dist_to_power;
                    int best_action = -1;
                    int best_dist = 999999;
                    for (int ga = 0; ga < 4; ++ga) {
                        if (is_backward_action(s, ga)) continue;
                        int gnx = hx + dx[ga];
                        int gny = hy + dy[ga];
                        if (gnx < 0 || gnx >= max_width || gny < 0 || gny >= max_height) continue;
                        int gpos = gny * max_width + gnx;
                        int16_t gcell = state.grid[gpos];
                        if (gcell == CELL_WALL || gcell >= CELL_SNAKE_BASE) continue;
                        int16_t d = bk_dist[gpos];
                        if (d < 0) continue;
                        if (d < best_dist) {
                            best_dist = d;
                            best_action = ga;
                        }
                    }
                    if (best_action != -1) {
                        pure_action = best_action;
                        pure_mode = "backward_bfs_fallback_v3";
                    }
                }
                if (pure_action == -1 && target_pos != -1 && !out_of_time()) {
                    PhaseBudgetScope phase_scope(PHASE_POWER_PLANNER);
                    pure_action = first_action_to_target_progress_budgeted(pure_plan_state, s.id, target_pos, pure_depth);
                    if (pure_action != -1) pure_mode = "target_progress_fallback_v3";
                }
                if (pure_action == -1 && target_pos != -1 && !out_of_time()) {
                    PhaseBudgetScope phase_scope(PHASE_PATH_AND_SCORING);
                    pure_action = cached_gravity_path_action(state, s, target_pos, target_is_power);
                    if (pure_action != -1) pure_mode = "cached_path_fallback_v3";
                }
                if (pure_action == -1 && target_pos != -1 && !out_of_time()) {
                    PhaseBudgetScope phase_scope(PHASE_PATH_AND_SCORING);
                    pure_action = bfs_first_action_to_cell(state, s, target_pos, true);
                    if (pure_action != -1) pure_mode = "bfs_fallback_v3";
                }
                if (pure_action != -1) {
                    candidate_action = pure_action;
                    GameState next_state = state;
                    vector<int> sim_my_actions = default_my_actions;
                    sim_my_actions[s_idx] = pure_action;
                    next_state.simulate(sim_my_actions, default_opp_actions);
                    const Snake* next_self = next_state.find_my_snake_by_id(s.id);
                    bool progress_ok = true;
                    if (next_self != nullptr && next_self->is_alive && target_pos != -1) {
                        int curr_dist = gravity_aware_distance_to_target(state, s, target_pos);
                        int next_dist = gravity_aware_distance_to_target(next_state, *next_self, target_pos);
                        int next_head = next_self->body[next_self->head_idx];
                        bool immediate_collect = (next_head >= 0 && next_head < grid_size && next_state.grid[next_head] == CELL_POWERUP);
                        if (!immediate_collect && curr_dist < 999999 && next_dist > curr_dist) {
                            progress_ok = false;
                        }
                    }
                    if (next_self != nullptr
                        && next_self->is_alive
                        && snake_body_hash(*next_self) != current_hash
                        && progress_ok) {
                        mylog << "PURE_DEEP_DECISION turn=" << counter
                              << " snake=" << s.id
                            << " power_count=" << powerup_positions.size()
                            << " my_count=" << state.my_snakes.size()
                            << " opp_count=" << state.opp_snakes.size()
                              << " head_x=" << (hx - max_len)
                              << " head_y=" << (hy - max_len)
                              << " target_x=" << ((target_pos >= 0) ? ((target_pos % max_width) - max_len) : -999)
                              << " target_y=" << ((target_pos >= 0) ? ((target_pos / max_width) - max_len) : -999)
                              << " target_is_power=" << (target_is_power ? 1 : 0)
                            << " mode=" << pure_mode
                              << " action=" << action_to_string(pure_action)
                              << "\n";
                        current_my_actions[s_idx] = pure_action;
                        append_action_with_target(s_idx, pure_action);
                        continue;
                    }
                }
                int fallback_legal = -1;
                int first_alive_fallback = -1;
                for (int fa = 0; fa < 4; ++fa) {
                    if (is_backward_action(s, fa)) continue;
                    int fnx = hx + dx[fa];
                    int fny = hy + dy[fa];
                    if (fnx < 0 || fnx >= max_width || fny < 0 || fny >= max_height) continue;
                    int fpos = fny * max_width + fnx;
                    int16_t fcell = state.grid[fpos];
                    if (fcell == CELL_WALL || fcell >= CELL_SNAKE_BASE) continue;

                    GameState fallback_state = state;
                    vector<int> fallback_my_actions = default_my_actions;
                    fallback_my_actions[s_idx] = fa;
                    fallback_state.simulate(fallback_my_actions, default_opp_actions);
                    const Snake* fallback_self = fallback_state.find_my_snake_by_id(s.id);
                    if (fallback_self == nullptr || !fallback_self->is_alive) continue;
                    if (first_alive_fallback == -1) first_alive_fallback = fa;
                    if (snake_body_hash(*fallback_self) != current_hash) {
                        fallback_legal = fa;
                        break;
                    }
                }
                if (fallback_legal == -1) {
                    fallback_legal = (first_alive_fallback != -1) ? first_alive_fallback : first_legal_action_basic(state, s);
                }
                bool fallback_same_state = false;
                if (fallback_legal != -1) {
                    GameState fallback_state = state;
                    vector<int> fallback_my_actions = default_my_actions;
                    fallback_my_actions[s_idx] = fallback_legal;
                    fallback_state.simulate(fallback_my_actions, default_opp_actions);
                    const Snake* fallback_self = fallback_state.find_my_snake_by_id(s.id);
                    fallback_same_state = (fallback_self != nullptr
                        && fallback_self->is_alive
                        && snake_body_hash(*fallback_self) == current_hash);
                }
                if (fallback_same_state && target_pos != -1 && powerup_positions.size() > 1) {
                    block_target_for_turns(s.id, target_pos, 12);
                    g_persistent_target_by_snake.erase(s.id);
                    g_persistent_target_is_power.erase(s.id);
                    clear_target_progress_state(s.id);
                    invalidate_snake_target_cache(s.id);
                }
                mylog << "PURE_DEEP_FALLBACK turn=" << counter
                      << " snake=" << s.id
                      << " power_count=" << powerup_positions.size()
                      << " head_x=" << (hx - max_len)
                      << " head_y=" << (hy - max_len)
                      << " target_x=" << ((target_pos >= 0) ? ((target_pos % max_width) - max_len) : -999)
                      << " target_y=" << ((target_pos >= 0) ? ((target_pos / max_width) - max_len) : -999)
                      << " target_is_power=" << (target_is_power ? 1 : 0)
                      << " candidate_mode=" << pure_mode
                      << " candidate_action=" << (candidate_action != -1 ? action_to_string(candidate_action) : string("NONE"))
                      << " fallback_same_state=" << (fallback_same_state ? 1 : 0)
                      << " fallback_action=" << action_to_string(fallback_legal)
                      << "\n";
                current_my_actions[s_idx] = fallback_legal;
                append_action_with_target(s_idx, fallback_legal);
                continue;
            }

            if (BotModeConfig::PURE_BFS_ONLY) {
                int pure_action = -1;
                if (target_pos != -1) {
                    {
                        PhaseBudgetScope phase_scope(PHASE_PATH_AND_SCORING);
                        bool allow_snake_cells_for_path = single_snake_mode;
                        pure_action = bfs_first_action_to_cell(state, s, target_pos, allow_snake_cells_for_path);
                    }
                    if (pure_action != -1 && !is_backward_action(s, pure_action)) {
                        GameState next_state = state;
                        vector<int> sim_my_actions = default_my_actions;
                        sim_my_actions[s_idx] = pure_action;
                        next_state.simulate(sim_my_actions, default_opp_actions);
                        const Snake* next_self = next_state.find_my_snake_by_id(s.id);
                        if (next_self != nullptr && next_self->is_alive) {
                            current_my_actions[s_idx] = pure_action;
                            append_action_with_target(s_idx, pure_action);
                            continue;
                        }
                    }
                }
                int fallback_legal = first_legal_action_basic(state, s);
                current_my_actions[s_idx] = fallback_legal;
                append_action_with_target(s_idx, fallback_legal);
                continue;
            }

            if (!BotModeConfig::PURE_HEURISTIC_ONLY && simple_single_pathing && target_is_power && target_pos != -1 && !out_of_time()) {
                int seq_action = -1;
                {
                    PhaseBudgetScope phase_scope(PHASE_POWER_PLANNER);
                    GameState simple_plan_state = state;
                    simple_plan_state.opp_snakes.clear();
                    int start_depth = (world_width >= 24 || world_height >= 18) ? 18 : 12;
                    seq_action = first_action_to_powerup_gain_budgeted(simple_plan_state, s.id, start_depth);
                }

                if (seq_action == -1) {
                    int best_d = 999999;
                    {
                        PhaseBudgetScope phase_scope(PHASE_PATH_AND_SCORING);
                        for (int a = 0; a < 4; ++a) {
                            if (is_backward_action(s, a)) continue;
                            int nx = hx + dx[a];
                            int ny = hy + dy[a];
                            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
                            int n_pos = ny * max_width + nx;
                            if (state.grid[n_pos] == CELL_WALL || state.grid[n_pos] >= CELL_SNAKE_BASE) continue;
                            int d = shortest_path_distance_walls_only(state, n_pos, target_pos);
                            if (d < best_d) {
                                best_d = d;
                                seq_action = a;
                            }
                        }
                    }
                }

                if (seq_action != -1) {
                    GameState dbg_state = state;
                    vector<int> dbg_my_actions = default_my_actions;
                    dbg_my_actions[s_idx] = seq_action;
                    dbg_state.simulate(dbg_my_actions, default_opp_actions);
                    const Snake* dbg_self = dbg_state.find_my_snake_by_id(s.id);
                    if (dbg_self == nullptr || !dbg_self->is_alive) {
                        mylog << "SIMPLE_PATH_CRASH turn=" << counter
                              << " snake=" << s.id
                              << " action=" << action_to_string(seq_action)
                              << " target_pos=" << target_pos
                              << " mode=deep_first"
                              << '\n';
                    }

                    current_my_actions[s_idx] = seq_action;
                    append_action_with_target(s_idx, seq_action);
                    continue;
                }
            }

            if (!BotModeConfig::PURE_HEURISTIC_ONLY && overlap_enemy_danger && danger.danger_map[head_pos] >= danger.high_risk_threshold && nearest_enemy_head_dist <= 5 && !out_of_time()) {
                int local_min_depth = 2;
                int local_max_depth = (state.my_snakes.size() > 1) ? 4 : 6;
                int local_ab_action = -1;
                {
                    PhaseBudgetScope phase_scope(PHASE_LOCAL_COMBAT);
                    local_ab_action = choose_local_alpha_beta_action_budgeted(state, s.id, local_min_depth, local_max_depth);
                }
                if (local_ab_action != -1) {
                    current_my_actions[s_idx] = local_ab_action;
                    append_action_with_target(s_idx, local_ab_action);
                    continue;
                }
            }

            if (!BotModeConfig::PURE_HEURISTIC_ONLY && single_snake_mode && target_is_power && target_pos != -1 && !out_of_time()) {
                GameState single_plan_state = state;
                single_plan_state.opp_snakes.clear();
                int single_start_depth = 24;
                if ((world_width * world_height) >= 500) single_start_depth = 48;
                int single_plan_action = -1;
                {
                    PhaseBudgetScope phase_scope(PHASE_POWER_PLANNER);
                    single_plan_action = first_action_to_powerup_gain_budgeted(single_plan_state, s.id, single_start_depth);
                }
                if (single_plan_action != -1 && !is_backward_action(s, single_plan_action) && !out_of_time()) {
                    GameState single_next_state = state;
                    vector<int> single_my_actions = default_my_actions;
                    single_my_actions[s_idx] = single_plan_action;
                    single_next_state.simulate(single_my_actions, default_opp_actions);
                    const Snake* single_next_self = single_next_state.find_my_snake_by_id(s.id);
                    if (single_next_self != nullptr && single_next_self->is_alive) {
                        current_my_actions[s_idx] = single_plan_action;
                        append_action_with_target(s_idx, single_plan_action);
                        continue;
                    }
                }

                if (powerup_positions.size() > 1) {
                    int cached_action = -1;
                    {
                        PhaseBudgetScope phase_scope(PHASE_PATH_AND_SCORING);
                        cached_action = cached_gravity_path_action(state, s, target_pos, true);
                    }
                    if (cached_action != -1 && !is_backward_action(s, cached_action) && !out_of_time()) {
                        GameState cached_next_state = state;
                        vector<int> cached_my_actions = default_my_actions;
                        cached_my_actions[s_idx] = cached_action;
                        cached_next_state.simulate(cached_my_actions, default_opp_actions);
                        const Snake* cached_next_self = cached_next_state.find_my_snake_by_id(s.id);
                        if (cached_next_self != nullptr && cached_next_self->is_alive) {
                            current_my_actions[s_idx] = cached_action;
                            append_action_with_target(s_idx, cached_action);
                            consume_cached_gravity_path_step(s.id);
                            continue;
                        }
                        invalidate_snake_target_cache(s.id);
                    }

                    int seq_action = -1;
                    {
                        PhaseBudgetScope phase_scope(PHASE_PATH_AND_SCORING);
                        seq_action = bfs_first_action_to_cell(state, s, target_pos, true);
                    }
                    if (seq_action != -1 && !is_backward_action(s, seq_action) && !out_of_time()) {
                        GameState seq_next_state = state;
                        vector<int> seq_my_actions = default_my_actions;
                        seq_my_actions[s_idx] = seq_action;
                        seq_next_state.simulate(seq_my_actions, default_opp_actions);
                        const Snake* seq_next_self = seq_next_state.find_my_snake_by_id(s.id);
                        if (seq_next_self != nullptr && seq_next_self->is_alive) {
                            current_my_actions[s_idx] = seq_action;
                            append_action_with_target(s_idx, seq_action);
                            continue;
                        }
                    }
                }
            }

            if (!BotModeConfig::PURE_HEURISTIC_ONLY
                && !single_snake_mode
                && target_is_power
                && target_pos != -1
                && powerup_positions.size() > 1
                && nearest_enemy_head_dist >= 4
                && !out_of_time()) {
                int tx = target_pos % max_width;
                if (tx > hx) {
                    int nx = hx + 1;
                    int ny = hy;
                    int npos = ny * max_width + nx;
                    if (!is_backward_action(s, 3) && nx < max_width && state.grid[npos] != CELL_WALL && state.grid[npos] < CELL_SNAKE_BASE) {
                        current_my_actions[s_idx] = 3;
                        append_action_with_target(s_idx, 3);
                        continue;
                    }
                } else if (tx < hx) {
                    int nx = hx - 1;
                    int ny = hy;
                    int npos = ny * max_width + nx;
                    if (!is_backward_action(s, 2) && nx >= 0 && state.grid[npos] != CELL_WALL && state.grid[npos] < CELL_SNAKE_BASE) {
                        current_my_actions[s_idx] = 2;
                        append_action_with_target(s_idx, 2);
                        continue;
                    }
                }
            }

            int assigned_target_dist = 999999;
            if (target_pos >= 0 && s_idx < fwd_bfs_by_snake_idx.size() && !fwd_bfs_by_snake_idx[s_idx].empty()) {
                int16_t d = fwd_bfs_by_snake_idx[s_idx][target_pos];
                if (d >= 0) assigned_target_dist = static_cast<int>(d);
            }

            bool large_map = (world_width * world_height) >= 500;
            bool strategic_long_path_mode = (
                target_is_power
                && target_pos != -1
                && nearest_enemy_head_dist >= 6
                && (assigned_target_dist >= 10 || (assigned_target_dist >= 6 && large_map))
            );

            // Commit mode for long paths: prefer deeper progress toward assigned
            // power targets under low pressure, while still validating immediate survival.
            if (!BotModeConfig::PURE_HEURISTIC_ONLY
                && !single_snake_mode
                && strategic_long_path_mode
                && target_stall_turns >= 3
                && s_idx < bk_bfs_by_snake_idx.size()
                && !bk_bfs_by_snake_idx[s_idx].best_dist_to_power.empty()
                && head_pos >= 0 && head_pos < grid_size) {
                // 1) Physics-aware planner first (no ghost-risk filters in this mode)
                GameState commit_plan_state = state;
                commit_plan_state.opp_snakes.clear();
                int commit_action = -1;
                int commit_start_depth = 8;
                if (assigned_target_dist >= 16) commit_start_depth = 12;
                if (assigned_target_dist >= 24) commit_start_depth = 16;
                {
                    PhaseBudgetScope phase_scope(PHASE_POWER_PLANNER);
                    commit_action = first_action_to_target_progress_budgeted(commit_plan_state, s.id, target_pos, commit_start_depth);
                }
                if (commit_action != -1 && !is_backward_action(s, commit_action) && !out_of_time()) {
                    GameState commit_next_state = state;
                    vector<int> commit_my_actions = default_my_actions;
                    commit_my_actions[s_idx] = commit_action;
                    commit_next_state.simulate(commit_my_actions, default_opp_actions);
                    const Snake* commit_next_self = commit_next_state.find_my_snake_by_id(s.id);
                    if (commit_next_self != nullptr && commit_next_self->is_alive) {
                        bool commit_progress_ok = true;
                        if (target_pos != -1) {
                            int curr_dist = bfs_distance_snake_head_to_cell(state, s, target_pos);
                            int next_dist = bfs_distance_snake_head_to_cell(commit_next_state, *commit_next_self, target_pos);
                            int next_head = commit_next_self->body[commit_next_self->head_idx];
                            bool immediate_collect = (next_head >= 0 && next_head < grid_size && commit_next_state.grid[next_head] == CELL_POWERUP);
                            if (!immediate_collect && curr_dist < 999999 && next_dist > curr_dist + 1) {
                                commit_progress_ok = false;
                            }
                        }
                        if (commit_progress_ok) {
                            current_my_actions[s_idx] = commit_action;
                            append_action_with_target(s_idx, commit_action);
                            continue;
                        }
                    }
                }

                // 2) Fallback: backward-BFS gradient
                const auto& bk_dist = bk_bfs_by_snake_idx[s_idx].best_dist_to_power;
                int best_action = -1;
                int best_dist = 999999;
                for (int ga = 0; ga < 4; ++ga) {
                    if (is_backward_action(s, ga)) continue;
                    int gnx = hx + dx[ga];
                    int gny = hy + dy[ga];
                    if (gnx < 0 || gnx >= max_width || gny < 0 || gny >= max_height) continue;
                    int gpos = gny * max_width + gnx;
                    int16_t gcell = state.grid[gpos];
                    if (gcell == CELL_WALL || gcell >= CELL_SNAKE_BASE) continue;
                    int16_t d = bk_dist[gpos];
                    if (d < 0) continue;
                    if (d < best_dist) {
                        best_dist = d;
                        best_action = ga;
                    }
                }

                if (best_action != -1 && !out_of_time()) {
                    GameState commit_next_state = state;
                    vector<int> commit_my_actions = default_my_actions;
                    commit_my_actions[s_idx] = best_action;
                    commit_next_state.simulate(commit_my_actions, default_opp_actions);
                    const Snake* commit_next_self = commit_next_state.find_my_snake_by_id(s.id);
                    if (commit_next_self != nullptr && commit_next_self->is_alive) {
                        current_my_actions[s_idx] = best_action;
                        append_action_with_target(s_idx, best_action);
                        continue;
                    }
                }
            }

            // --- BFS DIRECT PATH TO ASSIGNED TARGET ---
            // When a target is assigned and no emergency is active, use BFS to
            // find and follow the shortest path directly rather than relying on
            // the weak manhattan gradient in the scoring loop.
            if (!BotModeConfig::PURE_HEURISTIC_ONLY && target_pos != -1 && !out_of_time() && nearest_enemy_head_dist > 2) {
                int bfs_action = -1;
                PhaseBudgetScope phase_scope(PHASE_PATH_AND_SCORING);

                // For power targets, prefer descending the precomputed backward-BFS
                // distance gradient (real traversable distance to nearest power).
                if (target_is_power
                    && !single_snake_mode
                    && strategic_long_path_mode
                    && s_idx < bk_bfs_by_snake_idx.size()
                    && !bk_bfs_by_snake_idx[s_idx].best_dist_to_power.empty()
                    && head_pos >= 0 && head_pos < grid_size) {
                    const auto& bk_dist = bk_bfs_by_snake_idx[s_idx].best_dist_to_power;
                    int current_dist = bk_dist[head_pos];
                    int best_dist = 999999;
                    int best_action = -1;
                    for (int ga = 0; ga < 4; ++ga) {
                        if (is_backward_action(s, ga)) continue;
                        int gnx = hx + dx[ga];
                        int gny = hy + dy[ga];
                        if (gnx < 0 || gnx >= max_width || gny < 0 || gny >= max_height) continue;
                        int gpos = gny * max_width + gnx;
                        int16_t gcell = state.grid[gpos];
                        if (gcell == CELL_WALL || gcell >= CELL_SNAKE_BASE) continue;
                        int16_t d = bk_dist[gpos];
                        if (d < 0) continue;
                        if (d < best_dist) {
                            best_dist = d;
                            best_action = ga;
                        }
                    }

                    if (best_action != -1) {
                        bool improves = (current_dist < 0) || (best_dist < current_dist);
                        if (improves || target_stall_turns >= 2) {
                            bfs_action = best_action;
                        }
                    }
                }

                if (bfs_action == -1) {
                    bool allow_snake_cells_for_path = (single_snake_mode || strategic_long_path_mode);
                    bfs_action = bfs_first_action_to_cell(state, s, target_pos, allow_snake_cells_for_path);
                }
                if (bfs_action != -1 && !is_backward_action(s, bfs_action)) {
                    int bnx = hx + dx[bfs_action];
                    int bny = hy + dy[bfs_action];
                    int bpos = bny * max_width + bnx;
                    bool bfs_safe = true;
                    bool relaxed_single_snake_power = (single_snake_mode && target_is_power);
                    int bfs_danger = (bpos >= 0 && bpos < grid_size) ? static_cast<int>(danger.danger_map[bpos]) : 0;
                    bool bfs_critical = (state.grid[bpos] == CELL_POWERUP && v_res.length_delta < 0);
                    if (!relaxed_single_snake_power
                        && bfs_danger >= danger.high_risk_threshold
                        && !bfs_critical
                        && nearest_enemy_head_dist <= 5)
                        bfs_safe = false;
                    if (bfs_safe
                        && !relaxed_single_snake_power
                        && !(target_is_power && (strategic_long_path_mode || nearest_enemy_head_dist >= 4))
                        && !state.survives_flood_fill(bpos, max(2, s.length / 2))) {
                        bfs_safe = false;
                    }
                    if (bfs_safe && !out_of_time()) {
                        GameState bfs_next_state = state;
                        vector<int> bfs_my_actions = default_my_actions;
                        bfs_my_actions[s_idx] = bfs_action;
                        bfs_next_state.simulate(bfs_my_actions, default_opp_actions);
                        const Snake* bfs_next_self = bfs_next_state.find_my_snake_by_id(s.id);
                        if (bfs_next_self == nullptr || !bfs_next_self->is_alive)
                            bfs_safe = false;
                        else if (!relaxed_single_snake_power
                                 && !(target_is_power && nearest_enemy_head_dist >= 4)
                                 && bfs_next_state.count_safe_followups(*bfs_next_self) == 0)
                            bfs_safe = false;
                    }
                    if (bfs_safe) {
                        current_my_actions[s_idx] = bfs_action;
                        append_action_with_target(s_idx, bfs_action);
                        continue;
                    }
                }
            }

            if (!BotModeConfig::PURE_HEURISTIC_ONLY
                && target_is_power
                && !powerup_positions.empty()
                && !out_of_time()
                && !single_snake_mode
                && (strategic_long_path_mode || nearest_enemy_head_dist <= 5)) {
                GameState local_plan_state = state;
                local_plan_state.opp_snakes.clear();
                bool open_edge_map = map_has_open_left_edge || map_has_open_right_edge || map_has_open_floor_edge;
                int planner_depth = 5;
                if (strategic_long_path_mode) {
                    planner_depth = 10;
                    if (assigned_target_dist >= 16) planner_depth = 12;
                    if (assigned_target_dist >= 24) planner_depth = 16;
                } else if (open_edge_map) {
                    planner_depth = map_has_open_floor_edge ? 16 : 12;
                } else if (large_map && nearest_enemy_head_dist >= 6) {
                    planner_depth = 10;
                }
                int tactical_action = -1;
                {
                    PhaseBudgetScope phase_scope(PHASE_POWER_PLANNER);
                    if (target_pos != -1) {
                        tactical_action = first_action_to_target_progress_budgeted(local_plan_state, s.id, target_pos, planner_depth);
                    } else {
                        tactical_action = first_action_to_powerup_gain_budgeted(local_plan_state, s.id, planner_depth);
                    }
                }
                if (tactical_action != -1) {
                    int tnx = hx;
                    int tny = hy;
                    if (tactical_action == 0) tny -= 1;
                    else if (tactical_action == 1) tny += 1;
                    else if (tactical_action == 2) tnx -= 1;
                    else if (tactical_action == 3) tnx += 1;

                    bool tactical_acceptable = true;
                    if (tnx >= 0 && tnx < max_width && tny >= 0 && tny < max_height) {
                        int tpos = tny * max_width + tnx;
                        int danger_weight = (tpos >= 0 && tpos < grid_size) ? static_cast<int>(danger.danger_map[tpos]) : 0;
                        int16_t tcell = state.grid[tpos];
                        bool critical_objective = (tcell == CELL_POWERUP && v_res.length_delta < 0);
                        if (danger_weight >= danger.high_risk_threshold && !critical_objective) {
                            tactical_acceptable = false;
                        }
                    }

                    if (tactical_acceptable && target_pos != -1) {
                        GameState tactical_next_state = state;
                        vector<int> tactical_my_actions = default_my_actions;
                        tactical_my_actions[s_idx] = tactical_action;
                        tactical_next_state.simulate(tactical_my_actions, default_opp_actions);
                        const Snake* tactical_next_self = tactical_next_state.find_my_snake_by_id(s.id);
                        if (tactical_next_self == nullptr || !tactical_next_self->is_alive) {
                            tactical_acceptable = false;
                        } else {
                            int curr_dist = bfs_distance_snake_head_to_cell(state, s, target_pos);
                            int next_dist = bfs_distance_snake_head_to_cell(tactical_next_state, *tactical_next_self, target_pos);
                            int next_head = tactical_next_self->body[tactical_next_self->head_idx];
                            bool immediate_collect = (next_head >= 0 && next_head < grid_size && tactical_next_state.grid[next_head] == CELL_POWERUP);
                            if (!immediate_collect && curr_dist < 999999 && next_dist > curr_dist + 1) {
                                tactical_acceptable = false;
                            }
                        }
                    }

                    if (tactical_acceptable && !out_of_time()) {
                        if (!strategic_long_path_mode) {
                            int delayed_penalty = 0;
                            {
                                PhaseBudgetScope phase_scope(PHASE_PATH_AND_SCORING);
                                delayed_penalty = delayed_powerup_risk_penalty(state, s.id, tactical_action, 3);
                            }
                            if (delayed_penalty >= BotTuning::DELAYED_DEATH_PENALTY || delayed_penalty >= 2 * BotTuning::DELAYED_LOW_FOLLOWUP_PENALTY) {
                                tactical_acceptable = false;
                            }
                        }
                    }

                    if (tactical_acceptable) {
                        current_my_actions[s_idx] = tactical_action;
                        append_action_with_target(s_idx, tactical_action);
                        continue;
                    }
                }
            }

            if (!BotModeConfig::PURE_HEURISTIC_ONLY && target_is_power && target_pos != -1 && nearest_enemy_head_dist > 2 && !out_of_time()) {
                int cached_action = -1;
                {
                    PhaseBudgetScope phase_scope(PHASE_PATH_AND_SCORING);
                    cached_action = cached_gravity_path_action(state, s, target_pos, true);
                }
                if (cached_action != -1 && !is_backward_action(s, cached_action)) {
                    int cnx = hx + dx[cached_action];
                    int cny = hy + dy[cached_action];
                    int cpos = cny * max_width + cnx;
                    bool cached_safe = true;
                    int cached_danger = (cpos >= 0 && cpos < grid_size) ? static_cast<int>(danger.danger_map[cpos]) : 0;
                    bool cached_critical = (cpos >= 0 && cpos < grid_size && state.grid[cpos] == CELL_POWERUP && v_res.length_delta < 0);
                    if (cached_danger >= danger.high_risk_threshold && !cached_critical && nearest_enemy_head_dist <= 5)
                        cached_safe = false;
                    if (cached_safe
                        && !(target_is_power && nearest_enemy_head_dist >= 4)
                        && !state.survives_flood_fill(cpos, max(2, s.length / 2)))
                        cached_safe = false;
                    if (cached_safe && !out_of_time()) {
                        GameState cached_next_state = state;
                        vector<int> cached_my_actions = default_my_actions;
                        cached_my_actions[s_idx] = cached_action;
                        cached_next_state.simulate(cached_my_actions, default_opp_actions);
                        const Snake* cached_next_self = cached_next_state.find_my_snake_by_id(s.id);
                        if (cached_next_self == nullptr || !cached_next_self->is_alive)
                            cached_safe = false;
                        else if (!(target_is_power && nearest_enemy_head_dist >= 4)
                                 && cached_next_state.count_safe_followups(*cached_next_self) == 0)
                            cached_safe = false;
                    }
                    if (cached_safe) {
                        current_my_actions[s_idx] = cached_action;
                        append_action_with_target(s_idx, cached_action);
                        consume_cached_gravity_path_step(s.id);
                        continue;
                    }
                    invalidate_snake_target_cache(s.id);
                }
            }

            // Try all 4 directions (0:UP, 1:DOWN, 2:LEFT, 3:RIGHT)
            {
                PhaseBudgetScope phase_scope(PHASE_PATH_AND_SCORING);
                for (int a = 0; a < 4; ++a) {
                    if (out_of_time()) break;
                    if (is_backward_action(s, a)) continue;

                int nx = hx + dx[a];
                int ny = hy + dy[a];
                if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;

                int n_pos = ny * max_width + nx;
                int16_t next_cell = state.grid[n_pos];
                if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) {
                    continue;
                }

                int score = 0;
                bool critical_objective = (next_cell == CELL_POWERUP && v_res.length_delta < 0);
                int danger_weight = (n_pos >= 0 && n_pos < grid_size) ? static_cast<int>(danger.danger_map[n_pos]) : 0;

                if (danger_weight >= danger.high_risk_threshold && !critical_objective) {
                    score -= BotTuning::DANGER_HARD_BASE_PENALTY + danger_weight * BotTuning::DANGER_HARD_WEIGHT;
                } else {
                    score -= danger_weight * BotTuning::DANGER_SOFT_WEIGHT;
                }

                // Survive constraints first.
                if (!state.survives_flood_fill(n_pos, max(2, s.length / 2))) {
                    score -= BotTuning::FLOODFILL_DEATH_PENALTY;
                } else {
                    score += BotTuning::FLOODFILL_SURVIVE_BONUS;
                }

                bool defensive_mode = (s.length >= 4 && v_res.length_delta >= 0);

                // Prefer immediate powerup captures unless we're already ahead and switching defensive.
                if (next_cell == CELL_POWERUP) {
                    bool enclosed_map = !map_has_open_left_edge && !map_has_open_right_edge && !map_has_open_floor_edge;
                    if (enclosed_map && total_powerups_count == 2 && s.length >= 4) {
                        score -= BotTuning::ENCLOSED_POWERUP_PENALTY;
                    } else {
                        score += BotTuning::POWERUP_BONUS;
                    }
                }

                if (target_pos != -1) {
                    int target_dist_after = manhattan(n_pos, target_pos);
                    if (target_is_power
                        && s_idx < bk_bfs_by_snake_idx.size()
                        && !bk_bfs_by_snake_idx[s_idx].best_dist_to_power.empty()
                        && n_pos >= 0 && n_pos < grid_size) {
                        int16_t bfs_dist_after = bk_bfs_by_snake_idx[s_idx].best_dist_to_power[n_pos];
                        if (bfs_dist_after >= 0) {
                            target_dist_after = static_cast<int>(bfs_dist_after);
                        }
                    }

                    int target_weight = (target_is_power || role == SnakeRole::Collector)
                        ? BotTuning::TARGET_COLLECTOR_DIST_WEIGHT
                        : BotTuning::TARGET_SUPPORT_DIST_WEIGHT;
                    score -= target_dist_after * target_weight;
                    if (target_dist_after == 0) score += BotTuning::TARGET_REACHED_BONUS;
                }

                // Exclusive / contested heuristic proxy using distances to all heads.
                if (!powerup_positions.empty() && !out_of_time()) {
                    int best_powerup_eval = -999999;
                    for (int p_pos : powerup_positions) {
                        if (out_of_time()) break;
                        int my_dist = manhattan(n_pos, p_pos);
                        int opp_best_dist = 999999;
                        for (const auto& opp : state.opp_snakes) {
                            if (!opp.is_alive) continue;
                            opp_best_dist = min(opp_best_dist, manhattan(opp.body[opp.head_idx], p_pos));
                        }
                        int eval = defensive_mode ? (my_dist * 60) : (-my_dist * 100);
                        if (opp_best_dist < 999999) {
                            eval += (opp_best_dist * 10);
                        }
                        if (eval > best_powerup_eval) {
                            best_powerup_eval = eval;
                        }
                    }
                    score += best_powerup_eval;
                }

                // Strategic mode (length delta) from Voronoi summary.
                score += v_res.length_delta * BotTuning::VORONOI_LENGTH_DELTA_WEIGHT;
                score += v_res.my_exclusive_powerups * BotTuning::VORONOI_MY_EXCLUSIVE_WEIGHT;
                score -= v_res.opp_exclusive_powerups * BotTuning::VORONOI_OPP_EXCLUSIVE_WEIGHT;

                int nearest_enemy_now = 999999;
                int nearest_enemy_after = 999999;
                int nearest_short_enemy_after = 999999;
                for (const auto& opp : state.opp_snakes) {
                    if (!opp.is_alive || opp.length <= 0) continue;
                    int opp_head = opp.body[opp.head_idx];
                    int ox = opp_head % max_width;
                    int oy = opp_head / max_width;
                    nearest_enemy_now = min(nearest_enemy_now, abs(hx - ox) + abs(hy - oy));
                    int d_after = abs(nx - ox) + abs(ny - oy);
                    nearest_enemy_after = min(nearest_enemy_after, d_after);
                    if (opp.length <= 3) nearest_short_enemy_after = min(nearest_short_enemy_after, d_after);
                }

                int nearest_ally_after = 999999;
                for (size_t ally_idx = 0; ally_idx < state.my_snakes.size(); ++ally_idx) {
                    if (ally_idx == s_idx) continue;
                    const Snake& ally = state.my_snakes[ally_idx];
                    if (!ally.is_alive || ally.length <= 0) continue;
                    int ally_head = ally.body[ally.head_idx];
                    nearest_ally_after = min(
                        nearest_ally_after,
                        abs(nx - (ally_head % max_width)) + abs(ny - (ally_head / max_width))
                    );
                }

                if (role == SnakeRole::Collector) {
                    if (next_cell == CELL_POWERUP) score += BotTuning::ROLE_COLLECTOR_POWER_BONUS;
                    if (nearest_enemy_after <= 2) score -= BotTuning::ROLE_COLLECTOR_NEAR_ENEMY_PENALTY;
                } else if (role == SnakeRole::Support) {
                    if (nearest_ally_after < 999999) {
                        score += max(0, BotTuning::ROLE_SUPPORT_NEAR_ALLY_BONUS - nearest_ally_after * BotTuning::ROLE_SUPPORT_NEAR_ALLY_FALLOFF);
                    }
                    if (next_cell == CELL_POWERUP && nearest_ally_after <= 2) score -= BotTuning::ROLE_SUPPORT_POWER_NEAR_ALLY_PENALTY;

                    int collect_target = -1;
                    int best_collect_dist = 999999;
                    for (size_t ally_idx = 0; ally_idx < state.my_snakes.size(); ++ally_idx) {
                        if (ally_idx == s_idx) continue;
                        if (ally_idx >= snake_roles.size() || snake_roles[ally_idx] != SnakeRole::Collector) continue;
                        if (ally_idx >= target_plan.target_positions.size() || ally_idx >= target_plan.target_is_power.size()) continue;
                        if (!target_plan.target_is_power[ally_idx]) continue;
                        int at = target_plan.target_positions[ally_idx];
                        if (at == -1) continue;
                        int ally_head = state.my_snakes[ally_idx].body[state.my_snakes[ally_idx].head_idx];
                        int d = manhattan(ally_head, at);
                        if (d < best_collect_dist) {
                            best_collect_dist = d;
                            collect_target = at;
                        }
                    }
                    if (collect_target != -1 && best_collect_dist <= 1) {
                        int now_dist = manhattan(head_pos, collect_target);
                        int after_dist = manhattan(n_pos, collect_target);
                        if (after_dist > now_dist) score += BotTuning::SUPPORT_ROTATE_AWAY_BONUS;
                        if (after_dist <= 1) score -= BotTuning::SUPPORT_STAY_NEAR_COLLECT_PENALTY;
                    }
                } else if (role == SnakeRole::Defender) {
                    if (nearest_enemy_after <= 4) score += BotTuning::ROLE_DEFENDER_ENEMY_PROX_BONUS;
                    if (nearest_enemy_after > nearest_enemy_now) score -= BotTuning::ROLE_DEFENDER_RETREAT_PENALTY;
                } else if (role == SnakeRole::Suffocator) {
                    if (nearest_enemy_after < nearest_enemy_now) score += BotTuning::ROLE_SUFFOCATOR_APPROACH_BONUS;
                    if (nearest_enemy_after <= 1) score += BotTuning::ROLE_SUFFOCATOR_CLOSE_BONUS;
                } else if (role == SnakeRole::Killer) {
                    if (nearest_short_enemy_after < 999999) {
                        score += max(0, BotTuning::ROLE_KILLER_SHORT_ENEMY_BONUS - nearest_short_enemy_after * BotTuning::ROLE_KILLER_SHORT_ENEMY_FALLOFF);
                    }
                    if (next_cell == CELL_POWERUP) score -= BotTuning::ROLE_KILLER_POWER_PENALTY;
                }

                // Small wall-avoidance and center preference using playable-board coordinates.
                int playable_nx = nx - max_len;
                score -= abs(playable_nx - (world_width / 2));

                // Cheap 1-turn lookahead: avoid entering dead boxes and reward moves
                // that set up a guaranteed immediate apple on the next turn.
                if (!out_of_time()) {
                    GameState next_state = state;
                    vector<int> sim_my_actions = default_my_actions;
                    vector<int> sim_opp_actions = default_opp_actions;
                    sim_my_actions[s_idx] = a;
                    next_state.simulate(sim_my_actions, sim_opp_actions);

                    const Snake* next_self = next_state.find_my_snake_by_id(s.id);
                    if (next_self == nullptr || !next_self->is_alive) {
                        score -= BotTuning::SIM_DEATH_PENALTY;
                    } else {
                        uint64_t start_hash = snake_body_hash(s);
                        uint64_t next_hash = snake_body_hash(*next_self);
                        if (next_self->length < s.length) {
                            score -= BotTuning::SHORTER_SNAKE_FUTURE_PENALTY;
                        }
                        if (next_self->length == s.length && next_hash == start_hash) {
                            score -= BotTuning::SAME_STATE_OPPORTUNITY_PENALTY;
                        }

                        int simulated_head_pos = next_self->body[next_self->head_idx];
                        int shx = simulated_head_pos % max_width;
                        int shy = simulated_head_pos / max_width;
                        if (shx < max_len || shx >= max_len + world_width) {
                            score -= BotTuning::SIDE_EXIT_PENALTY;
                        }
                        if (shy >= max_len + world_height) {
                            score -= BotTuning::FLOOR_EXIT_PENALTY;
                        }
                        if (map_has_open_left_edge && hx == max_len && a == 2) {
                            score -= BotTuning::OPEN_EDGE_EXIT_PENALTY;
                        }
                        if (map_has_open_right_edge && hx == max_len + world_width - 1 && a == 3) {
                            score -= BotTuning::OPEN_EDGE_EXIT_PENALTY;
                        }

                        int followups = next_state.count_safe_followups(*next_self);
                        score += followups * BotTuning::FOLLOWUP_COUNT_WEIGHT;
                        if (followups == 0) score -= BotTuning::FOLLOWUP_ZERO_PENALTY;
                        if (followups == 1) score -= BotTuning::FOLLOWUP_ONE_PENALTY;
                        if (next_state.has_adjacent_powerup(simulated_head_pos)) {
                            score += BotTuning::ADJ_POWERUP_BONUS;
                        }

                        if (next_cell == CELL_POWERUP && !out_of_time()) {
                            int delayed_risk = 0;
                            {
                                PhaseBudgetScope phase_scope(PHASE_PATH_AND_SCORING);
                                delayed_risk = delayed_powerup_risk_penalty(state, s.id, a, 3);
                            }
                            score -= delayed_risk;
                        }

                        if (!powerup_positions.empty() && !out_of_time()) {
                            GameState local_plan_state = next_state;
                            local_plan_state.opp_snakes.clear();

// O(1) lookup into pre-computed backward BFS (distance to nearest powerup)
                        int steps_to_gain = 999999;
                        if (s_idx < bk_bfs_by_snake_idx.size()
                            && !bk_bfs_by_snake_idx[s_idx].best_dist_to_power.empty()
                            && simulated_head_pos >= 0 && simulated_head_pos < grid_size) {
                            int16_t d = bk_bfs_by_snake_idx[s_idx].best_dist_to_power[simulated_head_pos];
                            if (d >= 0) steps_to_gain = static_cast<int>(d);
                            }

                            if (steps_to_gain < 999999) {
                                score += 26000 - steps_to_gain * 2500;
                            } else {
                                score -= 12000;
                            }
                        }
                    }
                }
                
                    if (score > best_score) {
                        best_score = score;
                        best_action = a;
                    }
                }
            }
            
            current_my_actions[s_idx] = best_action;

            // Format ID ACTION
            append_action_with_target(s_idx, best_action);
        }
        
        // Join actions with semicolons
        string output = "";
        for (size_t i = 0; i < action_strs.size(); ++i) {
            output += action_strs[i];
            if (i < action_strs.size() - 1) output += ";";
        }
        if (output.empty()) output = "WAIT";

        auto iter_elapsed = duration_cast<microseconds>(high_resolution_clock::now() - iter_start);
        double iter_ms = static_cast<double>(iter_elapsed.count()) / 1000.0;
        double turn_util_pct = (iter_ms * 100.0) / static_cast<double>(BotTuning::TURN_TOTAL_MS_BUDGET);
        
        mylog << "Turn " << counter
              << " elapsed: " << iter_elapsed.count() << "ys"
              << ", output: " << output
              << ", util: " << turn_util_pct << "%"
              << ", phase_ms: "
              << kPhaseNames[PHASE_FORWARD_BFS] << "=" << (g_phase_used_us[PHASE_FORWARD_BFS] / 1000.0) << "/" << g_phase_budget_ms[PHASE_FORWARD_BFS] << ","
              << kPhaseNames[PHASE_BACKWARD_BFS] << "=" << (g_phase_used_us[PHASE_BACKWARD_BFS] / 1000.0) << "/" << g_phase_budget_ms[PHASE_BACKWARD_BFS] << ","
              << kPhaseNames[PHASE_LOCAL_COMBAT] << "=" << (g_phase_used_us[PHASE_LOCAL_COMBAT] / 1000.0) << "/" << g_phase_budget_ms[PHASE_LOCAL_COMBAT] << ","
              << kPhaseNames[PHASE_POWER_PLANNER] << "=" << (g_phase_used_us[PHASE_POWER_PLANNER] / 1000.0) << "/" << g_phase_budget_ms[PHASE_POWER_PLANNER] << ","
              << kPhaseNames[PHASE_PATH_AND_SCORING] << "=" << (g_phase_used_us[PHASE_PATH_AND_SCORING] / 1000.0) << "/" << g_phase_budget_ms[PHASE_PATH_AND_SCORING]
              << ", wrapup_pct=" << BotTuning::TURN_WRAPUP_BUFFER_PCT
              << '\n';
        mylog.flush();
        cout << output << endl;
    }
}