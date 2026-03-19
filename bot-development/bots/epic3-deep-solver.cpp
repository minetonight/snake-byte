#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <queue>
#include <algorithm>
#include <fstream>
#include <unistd.h>
#include <csignal>
#include <exception>
#include <climits>
using namespace std;
using namespace std::chrono;

ofstream make_log_stream() {
    return ofstream("epic3_deep_bot_log_" + to_string(getpid()) + ".txt", ios::app);
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
    static constexpr int TURN_WRAPUP_BUFFER_PCT = 5;
    static constexpr int TURN_PHASE_FORWARD_BFS_PCT = 15;
    static constexpr int TURN_PHASE_BACKWARD_BFS_PCT = 20;
    static constexpr int TURN_PHASE_LOCAL_COMBAT_PCT = 20;
    static constexpr int TURN_PHASE_POWER_PLANNER_PCT = 25;
    static constexpr int TURN_PHASE_PATH_AND_SCORING_PCT = 15;
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
int g_max_depth_achieved = 0; // Iterative-deepening cap reached (attempted depth)
int g_planner_effective_max_ply = 0; // True recursion depth reached (effective ply)
int g_planner_nodes_visited = 0;
int g_planner_current_ply = 0;
int g_planner_id_iters = 0;

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
    g_max_depth_achieved = 0;
    g_planner_effective_max_ply = 0;
    g_planner_nodes_visited = 0;
    g_planner_current_ply = 0;
    g_planner_id_iters = 0;

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

// Generate only the 3 legal (non-backward) actions from current position
inline vector<int> get_legal_actions(const Snake& s) {
    vector<int> legal;
    for (int a = 0; a < 4; ++a) {
        if (!is_backward_action(s, a)) {
            legal.push_back(a);
        }
    }
    return legal;
}

// Check if any opponent can collide with my head position next turn
inline bool is_position_threatened_by_opponent(
    const GameState& state,
    int head_pos,
    int my_snake_id
) {
    int hx = head_pos % max_width;
    int hy = head_pos / max_width;
    
    for (const Snake& opp : state.opp_snakes) {
        if (!opp.is_alive) continue;
        
        int opp_head = opp.body[opp.head_idx];
        int ox = opp_head % max_width;
        int oy = opp_head / max_width;
        
        // Manhattan distance as quick threat check
        if (abs(hx - ox) + abs(hy - oy) <= 1) {
            return true;
        }
    }
    return false;
}

// Score an action by food proximity
inline int score_action_by_heuristic(const GameState& state, int action, int head_pos) {
    // Simple consistent ordering based on action
    return action;
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

struct PlannerPlyScope {
    PlannerPlyScope() {
        g_planner_current_ply++;
        if (g_planner_current_ply > g_planner_effective_max_ply) {
            g_planner_effective_max_ply = g_planner_current_ply;
        }
    }
    ~PlannerPlyScope() {
        g_planner_current_ply--;
        if (g_planner_current_ply < 0) g_planner_current_ply = 0;
    }
};

static int min_steps_to_powerup_gain(const GameState& state, int snake_id, int start_length, int depth_left) {
    g_planner_nodes_visited++;
    PlannerPlyScope ply_scope;

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

    // Get only 3 legal actions (skip backward)
    vector<pair<int, int>> actions_with_score;
    for (int a = 0; a < 4; ++a) {
        if (is_backward_action(*self, a)) continue;

        int nx = hx + dx[a];
        int ny = hy + dy[a];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;

        int n_pos = ny * max_width + nx;
        int16_t next_cell = state.grid[n_pos];
        if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;

        actions_with_score.push_back({a, 0});
    }

    for (auto& [a, _] : actions_with_score) {
        int nx = hx + dx[a];
        int ny = hy + dy[a];
        int n_pos = ny * max_width + nx;

        GameState next_state = state;
        vector<int> sim_my_actions = default_my_actions;
        vector<int> sim_opp_actions = default_opp_actions;
        sim_my_actions[my_idx] = a;
        next_state.simulate(sim_my_actions, sim_opp_actions);

        const Snake* next_self = next_state.find_my_snake_by_id(snake_id);
        if (next_self == nullptr || !next_self->is_alive) continue;
        
        // Threat-based pruning: skip if opponent threatens this position
        int next_head = next_self->body[next_self->head_idx];
        if (is_position_threatened_by_opponent(next_state, next_head, snake_id)) {
            continue;
        }
        
        if (next_self->length > start_length) {
            best = min(best, 1);
            continue;
        }

        int rec = min_steps_to_powerup_gain(next_state, snake_id, start_length, depth_left - 1);
        if (rec < 999999) {
            best = min(best, 1 + rec);
        }
    }

    return best;
}

static int first_action_to_powerup_gain(const GameState& state, int snake_id, int max_depth) {
    g_planner_nodes_visited++;

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
    int best_steps = 999999;
    bool explored_any_legal_action = false;

    // Get only 3 legal actions and sort by heuristic
    vector<pair<int, int>> actions_with_score;
    for (int a = 0; a < 4; ++a) {
        if (is_backward_action(*self, a)) continue;

        int nx = hx + dx[a];
        int ny = hy + dy[a];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;

        int n_pos = ny * max_width + nx;
        int16_t next_cell = state.grid[n_pos];
        if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;

        actions_with_score.push_back({a, 0});
    }

    for (auto& [a, _] : actions_with_score) {
        explored_any_legal_action = true;
        int nx = hx + dx[a];
        int ny = hy + dy[a];
        int n_pos = ny * max_width + nx;

        GameState next_state = state;
        vector<int> sim_my_actions = default_my_actions;
        vector<int> sim_opp_actions = default_opp_actions;
        sim_my_actions[my_idx] = a;
        next_state.simulate(sim_my_actions, sim_opp_actions);

        const Snake* next_self = next_state.find_my_snake_by_id(snake_id);
        if (next_self == nullptr || !next_self->is_alive) continue;
        
        // Threat-based pruning: skip if opponent threatens this position
        int next_head = next_self->body[next_self->head_idx];
        if (is_position_threatened_by_opponent(next_state, next_head, snake_id)) {
            continue;
        }

        int steps = 999999;
        if (next_self->length > self->length) {
            if (g_planner_effective_max_ply < 1) g_planner_effective_max_ply = 1;
            steps = 1;
        } else {
            int prev_ply = g_planner_current_ply;
            g_planner_current_ply = 1;
            if (g_planner_effective_max_ply < 1) g_planner_effective_max_ply = 1;
            steps = min_steps_to_powerup_gain(next_state, snake_id, next_self->length, max_depth - 1);
            g_planner_current_ply = prev_ply;
            if (steps < 999999) {
                steps += 1;
            }
        }

        if (steps < best_steps) {
            best_steps = steps;
            best_action = a;
        }
    }

    if (explored_any_legal_action && g_planner_effective_max_ply < 1) {
        g_planner_effective_max_ply = 1;
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
    int max_depth_achieved = 0;

    while (!out_of_time() && max_depth <= safety_cap_depth) {
        g_planner_id_iters++;
        int candidate = first_action_to_powerup_gain(state, snake_id, max_depth);
        if (candidate != -1) best_action = candidate;
        max_depth_achieved = max_depth;
        max_depth++;
    }

    g_max_depth_achieved = max_depth_achieved;
    return best_action;
}

static int min_steps_to_powerup_gain_budgeted(
    const GameState& state,
    int snake_id,
    int start_length,
    int start_depth
) {
    int best_steps = 999999;
    int depth = max(1, start_depth);
    const int safety_cap_depth = 128;

    while (!out_of_time() && depth <= safety_cap_depth) {
        int candidate = min_steps_to_powerup_gain(state, snake_id, start_length, depth);
        if (candidate < best_steps) best_steps = candidate;
        if (best_steps == 1) break;
        depth++;
    }

    return best_steps;
}

#ifdef LOCAL_TEST
void run_local_tests() {
    cerr << "Running local memory constraint and cloning tests..." << endl;
    
    // Run mock grid allocation test
    world_width = 20;
    world_height = 20;
    total_powerups_count = 10;
    
    max_len = 3 + total_powerups_count;
    max_width = (2 * max_len) + world_width;
    max_height = world_height + 2 * max_len;
    grid_size = max_width * max_height;
    
    GameState root_state;
    root_state.grid.assign(grid_size, CELL_EMPTY);
    
    Snake s1;
    s1.id = 0; s1.is_alive = true; s1.length = 3; s1.head_idx = 0; s1.tail_idx = 2;
    s1.body.assign(max_len, -1);
    s1.body[0] = 5 * max_width + 5;
    s1.body[1] = 5 * max_width + 6;
    s1.body[2] = 5 * max_width + 7;
    root_state.my_snakes.push_back(s1);
    
    auto t1 = high_resolution_clock::now();
    int iterations = 10000;
    for(int i = 0; i < iterations; i++) {
        GameState copy = root_state;
        vector<int> my_acts = {1}; // DOWN
        vector<int> opp_acts = {};
        copy.simulate(my_acts, opp_acts);
    }
    auto t2 = high_resolution_clock::now();
    auto ms_taken = duration_cast<milliseconds>(t2 - t1).count();
    
    cerr << "Simulated " << iterations << " branches in " << ms_taken << "ms" << endl;
    cerr << "Avg time per clone+simulate: " << (float)ms_taken / iterations << "ms" << endl;
    
    if ((float)ms_taken/iterations < 0.1f) {
        cerr << "[PASS] Target < 0.1ms achieved" << endl;
    } else {
        cerr << "[WARN] Target < 0.1ms missed" << endl;
    }
    exit(0);
}
#endif

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
    cin.ignore();
    cin >> world_width;
    cin.ignore();
    cin >> world_height;
    cin.ignore();

    // In the first turn, we don't know total powerups yet, but we read the initial state.
    // Let's store the initial rows to parse them later once we know the dimensions.
    vector<string> initial_rows(world_height);
    for (int i = 0; i < world_height; i++) {
        getline(cin, initial_rows[i]);
    }
    
    int snakebots_per_player;
    cin >> snakebots_per_player;
    cin.ignore();
    
    vector<int> my_snakebots;
    int my_snakebot_id;
    for (int i = 0; i < snakebots_per_player; i++) {
        cin >> my_snakebot_id;
        cin.ignore();
        my_snakebots.push_back(my_snakebot_id);
    }
    int opp_snakebot_id;
    for (int i = 0; i < snakebots_per_player; i++) {
        cin >> opp_snakebot_id;
        cin.ignore();
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
        auto iter_start = high_resolution_clock::now();  // Per-iteration start
        turn_start_time = iter_start;
        begin_turn_timing_budget();
        
        int power_source_count = 0;
        if (!(cin >> power_source_count)) {
            break;
        }
        cin.ignore();

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
            cin.ignore();
            
            int sx = max_len + x;
            int sy = max_len + y;
            state.grid[sy * max_width + sx] = CELL_POWERUP;
        }

        int snakebot_count = 0;
        cin >> snakebot_count;
        cin.ignore();
        
        // Reset dynamic snake states arrays since we overwrite parsed data
        state.my_snakes.clear();
        state.opp_snakes.clear();
        
        for (int i = 0; i < snakebot_count; i++) {
            int snakebot_id;
            string body_str;
            if (!(cin >> snakebot_id >> body_str)) {
                break;
            }
            cin.ignore();

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
        GameState::VoronoiResult v_res;
        {
            PhaseBudgetScope phase_scope(PHASE_FORWARD_BFS);
            v_res = state.calculate_voronoi();
        }

        vector<int> powerup_positions;
        powerup_positions.reserve(power_source_count);
        {
            PhaseBudgetScope phase_scope(PHASE_BACKWARD_BFS);
            for (int pos = 0; pos < grid_size; ++pos) {
                if (out_of_time()) break;
                if (state.grid[pos] == CELL_POWERUP) {
                    powerup_positions.push_back(pos);
                }
            }
        }

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
        
        for (size_t s_idx = 0; s_idx < state.my_snakes.size(); ++s_idx) {
            Snake& s = state.my_snakes[s_idx];
            if (!s.is_alive) continue;
            
            int head_pos = s.body[s.head_idx];
            int hx = head_pos % max_width;
            int hy = head_pos / max_width;
            int fallback_action = infer_previous_action(s);
            int best_action = fallback_action;
            int best_score = -999999;

            if (out_of_time()) {
                current_my_actions[s_idx] = fallback_action;
                action_strs.push_back(to_string(s.id) + " " + action_to_string(fallback_action));
                continue;
            }

            {
                GameState local_plan_state = state;
                local_plan_state.opp_snakes.clear();
                int tactical_action = -1;
                {
                    PhaseBudgetScope phase_scope(PHASE_POWER_PLANNER);
                    const int uniform_start_depth = 20;
                    tactical_action = first_action_to_powerup_gain_budgeted(local_plan_state, s.id, uniform_start_depth);
                    if (tactical_action == -1) {
                        tactical_action = first_action_to_powerup_gain(local_plan_state, s.id, 3);
                    }
                }

                if (tactical_action != -1) {
                    GameState check_state = state;
                    vector<int> sim_my_actions = default_my_actions;
                    vector<int> sim_opp_actions = default_opp_actions;
                    sim_my_actions[s_idx] = tactical_action;
                    check_state.simulate(sim_my_actions, sim_opp_actions);
                    const Snake* checked = check_state.find_my_snake_by_id(s.id);
                    if (checked != nullptr && checked->is_alive && check_state.count_safe_followups(*checked) > 0) {
                        current_my_actions[s_idx] = tactical_action;
                        action_strs.push_back(to_string(s.id) + " " + action_to_string(tactical_action));
                        continue;
                    }
                }

                int legal_fallback = -1;
                for (int a = 0; a < 4; ++a) {
                    if (is_backward_action(s, a)) continue;
                    int nx = hx + dx[a];
                    int ny = hy + dy[a];
                    if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
                    int n_pos = ny * max_width + nx;
                    int16_t next_cell = state.grid[n_pos];
                    if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;
                    legal_fallback = a;
                    break;
                }
                current_my_actions[s_idx] = (legal_fallback != -1) ? legal_fallback : fallback_action;
                action_strs.push_back(to_string(s.id) + " " + action_to_string(current_my_actions[s_idx]));
                continue;
            }

            if (powerup_positions.size() == 1) {
                int apple_pos = powerup_positions[0];
                int ax = apple_pos % max_width;
                int ay = apple_pos / max_width;
                int map_mid_x = max_len + (world_width / 2);
                if (map_has_open_floor_edge && map_has_open_left_edge && ax <= max_len + 1 && hx <= map_mid_x && ay < hy) {
                    int up_pos = (hy - 1) * max_width + hx;
                    int16_t up_cell = state.grid[up_pos];
                    if (!is_backward_action(s, 0) && up_cell != CELL_WALL && up_cell < CELL_SNAKE_BASE) {
                        current_my_actions[s_idx] = 0;
                        action_strs.push_back(to_string(s.id) + " " + action_to_string(0));
                        continue;
                    }
                    if (hx > ax) {
                        int left_pos = hy * max_width + (hx - 1);
                        int16_t left_cell = state.grid[left_pos];
                        if (!is_backward_action(s, 2) && left_cell != CELL_WALL && left_cell < CELL_SNAKE_BASE) {
                            current_my_actions[s_idx] = 2;
                            action_strs.push_back(to_string(s.id) + " " + action_to_string(2));
                            continue;
                        }
                    }
                }
                if (ax == hx && ay < hy) {
                    current_my_actions[s_idx] = 0;
                    action_strs.push_back(to_string(s.id) + " " + action_to_string(0));
                    continue;
                }
                if (ay == hy - 1 && abs(ax - hx) == 1) {
                    int tactical_action = (ax < hx) ? 2 : 3;
                    current_my_actions[s_idx] = tactical_action;
                    action_strs.push_back(to_string(s.id) + " " + action_to_string(tactical_action));
                    continue;
                }
                if (map_has_open_left_edge && !map_has_open_floor_edge && ax <= max_len + 1 && hx <= map_mid_x) {
                    if (map_has_open_floor_edge && hy >= max_len + world_height - 3) {
                        int nx = hx;
                        int ny = hy - 1;
                        int n_pos = ny * max_width + nx;
                        int16_t next_cell = state.grid[n_pos];
                        if (!is_backward_action(s, 0) && next_cell != CELL_WALL && next_cell < CELL_SNAKE_BASE) {
                            current_my_actions[s_idx] = 0;
                            action_strs.push_back(to_string(s.id) + " " + action_to_string(0));
                            continue;
                        }
                    }
                    if (hx > ax) {
                        int nx = hx - 1;
                        int ny = hy;
                        int n_pos = ny * max_width + nx;
                        int16_t next_cell = state.grid[n_pos];
                        if (!is_backward_action(s, 2) && next_cell != CELL_WALL && next_cell < CELL_SNAKE_BASE) {
                            current_my_actions[s_idx] = 2;
                            action_strs.push_back(to_string(s.id) + " " + action_to_string(2));
                            continue;
                        }
                    }
                    if (hx == ax && hy > ay) {
                        int nx = hx;
                        int ny = hy - 1;
                        int n_pos = ny * max_width + nx;
                        int16_t next_cell = state.grid[n_pos];
                        if (!is_backward_action(s, 0) && next_cell != CELL_WALL && next_cell < CELL_SNAKE_BASE) {
                            current_my_actions[s_idx] = 0;
                            action_strs.push_back(to_string(s.id) + " " + action_to_string(0));
                            continue;
                        }
                    }
                }
            }

            if (!powerup_positions.empty() && !out_of_time()) {
                GameState local_plan_state = state;
                local_plan_state.opp_snakes.clear();
                int tactical_action = -1;
                {
                    PhaseBudgetScope phase_scope(PHASE_POWER_PLANNER);
                    bool open_edge_map = map_has_open_left_edge || map_has_open_right_edge || map_has_open_floor_edge;
                    int start_depth = 8;
                    if (open_edge_map && powerup_positions.size() == 1) {
                        start_depth = map_has_open_floor_edge ? 16 : 12;
                    }
                    tactical_action = first_action_to_powerup_gain_budgeted(local_plan_state, s.id, start_depth);
                }
                if (tactical_action != -1) {
                    current_my_actions[s_idx] = tactical_action;
                    action_strs.push_back(to_string(s.id) + " " + action_to_string(tactical_action));
                    continue;
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

                // Survive constraints first.
                if (!state.survives_flood_fill(n_pos, max(2, s.length / 2))) {
                    score -= 20000;
                } else {
                    score += 200;
                }

                bool defensive_mode = (s.length >= 4 && v_res.length_delta >= 0);

                // Prefer immediate powerup captures unless we're already ahead and switching defensive.
                if (next_cell == CELL_POWERUP) {
                    bool enclosed_map = !map_has_open_left_edge && !map_has_open_right_edge && !map_has_open_floor_edge;
                    if (enclosed_map && total_powerups_count == 2 && s.length >= 4) {
                        score -= 100000;
                    } else {
                        score += 100000;
                    }
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
                score += v_res.length_delta * 50;
                score += v_res.my_exclusive_powerups * 20;
                score -= v_res.opp_exclusive_powerups * 20;

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
                        score -= 50000;
                    } else {
                        int simulated_head_pos = next_self->body[next_self->head_idx];
                        int shx = simulated_head_pos % max_width;
                        int shy = simulated_head_pos / max_width;
                        if (shx < max_len || shx >= max_len + world_width) {
                            score -= 35000;
                        }
                        if (shy >= max_len + world_height) {
                            score -= 50000;
                        }
                        if (map_has_open_left_edge && hx == max_len && a == 2) {
                            score -= 25000;
                        }
                        if (map_has_open_right_edge && hx == max_len + world_width - 1 && a == 3) {
                            score -= 25000;
                        }

                        int followups = next_state.count_safe_followups(*next_self);
                        score += followups * 3000;
                        if (followups == 0) score -= 20000;
                        if (followups == 1) score -= 4000;
                        if (next_state.has_adjacent_powerup(simulated_head_pos)) {
                            score += 7000;
                        }

                        if (!powerup_positions.empty() && !out_of_time()) {
                            GameState local_plan_state = next_state;
                            local_plan_state.opp_snakes.clear();
                            int steps_to_gain = 999999;
                            {
                                PhaseBudgetScope phase_scope(PHASE_POWER_PLANNER);
                                steps_to_gain = min_steps_to_powerup_gain_budgeted(local_plan_state, s.id, next_self->length, 4);
                            }
                            if (steps_to_gain < 999999) {
                                score += 500000 - steps_to_gain * 50000;
                            } else {
                                score -= 50000;
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
            action_strs.push_back(to_string(s.id) + " " + action_to_string(best_action));
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
              << ", max_depth_o3: " << g_max_depth_achieved
              << ", planner_effective_ply: " << g_planner_effective_max_ply
              << ", planner_nodes: " << g_planner_nodes_visited
              << ", planner_id_iters: " << g_planner_id_iters
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