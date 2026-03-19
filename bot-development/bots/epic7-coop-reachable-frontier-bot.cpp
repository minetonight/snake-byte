#include <algorithm>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdint>
#include <deque>
#include <exception>
#include <fstream>
#include <iostream>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h>
#include <vector>

using namespace std;
using namespace std::chrono;

// Evolution of:
// - bot-development/bots/epic4-solver-BFS-bot.cpp
// - bot-development/bots/epic4-deep-search-only-bot.cpp
//
// This file is a clean single-mode successor focused on one consistent idea:
// bounded reachable-frontier scanning over real simulated states.
//
// Differences from the older Epic 4 file family:
// - no mode macros
// - no hybrid/BFS/heuristic compile-time branches
// - no optimistic head-only reachability for apple selection
// - center-oriented long-term fallback goal when no reachable apple is found

static constexpr const char* BOT_LOG_PREFIX = "epic7_coop_reachable_frontier_bot_log_";
static constexpr int TURN_BUDGET_MS = 72;
static constexpr int SCAN_EXPANSION_LIMIT = 6000;
static constexpr int SCAN_DEPTH_SMALL = 18;
static constexpr int SCAN_DEPTH_LARGE = 26;
static constexpr int DIRECT_APPLE_MAX_DIST = 6;
static constexpr int LONG_TERM_GOAL_TTL = 20;
static constexpr int APPLE_GOAL_TTL = 28;
static constexpr int DEEP_SCAN_BUDGET_PCT = 70;
static constexpr int FOLLOWTHROUGH_BUDGET_PCT = 20;
static constexpr int TURN_RESERVE_BUDGET_PCT = 10;

ofstream make_log_stream() {
    return ofstream(string(BOT_LOG_PREFIX) + to_string(getpid()) + ".txt", ios::app);
}

ofstream mylog = make_log_stream();
auto turn_start_time = high_resolution_clock::now();

enum class SearchPhase {
    None,
    DeepScan,
    FollowThrough,
};

struct PhaseTimingBudget {
    int deep_scan_budget_ms = TURN_BUDGET_MS;
    int followthrough_budget_ms = TURN_BUDGET_MS;
    int deep_scan_used_ms = 0;
    int followthrough_used_ms = 0;
};

PhaseTimingBudget* g_active_phase_budget = nullptr;
SearchPhase g_active_phase = SearchPhase::None;
high_resolution_clock::time_point g_active_phase_started = high_resolution_clock::now();

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

static int elapsed_turn_ms() {
    return static_cast<int>(duration_cast<milliseconds>(high_resolution_clock::now() - turn_start_time).count());
}

static void charge_active_phase_time() {
    if (g_active_phase_budget == nullptr || g_active_phase == SearchPhase::None) return;
    auto now = high_resolution_clock::now();
    int elapsed_ms = static_cast<int>(duration_cast<milliseconds>(now - g_active_phase_started).count());
    if (elapsed_ms <= 0) return;
    if (g_active_phase == SearchPhase::DeepScan) {
        g_active_phase_budget->deep_scan_used_ms += elapsed_ms;
    } else if (g_active_phase == SearchPhase::FollowThrough) {
        g_active_phase_budget->followthrough_used_ms += elapsed_ms;
    }
    g_active_phase_started = now;
}

static int phase_used_ms(const PhaseTimingBudget& budget, SearchPhase phase) {
    int used = 0;
    if (phase == SearchPhase::DeepScan) used = budget.deep_scan_used_ms;
    else if (phase == SearchPhase::FollowThrough) used = budget.followthrough_used_ms;

    if (g_active_phase_budget == &budget && g_active_phase == phase) {
        used += static_cast<int>(duration_cast<milliseconds>(high_resolution_clock::now() - g_active_phase_started).count());
    }
    return used;
}

static bool phase_budget_exhausted(const PhaseTimingBudget& budget, SearchPhase phase) {
    if (phase == SearchPhase::DeepScan) {
        return phase_used_ms(budget, phase) >= budget.deep_scan_budget_ms;
    }
    if (phase == SearchPhase::FollowThrough) {
        return phase_used_ms(budget, phase) >= budget.followthrough_budget_ms;
    }
    return false;
}

struct ScopedSearchPhase {
    PhaseTimingBudget* previous_budget = nullptr;
    SearchPhase previous_phase = SearchPhase::None;
    high_resolution_clock::time_point previous_started = high_resolution_clock::now();

    ScopedSearchPhase(PhaseTimingBudget* budget, SearchPhase phase) {
        charge_active_phase_time();
        previous_budget = g_active_phase_budget;
        previous_phase = g_active_phase;
        previous_started = g_active_phase_started;
        g_active_phase_budget = budget;
        g_active_phase = phase;
        g_active_phase_started = high_resolution_clock::now();
    }

    ~ScopedSearchPhase() {
        charge_active_phase_time();
        g_active_phase_budget = previous_budget;
        g_active_phase = previous_phase;
        g_active_phase_started = high_resolution_clock::now();
    }
};

static bool out_of_time() {
    if (elapsed_turn_ms() >= TURN_BUDGET_MS) return true;
    if (g_active_phase_budget != nullptr && g_active_phase != SearchPhase::None) {
        return phase_budget_exhausted(*g_active_phase_budget, g_active_phase);
    }
    return false;
}

constexpr int16_t CELL_WALL = -1;
constexpr int16_t CELL_EMPTY = 0;
constexpr int16_t CELL_POWERUP = 3;
constexpr int16_t CELL_SNAKE_BASE = 10;

int world_width = 0;
int world_height = 0;
int total_powerups_count = 0;
int max_len = 0;
int max_width = 0;
int max_height = 0;
int grid_size = 0;
int g_turn_counter = 0;

struct PersistentGoal {
    int target_pos = -1;
    int expires_turn = 0;
};

struct ScanBudgetConfig {
    int depth_limit = SCAN_DEPTH_SMALL;
    int expansion_limit = SCAN_EXPANSION_LIMIT;
    int scan_budget_ms = TURN_BUDGET_MS;
    int followthrough_budget_ms = TURN_BUDGET_MS;
    const char* tier = "medium";
};

struct DirectAppleHint {
    bool found = false;
    int action = -1;
    int target_pos = -1;
    int distance = INT_MAX;
};

unordered_map<int, PersistentGoal> g_center_goal_by_snake;
unordered_map<int, PersistentGoal> g_apple_goal_by_snake;

struct Snake {
    int id = -1;
    int length = 0;
    int head_idx = 0;
    int tail_idx = 0;
    bool is_alive = false;
    vector<int> body;
};

inline int ring_size(const Snake& s) {
    return static_cast<int>(s.body.size());
}

inline int infer_previous_action(const Snake& s) {
    if (s.length > 1) {
        int h_pos = s.body[s.head_idx];
        int n_pos = s.body[(s.head_idx + 1) % ring_size(s)];
        if (h_pos == n_pos - max_width) return 0;
        if (h_pos == n_pos + max_width) return 1;
        if (h_pos == n_pos - 1) return 2;
        if (h_pos == n_pos + 1) return 3;
    }
    return 0;
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

static inline uint64_t hash_combine_u64(uint64_t seed, uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

struct GameState {
    vector<int16_t> grid;
    vector<Snake> my_snakes;
    vector<Snake> opp_snakes;

    inline bool is_cell_grounded(int pos, const vector<bool>& grounded_snakes) const {
        int below_idx = pos + max_width;
        if (below_idx >= grid_size) return true;
        int16_t below_cell = grid[below_idx];
        if (below_cell == CELL_WALL || below_cell == CELL_POWERUP) return true;
        if (below_cell >= CELL_SNAKE_BASE) {
            int snake_id = below_cell - CELL_SNAKE_BASE;
            if (snake_id >= 0 && snake_id < static_cast<int>(grounded_snakes.size()) && grounded_snakes[snake_id]) return true;
        }
        return false;
    }

    inline void apply_gravity() {
        bool something_fell = true;
        int infinite_loop_guard = 0;
        while (something_fell) {
            if (++infinite_loop_guard > 50) break;
            something_fell = false;

            vector<Snake*> airborne;
            for (auto& s : my_snakes) if (s.is_alive) airborne.push_back(&s);
            for (auto& s : opp_snakes) if (s.is_alive) airborne.push_back(&s);

            vector<bool> grounded(10000, false);
            bool something_got_grounded = true;
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
                        if (s->id >= 0 && s->id < static_cast<int>(grounded.size())) grounded[s->id] = true;
                        something_got_grounded = true;
                        it = airborne.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            for (Snake* s : airborne) {
                for (int i = 0; i < s->length; ++i) {
                    int pos = s->body[(s->head_idx + i) % ring_size(*s)];
                    if (pos >= 0 && pos < grid_size) grid[pos] = CELL_EMPTY;
                }
            }

            for (Snake* s : airborne) {
                something_fell = true;
                for (int i = 0; i < s->length; ++i) {
                    int idx = (s->head_idx + i) % ring_size(*s);
                    s->body[idx] += max_width;
                }
                bool all_out = true;
                for (int i = 0; i < s->length; ++i) {
                    int pos = s->body[(s->head_idx + i) % ring_size(*s)];
                    int y = pos / max_width;
                    if (y < max_len + world_height + 1) {
                        all_out = false;
                        break;
                    }
                }
                if (all_out) s->is_alive = false;
            }

            for (Snake* s : airborne) {
                if (!s->is_alive) continue;
                for (int i = 0; i < s->length; ++i) {
                    int pos = s->body[(s->head_idx + i) % ring_size(*s)];
                    if (i == 0) continue;
                    if (pos >= 0 && pos < grid_size) grid[pos] = CELL_SNAKE_BASE + s->id;
                }
            }
        }
    }

    inline void apply_movement(const vector<int>& my_actions, const vector<int>& opp_actions) {
        auto move_snakes = [&](vector<Snake>& snakes, const vector<int>& actions) {
            for (size_t i = 0; i < snakes.size(); ++i) {
                Snake& s = snakes[i];
                if (!s.is_alive) continue;
                int action = actions[i];
                if (action == 4) action = infer_previous_action(s);

                int h_pos = s.body[s.head_idx];
                int hx = h_pos % max_width;
                int hy = h_pos / max_width;
                int n_hx = hx;
                int n_hy = hy;
                if (action == 0) n_hy -= 1;
                else if (action == 1) n_hy += 1;
                else if (action == 2) n_hx -= 1;
                else if (action == 3) n_hx += 1;

                int n_pos = n_hy * max_width + n_hx;
                bool will_eat_powerup = n_pos >= 0 && n_pos < grid_size && grid[n_pos] == CELL_POWERUP;
                int tail_pos = s.body[s.tail_idx];
                int new_head_idx = (s.head_idx - 1 + ring_size(s)) % ring_size(s);
                s.body[new_head_idx] = n_pos;
                if (!will_eat_powerup) {
                    s.tail_idx = (s.tail_idx - 1 + ring_size(s)) % ring_size(s);
                } else {
                    s.length++;
                }
                s.head_idx = new_head_idx;
                if (!will_eat_powerup && tail_pos >= 0 && tail_pos < grid_size) grid[tail_pos] = CELL_EMPTY;
            }
        };

        move_snakes(my_snakes, my_actions);
        move_snakes(opp_snakes, opp_actions);
    }

    inline void apply_eats() {
        auto consume_snake_heads = [&](const vector<Snake>& snakes) {
            for (const Snake& s : snakes) {
                if (!s.is_alive) continue;
                int h_pos = s.body[s.head_idx];
                if (h_pos >= 0 && h_pos < grid_size && grid[h_pos] == CELL_POWERUP) {
                    grid[h_pos] = CELL_EMPTY;
                }
            }
        };

        consume_snake_heads(my_snakes);
        consume_snake_heads(opp_snakes);
    }

    inline void resolve_collisions() {
        vector<Snake*> all_snakes;
        for (auto& s : my_snakes) if (s.is_alive) all_snakes.push_back(&s);
        for (auto& s : opp_snakes) if (s.is_alive) all_snakes.push_back(&s);

        vector<int> head_positions(all_snakes.size());
        vector<bool> to_destroy(all_snakes.size(), false);
        for (size_t i = 0; i < all_snakes.size(); ++i) {
            head_positions[i] = all_snakes[i]->body[all_snakes[i]->head_idx];
        }

        for (size_t i = 0; i < all_snakes.size(); ++i) {
            int h_pos = head_positions[i];
            if (h_pos < 0 || h_pos >= grid_size) {
                to_destroy[i] = true;
                continue;
            }
            int cell_val = grid[h_pos];
            if (cell_val == CELL_WALL || cell_val >= CELL_SNAKE_BASE) to_destroy[i] = true;

            for (size_t j = i + 1; j < all_snakes.size(); ++j) {
                if (head_positions[i] == head_positions[j]) {
                    to_destroy[i] = true;
                    to_destroy[j] = true;
                }
            }
        }

        for (size_t i = 0; i < all_snakes.size(); ++i) {
            Snake* s = all_snakes[i];
            if (to_destroy[i]) {
                s->length--;
                s->head_idx = (s->head_idx + 1) % ring_size(*s);
                if (s->length < 3) {
                    s->is_alive = false;
                    for (int k = 0; k < max(0, s->length); ++k) {
                        int b_idx = (s->head_idx + k) % ring_size(*s);
                        int pos = s->body[b_idx];
                        if (pos >= 0 && pos < grid_size) grid[pos] = CELL_EMPTY;
                    }
                }
            }
        }

        for (Snake* s : all_snakes) {
            if (!s->is_alive) continue;
            for (int k = 0; k < s->length; ++k) {
                int b_idx = (s->head_idx + k) % ring_size(*s);
                int pos = s->body[b_idx];
                if (pos >= 0 && pos < grid_size) grid[pos] = CELL_SNAKE_BASE + s->id;
            }
        }
    }

    inline void simulate(const vector<int>& my_actions, const vector<int>& opp_actions) {
        apply_movement(my_actions, opp_actions);
        apply_eats();
        resolve_collisions();
        apply_gravity();
    }

    const Snake* find_my_snake_by_id(int snake_id) const {
        for (const auto& s : my_snakes) if (s.id == snake_id) return &s;
        return nullptr;
    }

    bool survives_flood_fill(int start_pos, int required_space) const {
        if (required_space <= 1) return true;
        vector<bool> visited(grid_size, false);
        queue<int> q;
        q.push(start_pos);
        visited[start_pos] = true;
        int space_found = 1;
        int dx[] = {0, 0, -1, 1};
        int dy[] = {-1, 1, 0, 0};
        while (!q.empty()) {
            if (out_of_time()) return true;
            int pos = q.front();
            q.pop();
            int cx = pos % max_width;
            int cy = pos / max_width;
            for (int i = 0; i < 4; ++i) {
                int nx = cx + dx[i];
                int ny = cy + dy[i];
                if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
                int n_pos = ny * max_width + nx;
                int16_t cell = grid[n_pos];
                if (cell != CELL_EMPTY && cell != CELL_POWERUP) continue;
                if (!visited[n_pos]) {
                    visited[n_pos] = true;
                    space_found++;
                    if (space_found >= required_space) return true;
                    q.push(n_pos);
                }
            }
        }
        return space_found >= required_space;
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
            if (survives_flood_fill(n_pos, max(2, s.length / 2))) safe_count++;
        }
        return safe_count;
    }
};

static vector<int> infer_default_actions(const vector<Snake>& snakes) {
    vector<int> actions(snakes.size(), 0);
    for (size_t i = 0; i < snakes.size(); ++i) actions[i] = infer_previous_action(snakes[i]);
    return actions;
}

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

static bool is_goal_cell_valid(const GameState& state, int pos) {
    if (!is_playable_cell(pos)) return false;
    int16_t cell = state.grid[pos];
    return cell != CELL_WALL;
}

static bool is_center_goal_cell_valid(const GameState& state, int pos) {
    if (!is_playable_cell(pos)) return false;
    int16_t cell = state.grid[pos];
    return cell == CELL_EMPTY || cell == CELL_POWERUP;
}

static int first_legal_action_basic(const GameState& state, const Snake& s) {
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
        int16_t cell = state.grid[n_pos];
        if (cell == CELL_WALL || cell >= CELL_SNAKE_BASE) continue;
        return a;
    }
    return infer_previous_action(s);
}

static bool is_action_locally_legal(const GameState& state, const Snake& s, int action) {
    if (action < 0 || action > 3) return false;
    if (is_backward_action(s, action)) return false;

    int head_pos = s.body[s.head_idx];
    int hx = head_pos % max_width;
    int hy = head_pos / max_width;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    int nx = hx + dx[action];
    int ny = hy + dy[action];
    if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) return false;

    int n_pos = ny * max_width + nx;
    int16_t cell = state.grid[n_pos];
    return cell != CELL_WALL && cell < CELL_SNAKE_BASE;
}

static const Snake* find_any_snake_by_id(const GameState& state, int snake_id) {
    const Snake* mine = state.find_my_snake_by_id(snake_id);
    if (mine != nullptr) return mine;
    for (const Snake& s : state.opp_snakes) {
        if (s.id == snake_id) return &s;
    }
    return nullptr;
}

static bool assign_action_for_snake(const GameState& state, int snake_id, int action, vector<int>& my_actions, vector<int>& opp_actions) {
    for (size_t i = 0; i < state.my_snakes.size(); ++i) {
        if (state.my_snakes[i].id == snake_id) {
            my_actions[i] = action;
            return true;
        }
    }
    for (size_t i = 0; i < state.opp_snakes.size(); ++i) {
        if (state.opp_snakes[i].id == snake_id) {
            opp_actions[i] = action;
            return true;
        }
    }
    return false;
}

static int shortest_path_distance_walls_only(const GameState& state, int start_pos, int target_pos) {
    if (start_pos < 0 || start_pos >= grid_size || target_pos < 0 || target_pos >= grid_size) return INT_MAX;
    if (start_pos == target_pos) return 0;

    vector<int> dist(grid_size, -1);
    queue<int> q;
    q.push(start_pos);
    dist[start_pos] = 0;

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    while (!q.empty() && !out_of_time()) {
        int pos = q.front();
        q.pop();
        int cx = pos % max_width;
        int cy = pos / max_width;
        for (int i = 0; i < 4; ++i) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
            int n_pos = ny * max_width + nx;
            if (!is_playable_cell(n_pos)) continue;
            if (dist[n_pos] != -1) continue;
            if (state.grid[n_pos] == CELL_WALL) continue;
            dist[n_pos] = dist[pos] + 1;
            if (n_pos == target_pos) return dist[n_pos];
            q.push(n_pos);
        }
    }

    return INT_MAX;
}

static int first_action_toward_cell_walls_only(const GameState& state, const Snake& s, int target_pos) {
    if (target_pos < 0 || target_pos >= grid_size) return -1;
    int head_pos = s.body[s.head_idx];
    if (head_pos == target_pos) return infer_previous_action(s);

    vector<int> dist(grid_size, -1);
    vector<int> first_action(grid_size, -1);
    queue<int> q;
    q.push(head_pos);
    dist[head_pos] = 0;

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    while (!q.empty() && !out_of_time()) {
        int pos = q.front();
        q.pop();
        int cx = pos % max_width;
        int cy = pos / max_width;
        for (int a = 0; a < 4; ++a) {
            if (pos == head_pos && is_backward_action(s, a)) continue;
            int nx = cx + dx[a];
            int ny = cy + dy[a];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
            int n_pos = ny * max_width + nx;
            if (!is_playable_cell(n_pos)) continue;
            if (dist[n_pos] != -1) continue;
            if (state.grid[n_pos] == CELL_WALL) continue;
            int root_action = (pos == head_pos) ? a : first_action[pos];
            dist[n_pos] = dist[pos] + 1;
            first_action[n_pos] = root_action;
            if (n_pos == target_pos) return root_action;
            q.push(n_pos);
        }
    }

    return -1;
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

static bool simulated_step_makes_progress_to_target(const GameState& state, const Snake& s, int action, int target_pos) {
    if (action < 0 || target_pos < 0 || target_pos >= grid_size) return false;

    int start_head = s.body[s.head_idx];
    int start_dist = shortest_path_distance_walls_only(state, start_head, target_pos);
    if (start_dist == INT_MAX) return false;

    GameState isolated = isolate_state_for_single_snake_planning(state, s.id);
    if (isolated.my_snakes.empty()) return false;

    vector<int> my_actions = infer_default_actions(isolated.my_snakes);
    vector<int> opp_actions;
    my_actions[0] = action;
    isolated.simulate(my_actions, opp_actions);

    const Snake* next_self = isolated.find_my_snake_by_id(s.id);
    if (next_self == nullptr || !next_self->is_alive || next_self->length <= 0) return false;

    int next_head = next_self->body[next_self->head_idx];
    if (!is_playable_cell(next_head)) return false;
    int next_dist = shortest_path_distance_walls_only(isolated, next_head, target_pos);
    if (next_dist == INT_MAX) return false;
    return next_dist < start_dist || next_self->length > s.length;
}

static int count_simulated_safe_followups(const GameState& state, const Snake& s) {
    if (!s.is_alive || s.length <= 0) return 0;

    int safe_count = 0;
    for (int action = 0; action < 4; ++action) {
        if (is_backward_action(s, action)) continue;

        int head_pos = s.body[s.head_idx];
        int hx = head_pos % max_width;
        int hy = head_pos / max_width;
        int dx[] = {0, 0, -1, 1};
        int dy[] = {-1, 1, 0, 0};
        int nx = hx + dx[action];
        int ny = hy + dy[action];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;

        int n_pos = ny * max_width + nx;
        int16_t cell = state.grid[n_pos];
        if (cell == CELL_WALL || cell >= CELL_SNAKE_BASE) continue;

        GameState probe = state;
        vector<int> my_actions = infer_default_actions(probe.my_snakes);
        vector<int> opp_actions = infer_default_actions(probe.opp_snakes);
        if (!assign_action_for_snake(probe, s.id, action, my_actions, opp_actions)) continue;
        probe.simulate(my_actions, opp_actions);

        const Snake* next_self = find_any_snake_by_id(probe, s.id);
        if (next_self == nullptr || !next_self->is_alive || next_self->length < s.length) continue;
        safe_count++;
    }

    return safe_count;
}

static int count_simulated_safe_followthrough_moves(const GameState& state, const Snake& s) {
    if (!s.is_alive || s.length <= 0) return 0;

    int safe_count = 0;
    for (int action = 0; action < 4; ++action) {
        if (is_backward_action(s, action)) continue;

        int head_pos = s.body[s.head_idx];
        int hx = head_pos % max_width;
        int hy = head_pos / max_width;
        int dx[] = {0, 0, -1, 1};
        int dy[] = {-1, 1, 0, 0};
        int nx = hx + dx[action];
        int ny = hy + dy[action];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;

        int n_pos = ny * max_width + nx;
        int16_t cell = state.grid[n_pos];
        if (cell == CELL_WALL || cell >= CELL_SNAKE_BASE) continue;

        GameState probe = state;
        vector<int> my_actions = infer_default_actions(probe.my_snakes);
        vector<int> opp_actions = infer_default_actions(probe.opp_snakes);
        if (!assign_action_for_snake(probe, s.id, action, my_actions, opp_actions)) continue;
        probe.simulate(my_actions, opp_actions);

        const Snake* next_self = find_any_snake_by_id(probe, s.id);
        if (next_self == nullptr || !next_self->is_alive || next_self->length < s.length) continue;
        if (count_simulated_safe_followups(probe, *next_self) <= 0) continue;
        safe_count++;
    }

    return safe_count;
}

static bool is_action_immediately_unsafe_against_stronger_snake(const GameState& state, const Snake& self, int action) {
    if (!is_action_locally_legal(state, self, action)) return true;

    int self_head = self.body[self.head_idx];
    auto punished_by = [&](const Snake& other) {
        if (!other.is_alive || other.length <= 0 || other.id == self.id) return false;
        if (other.length < self.length) return false;

        int other_head = other.body[other.head_idx];
        if (manhattan_dist_pos(self_head, other_head) > 4) return false;

        for (int other_action = 0; other_action < 4; ++other_action) {
            if (!is_action_locally_legal(state, other, other_action)) continue;

            GameState probe = state;
            vector<int> my_actions = infer_default_actions(probe.my_snakes);
            vector<int> opp_actions = infer_default_actions(probe.opp_snakes);
            if (!assign_action_for_snake(probe, self.id, action, my_actions, opp_actions)) return true;
            if (!assign_action_for_snake(probe, other.id, other_action, my_actions, opp_actions)) continue;

            probe.simulate(my_actions, opp_actions);

            const Snake* self_after = find_any_snake_by_id(probe, self.id);
            if (self_after == nullptr || !self_after->is_alive || self_after->length < self.length) {
                return true;
            }
        }

        return false;
    };

    for (const Snake& other : state.my_snakes) {
        if (punished_by(other)) return true;
    }
    for (const Snake& other : state.opp_snakes) {
        if (punished_by(other)) return true;
    }
    return false;
}

static int max_reachable_survival_depth(const GameState& state, int snake_id, int depth_limit, int expansion_limit);
static int count_powerups_on_grid(const GameState& state);

struct GrowthRunwayEvaluation {
    int continuation_depth = 0;
    int continuation_expansion = 0;
    int remaining_powerups = 0;
    int survival_runway = 0;
    int minimum_runway = 0;
    bool sufficient = false;
};

static int world_x_from_pos(int pos) {
    return (pos % max_width) - max_len;
}

static int world_y_from_pos(int pos) {
    return (pos / max_width) - max_len;
}

static bool is_debug_target_apple_pos(int pos) {
    return world_x_from_pos(pos) == 0 && world_y_from_pos(pos) == 2;
}

static string format_snake_body_world(const Snake& s) {
    ostringstream oss;
    for (int i = 0; i < s.length; ++i) {
        if (i > 0) oss << "->";
        int pos = s.body[(s.head_idx + i) % ring_size(s)];
        oss << "(" << world_x_from_pos(pos) << "," << world_y_from_pos(pos) << ")";
    }
    return oss.str();
}

static GrowthRunwayEvaluation evaluate_growth_runway(const GameState& state, int snake_id, const Snake& grown_self, bool strict_checks) {
    GrowthRunwayEvaluation eval;
    eval.continuation_depth = strict_checks ? 12 : 8;
    eval.continuation_expansion = strict_checks ? 2200 : 900;
    eval.remaining_powerups = count_powerups_on_grid(state);
    eval.survival_runway = max_reachable_survival_depth(state, snake_id, eval.continuation_depth, eval.continuation_expansion);
    eval.minimum_runway = min(eval.continuation_depth, max(4, min(8, grown_self.length + (eval.remaining_powerups > 0 ? 1 : 0))));
    eval.sufficient = eval.survival_runway >= eval.minimum_runway;
    return eval;
}

static void log_debug_growth_rejection(const char* site, int snake_id, const Snake& grown_self, const GrowthRunwayEvaluation& eval,
                                       int simulated_followups, int simulated_followthroughs, int local_followups) {
    int head_pos = grown_self.body[grown_self.head_idx];
    if (!is_debug_target_apple_pos(head_pos)) return;

    mylog << "DEBUG_REJECT_GROWTH"
          << " site=" << site
          << " turn=" << g_turn_counter
          << " snake=" << snake_id
          << " head_x=" << world_x_from_pos(head_pos)
          << " head_y=" << world_y_from_pos(head_pos)
          << " length=" << grown_self.length
          << " body=" << format_snake_body_world(grown_self)
          << " local_followups=" << local_followups
          << " simulated_followups=" << simulated_followups
          << " simulated_followthroughs=" << simulated_followthroughs
          << " continuation_depth=" << eval.continuation_depth
          << " continuation_expansion=" << eval.continuation_expansion
          << " remaining_powerups=" << eval.remaining_powerups
          << " survival_runway=" << eval.survival_runway
          << " minimum_runway=" << eval.minimum_runway
          << " sufficient=" << (eval.sufficient ? 1 : 0)
          << '\n';
}

static bool growth_state_has_sufficient_runway(const GameState& state, int snake_id, const Snake& grown_self, bool strict_checks) {
    return evaluate_growth_runway(state, snake_id, grown_self, strict_checks).sufficient;
}

static int choose_safe_action_for_target(const GameState& state, const Snake& s, int target_pos) {
    int best_action = -1;
    bool best_viable = false;
    bool best_progress = false;
    int best_survival_depth = -1;
    int best_dist = INT_MAX;
    int best_followups = -1;

    const int survival_probe_depth = 8;
    const int survival_probe_expansion = 700;

    for (int action = 0; action < 4; ++action) {
        if (!is_action_locally_legal(state, s, action)) continue;
        if (is_action_immediately_unsafe_against_stronger_snake(state, s, action)) continue;

        GameState isolated = isolate_state_for_single_snake_planning(state, s.id);
        if (isolated.my_snakes.empty()) continue;
        vector<int> my_actions = infer_default_actions(isolated.my_snakes);
        vector<int> opp_actions;
        my_actions[0] = action;
        isolated.simulate(my_actions, opp_actions);

        const Snake* next_self = isolated.find_my_snake_by_id(s.id);
        if (next_self == nullptr || !next_self->is_alive || next_self->length < s.length) continue;
        if (next_self->length > s.length && !growth_state_has_sufficient_runway(isolated, s.id, *next_self, false)) continue;
        if (next_self->length > s.length && !growth_state_has_sufficient_runway(isolated, s.id, *next_self, false)) continue;

        int next_head = next_self->body[next_self->head_idx];
        bool makes_progress = (target_pos != -1) && simulated_step_makes_progress_to_target(state, s, action, target_pos);
        int dist = (target_pos != -1) ? manhattan_dist_pos(next_head, target_pos) : INT_MAX;
        int followups = isolated.count_safe_followups(*next_self);
        int survival_depth = max_reachable_survival_depth(isolated, s.id, survival_probe_depth, survival_probe_expansion);
        bool viable = followups > 0 || survival_depth >= 4;

        if (best_action == -1
            || (viable && !best_viable)
            || (viable == best_viable && makes_progress && !best_progress)
            || (viable == best_viable && makes_progress == best_progress && survival_depth > best_survival_depth)
            || (viable == best_viable && makes_progress == best_progress && survival_depth == best_survival_depth && followups > best_followups)
            || (viable == best_viable && makes_progress == best_progress && survival_depth == best_survival_depth && followups == best_followups && dist < best_dist)) {
            best_action = action;
            best_viable = viable;
            best_progress = makes_progress;
            best_survival_depth = survival_depth;
            best_dist = dist;
            best_followups = followups;
        }
    }

    return best_action;
}

static DirectAppleHint find_direct_apple_hint(const GameState& state, const Snake& s, const unordered_set<int>& forbidden_targets = {}) {
    DirectAppleHint hint;
    if ((world_width * world_height) > 220) return hint;
    int head_pos = s.body[s.head_idx];
    int center_x = max_len + world_width / 2;
    int center_y = max_len + world_height / 2;
    int center_pos = center_y * max_width + center_x;

    for (int pos = 0; pos < grid_size; ++pos) {
        if (state.grid[pos] != CELL_POWERUP) continue;
        if (forbidden_targets.find(pos) != forbidden_targets.end()) continue;
        int dist = shortest_path_distance_walls_only(state, head_pos, pos);
        if (dist == INT_MAX || dist > DIRECT_APPLE_MAX_DIST) continue;
        int action = first_action_toward_cell_walls_only(state, s, pos);
        if (action == -1) continue;

        int hx = head_pos % max_width;
        int hy = head_pos / max_width;
        int dx[] = {0, 0, -1, 1};
        int dy[] = {-1, 1, 0, 0};
        int nx = hx + dx[action];
        int ny = hy + dy[action];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
        int next_pos = ny * max_width + nx;
        int16_t cell = state.grid[next_pos];
        if (cell == CELL_WALL || cell >= CELL_SNAKE_BASE) continue;
        if (!simulated_step_makes_progress_to_target(state, s, action, pos)) continue;

        GameState isolated = isolate_state_for_single_snake_planning(state, s.id);
        if (isolated.my_snakes.empty()) continue;
        vector<int> my_actions = infer_default_actions(isolated.my_snakes);
        vector<int> opp_actions;
        my_actions[0] = action;
        isolated.simulate(my_actions, opp_actions);

        const Snake* next_self = isolated.find_my_snake_by_id(s.id);
        if (next_self == nullptr || !next_self->is_alive || next_self->length < s.length) continue;
        if (next_self->length > s.length && !growth_state_has_sufficient_runway(isolated, s.id, *next_self, false)) continue;

        if (!hint.found
            || dist < hint.distance
            || (dist == hint.distance && manhattan_dist_pos(pos, center_pos) < manhattan_dist_pos(hint.target_pos, center_pos))) {
            hint.found = true;
            hint.action = action;
            hint.target_pos = pos;
            hint.distance = dist;
        }
    }

    return hint;
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
        if (state.grid[i] == CELL_POWERUP) h = hash_combine_u64(h, static_cast<uint64_t>(i + 1000003));
    }
    return h;
}

static int choose_center_anchor_cell(const GameState& state, int self_head_pos, const unordered_set<int>& reserved_targets, int exclude_pos = -1) {
    int center_x = max_len + world_width / 2;
    int center_y = max_len + world_height / 2;
    int center_pos = center_y * max_width + center_x;
    int best_pos = -1;
    int best_score = INT_MAX;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    for (int pos = 0; pos < grid_size; ++pos) {
        if (!is_center_goal_cell_valid(state, pos)) continue;
        if (pos == exclude_pos) continue;
        if (reserved_targets.find(pos) != reserved_targets.end()) continue;
        int d = manhattan_dist_pos(pos, center_pos);
        int self_dist = manhattan_dist_pos(pos, self_head_pos);
        int px = pos % max_width;
        int py = pos / max_width;
        int open_neighbors = 0;
        int nearby_apples = 0;
        for (int i = 0; i < 4; ++i) {
            int nx = px + dx[i];
            int ny = py + dy[i];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
            int n_pos = ny * max_width + nx;
            if (!is_playable_cell(n_pos)) continue;
            if (state.grid[n_pos] != CELL_WALL) open_neighbors++;
        }
        for (int apple_pos = 0; apple_pos < grid_size; ++apple_pos) {
            if (state.grid[apple_pos] != CELL_POWERUP) continue;
            if (manhattan_dist_pos(pos, apple_pos) <= 5) nearby_apples++;
        }
        int score = d * 100 + self_dist * 6 - open_neighbors * 12 - nearby_apples * 10;
        if (score < best_score) {
            best_score = score;
            best_pos = pos;
        }
    }
    return best_pos;
}

struct FrontierScanResult {
    bool found_apple = false;
    int apple_action = -1;
    int apple_pos = -1;
    int apple_depth = INT_MAX;
    int apple_followups = -1;
    int apple_goal_dist = INT_MAX;

    bool found_apple_progress = false;
    int apple_progress_action = -1;
    int apple_progress_target = -1;
    int apple_progress_dist = INT_MAX;
    int apple_progress_depth = INT_MAX;
    int apple_progress_followups = -1;

    bool found_goal_progress = false;
    int goal_action = -1;
    int goal_head = -1;
    int goal_dist = INT_MAX;
    int goal_depth = INT_MAX;
    int goal_followups = -1;

    int expanded = 0;
    int explored_states = 0;
};

struct AppleProgressCandidate {
    int action = -1;
    int target = -1;
    int dist = INT_MAX;
    int depth = INT_MAX;
    int followups = -1;
};

struct SearchNode {
    GameState state;
    int first_action = -1;
    int depth = 0;
    int head_pos = -1;
};

struct AppleCommitSearchResult {
    bool found = false;
    int action = -1;
    int depth = INT_MAX;
};

static int count_powerups_on_grid(const GameState& state) {
    int count = 0;
    for (int pos = 0; pos < grid_size; ++pos) {
        if (state.grid[pos] == CELL_POWERUP) count++;
    }
    return count;
}

static int max_reachable_survival_depth(const GameState& state, int snake_id, int depth_limit, int expansion_limit) {
    if (depth_limit <= 0 || expansion_limit <= 0) return 0;

    GameState start_state = isolate_state_for_single_snake_planning(state, snake_id);
    const Snake* start_self = start_state.find_my_snake_by_id(snake_id);
    if (start_self == nullptr || !start_self->is_alive || start_self->length <= 0) return 0;

    deque<SearchNode> nodes;
    nodes.push_back({start_state, -1, 0, start_self->body[start_self->head_idx]});

    queue<int> q;
    q.push(0);

    unordered_map<uint64_t, int> best_depth;
    best_depth.reserve(1024);
    best_depth[encode_single_snake_plan_hash(start_state, *start_self)] = 0;

    int expanded = 0;
    int best_depth_reached = 0;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    while (!q.empty() && !out_of_time() && expanded < expansion_limit) {
        int idx = q.front();
        q.pop();

        const SearchNode& node = nodes[idx];
        const Snake* cur_self = node.state.find_my_snake_by_id(snake_id);
        if (cur_self == nullptr || !cur_self->is_alive || cur_self->length <= 0) continue;

        expanded++;
        best_depth_reached = max(best_depth_reached, node.depth);
        if (node.depth >= depth_limit) continue;

        int head_pos = cur_self->body[cur_self->head_idx];
        int hx = head_pos % max_width;
        int hy = head_pos / max_width;

        for (int action = 0; action < 4; ++action) {
            if (is_backward_action(*cur_self, action)) continue;
            int nx = hx + dx[action];
            int ny = hy + dy[action];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;

            int n_pos = ny * max_width + nx;
            int16_t next_cell = node.state.grid[n_pos];
            if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;

            GameState next_state = node.state;
            vector<int> my_actions = infer_default_actions(next_state.my_snakes);
            vector<int> opp_actions;
            my_actions[0] = action;
            next_state.simulate(my_actions, opp_actions);

            const Snake* next_self = next_state.find_my_snake_by_id(snake_id);
            if (next_self == nullptr || !next_self->is_alive || next_self->length <= 0) continue;

            int next_depth = node.depth + 1;
            uint64_t h = encode_single_snake_plan_hash(next_state, *next_self);
            auto it = best_depth.find(h);
            if (it != best_depth.end() && it->second <= next_depth) continue;
            best_depth[h] = next_depth;

            nodes.push_back({next_state, -1, next_depth, next_self->body[next_self->head_idx]});
            q.push(static_cast<int>(nodes.size()) - 1);
        }
    }

    return best_depth_reached;
}

static AppleCommitSearchResult find_safe_commit_to_apple(const GameState& state, int snake_id, int apple_pos, int depth_limit) {
    AppleCommitSearchResult result;
    if (apple_pos < 0 || apple_pos >= grid_size || depth_limit <= 0) return result;

    GameState start_state = isolate_state_for_single_snake_planning(state, snake_id);
    const Snake* start_self = start_state.find_my_snake_by_id(snake_id);
    if (start_self == nullptr || !start_self->is_alive || start_self->length <= 0) return result;

    deque<SearchNode> nodes;
    nodes.push_back({start_state, -1, 0, start_self->body[start_self->head_idx]});

    queue<int> q;
    q.push(0);

    unordered_map<uint64_t, int> best_depth;
    best_depth.reserve(512);
    best_depth[encode_single_snake_plan_hash(start_state, *start_self)] = 0;

    int start_length = start_self->length;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    while (!q.empty() && !out_of_time()) {
        int idx = q.front();
        q.pop();

        const SearchNode& node = nodes[idx];
        const Snake* cur_self = node.state.find_my_snake_by_id(snake_id);
        if (cur_self == nullptr || !cur_self->is_alive || cur_self->length < start_length) continue;
        if (node.depth >= depth_limit) continue;

        int head_pos = cur_self->body[cur_self->head_idx];
        int hx = head_pos % max_width;
        int hy = head_pos / max_width;

        for (int action = 0; action < 4; ++action) {
            if (is_backward_action(*cur_self, action)) continue;
            int nx = hx + dx[action];
            int ny = hy + dy[action];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;

            int n_pos = ny * max_width + nx;
            int16_t next_cell = node.state.grid[n_pos];
            if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;

            GameState next_state = node.state;
            vector<int> my_actions = infer_default_actions(next_state.my_snakes);
            vector<int> opp_actions;
            my_actions[0] = action;
            next_state.simulate(my_actions, opp_actions);

            const Snake* next_self = next_state.find_my_snake_by_id(snake_id);
            if (next_self == nullptr || !next_self->is_alive || next_self->length < start_length) continue;

            int first_action = (node.first_action == -1) ? action : node.first_action;
            int next_depth = node.depth + 1;
            int next_head = next_self->body[next_self->head_idx];
            if (next_head == apple_pos && next_self->length > start_length) {
                GrowthRunwayEvaluation eval = evaluate_growth_runway(next_state, snake_id, *next_self, true);
                if (eval.sufficient) {
                    result.found = true;
                    result.action = first_action;
                    result.depth = next_depth;
                    return result;
                }
                log_debug_growth_rejection("commit_search", snake_id, *next_self, eval, -1, -1, next_state.count_safe_followups(*next_self));
            }

            uint64_t h = encode_single_snake_plan_hash(next_state, *next_self);
            auto it = best_depth.find(h);
            if (it != best_depth.end() && it->second <= next_depth) continue;
            best_depth[h] = next_depth;

            nodes.push_back({next_state, first_action, next_depth, next_head});
            q.push(static_cast<int>(nodes.size()) - 1);
        }
    }

    return result;
}

static void debug_log_specific_apple_growth_probe(const GameState& state, int snake_id, int apple_pos, int depth_limit) {
    if (apple_pos < 0 || apple_pos >= grid_size || depth_limit <= 0) return;
    if (!is_debug_target_apple_pos(apple_pos)) return;

    GameState start_state = isolate_state_for_single_snake_planning(state, snake_id);
    const Snake* start_self = start_state.find_my_snake_by_id(snake_id);
    if (start_self == nullptr || !start_self->is_alive || start_self->length <= 0) return;

    deque<SearchNode> nodes;
    nodes.push_back({start_state, -1, 0, start_self->body[start_self->head_idx]});

    queue<int> q;
    q.push(0);

    unordered_map<uint64_t, int> best_depth;
    best_depth.reserve(512);
    best_depth[encode_single_snake_plan_hash(start_state, *start_self)] = 0;

    int start_length = start_self->length;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    while (!q.empty() && !out_of_time()) {
        int idx = q.front();
        q.pop();

        const SearchNode& node = nodes[idx];
        const Snake* cur_self = node.state.find_my_snake_by_id(snake_id);
        if (cur_self == nullptr || !cur_self->is_alive || cur_self->length < start_length) continue;
        if (node.depth >= depth_limit) continue;

        int head_pos = cur_self->body[cur_self->head_idx];
        int hx = head_pos % max_width;
        int hy = head_pos / max_width;

        for (int action = 0; action < 4; ++action) {
            if (is_backward_action(*cur_self, action)) continue;
            int nx = hx + dx[action];
            int ny = hy + dy[action];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;

            int n_pos = ny * max_width + nx;
            int16_t next_cell = node.state.grid[n_pos];
            if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;

            GameState next_state = node.state;
            vector<int> my_actions = infer_default_actions(next_state.my_snakes);
            vector<int> opp_actions;
            my_actions[0] = action;
            next_state.simulate(my_actions, opp_actions);

            const Snake* next_self = next_state.find_my_snake_by_id(snake_id);
            if (next_self == nullptr || !next_self->is_alive || next_self->length < start_length) continue;

            int next_head = next_self->body[next_self->head_idx];
            int next_depth = node.depth + 1;
            if (next_head == apple_pos && next_self->length > start_length) {
                GrowthRunwayEvaluation eval = evaluate_growth_runway(next_state, snake_id, *next_self, true);
                int local_followups = next_state.count_safe_followups(*next_self);
                int simulated_followups = count_simulated_safe_followups(next_state, *next_self);
                int simulated_followthroughs = (simulated_followups > 0)
                    ? count_simulated_safe_followthrough_moves(next_state, *next_self)
                    : 0;
                mylog << "DEBUG_PROBE_GROWN_STATE"
                      << " turn=" << g_turn_counter
                      << " snake=" << snake_id
                      << " target_x=0 target_y=2"
                      << " depth=" << next_depth
                      << " head_x=" << world_x_from_pos(next_head)
                      << " head_y=" << world_y_from_pos(next_head)
                      << " length=" << next_self->length
                      << " body=" << format_snake_body_world(*next_self)
                      << " local_followups=" << local_followups
                      << " simulated_followups=" << simulated_followups
                      << " simulated_followthroughs=" << simulated_followthroughs
                      << " continuation_depth=" << eval.continuation_depth
                      << " continuation_expansion=" << eval.continuation_expansion
                      << " remaining_powerups=" << eval.remaining_powerups
                      << " survival_runway=" << eval.survival_runway
                      << " minimum_runway=" << eval.minimum_runway
                      << " sufficient=" << (eval.sufficient ? 1 : 0)
                      << '\n';
                return;
            }

            uint64_t h = encode_single_snake_plan_hash(next_state, *next_self);
            auto it = best_depth.find(h);
            if (it != best_depth.end() && it->second <= next_depth) continue;
            best_depth[h] = next_depth;

            nodes.push_back({next_state, -1, next_depth, next_head});
            q.push(static_cast<int>(nodes.size()) - 1);
        }
    }

    mylog << "DEBUG_PROBE_GROWN_STATE turn=" << g_turn_counter
          << " snake=" << snake_id
          << " target_x=0 target_y=2 found=0\n";
}

static int decode_debug_action(char c) {
    if (c == 'U') return 0;
    if (c == 'D') return 1;
    if (c == 'L') return 2;
    if (c == 'R') return 3;
    return -1;
}

static void debug_log_fixed_left_apple_route(const GameState& state, int snake_id, const string& route) {
    GameState cur_state = isolate_state_for_single_snake_planning(state, snake_id);
    const Snake* start_self = cur_state.find_my_snake_by_id(snake_id);
    if (start_self == nullptr || !start_self->is_alive || start_self->length <= 0) return;

    mylog << "DEBUG_ROUTE_TRACE turn=" << g_turn_counter
          << " snake=" << snake_id
          << " route=" << route
          << " start_body=" << format_snake_body_world(*start_self)
          << '\n';

    for (size_t i = 0; i < route.size(); ++i) {
        const Snake* self = cur_state.find_my_snake_by_id(snake_id);
        if (self == nullptr || !self->is_alive || self->length <= 0) {
            mylog << "DEBUG_ROUTE_FAIL turn=" << g_turn_counter
                  << " snake=" << snake_id
                  << " step=" << (i + 1)
                  << " action=" << route[i]
                  << " reason=missing_self_before_step\n";
            return;
        }

        int action = decode_debug_action(route[i]);
        int head_pos = self->body[self->head_idx];
        int hx = head_pos % max_width;
        int hy = head_pos / max_width;
        int dx[] = {0, 0, -1, 1};
        int dy[] = {-1, 1, 0, 0};

        if (action < 0 || action > 3) {
            mylog << "DEBUG_ROUTE_FAIL turn=" << g_turn_counter
                  << " snake=" << snake_id
                  << " step=" << (i + 1)
                  << " action=" << route[i]
                  << " reason=unknown_action\n";
            return;
        }

        if (is_backward_action(*self, action)) {
            mylog << "DEBUG_ROUTE_FAIL turn=" << g_turn_counter
                  << " snake=" << snake_id
                  << " step=" << (i + 1)
                  << " action=" << route[i]
                  << " head_x=" << world_x_from_pos(head_pos)
                  << " head_y=" << world_y_from_pos(head_pos)
                  << " reason=backward_action"
                  << " body=" << format_snake_body_world(*self)
                  << '\n';
            return;
        }

        int nx = hx + dx[action];
        int ny = hy + dy[action];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) {
            mylog << "DEBUG_ROUTE_FAIL turn=" << g_turn_counter
                  << " snake=" << snake_id
                  << " step=" << (i + 1)
                  << " action=" << route[i]
                  << " head_x=" << world_x_from_pos(head_pos)
                  << " head_y=" << world_y_from_pos(head_pos)
                  << " reason=out_of_bounds"
                  << '\n';
            return;
        }

        int n_pos = ny * max_width + nx;
        int16_t next_cell = cur_state.grid[n_pos];
        if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) {
            mylog << "DEBUG_ROUTE_FAIL turn=" << g_turn_counter
                  << " snake=" << snake_id
                  << " step=" << (i + 1)
                  << " action=" << route[i]
                  << " next_x=" << world_x_from_pos(n_pos)
                  << " next_y=" << world_y_from_pos(n_pos)
                  << " reason=blocked_before_sim"
                  << " cell=" << next_cell
                  << " body=" << format_snake_body_world(*self)
                  << '\n';
            return;
        }

        vector<int> my_actions = infer_default_actions(cur_state.my_snakes);
        vector<int> opp_actions;
        my_actions[0] = action;
        cur_state.simulate(my_actions, opp_actions);

        const Snake* next_self = cur_state.find_my_snake_by_id(snake_id);
        if (next_self == nullptr || !next_self->is_alive || next_self->length <= 0) {
            mylog << "DEBUG_ROUTE_FAIL turn=" << g_turn_counter
                  << " snake=" << snake_id
                  << " step=" << (i + 1)
                  << " action=" << route[i]
                  << " next_x=" << world_x_from_pos(n_pos)
                  << " next_y=" << world_y_from_pos(n_pos)
                  << " reason=died_after_sim"
                  << '\n';
            return;
        }

        mylog << "DEBUG_ROUTE_STEP turn=" << g_turn_counter
              << " snake=" << snake_id
              << " step=" << (i + 1)
              << " action=" << route[i]
              << " head_x=" << world_x_from_pos(next_self->body[next_self->head_idx])
              << " head_y=" << world_y_from_pos(next_self->body[next_self->head_idx])
              << " length=" << next_self->length
              << " body=" << format_snake_body_world(*next_self)
              << '\n';
    }

    const Snake* final_self = cur_state.find_my_snake_by_id(snake_id);
    if (final_self != nullptr && final_self->is_alive && final_self->length > 0) {
        int final_head = final_self->body[final_self->head_idx];
        mylog << "DEBUG_ROUTE_END turn=" << g_turn_counter
              << " snake=" << snake_id
              << " head_x=" << world_x_from_pos(final_head)
              << " head_y=" << world_y_from_pos(final_head)
              << " length=" << final_self->length
              << " body=" << format_snake_body_world(*final_self)
              << '\n';
    }
}

static bool has_reachable_future_growth(const GameState& state, int snake_id, int depth_limit, int expansion_limit) {
    if (depth_limit <= 0 || expansion_limit <= 0) return false;

    GameState start_state = isolate_state_for_single_snake_planning(state, snake_id);
    const Snake* start_self = start_state.find_my_snake_by_id(snake_id);
    if (start_self == nullptr || !start_self->is_alive || start_self->length <= 0) return false;
    if (count_powerups_on_grid(start_state) == 0) return false;

    deque<SearchNode> nodes;
    nodes.push_back({start_state, -1, 0, start_self->body[start_self->head_idx]});

    queue<int> q;
    q.push(0);

    unordered_map<uint64_t, int> best_depth;
    best_depth.reserve(1024);
    best_depth[encode_single_snake_plan_hash(start_state, *start_self)] = 0;

    int start_length = start_self->length;
    int expanded = 0;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    while (!q.empty() && !out_of_time() && expanded < expansion_limit) {
        int idx = q.front();
        q.pop();

        const SearchNode& node = nodes[idx];
        const Snake* cur_self = node.state.find_my_snake_by_id(snake_id);
        if (cur_self == nullptr || !cur_self->is_alive || cur_self->length < start_length) continue;

        expanded++;
        if (node.depth >= depth_limit) continue;

        int head_pos = cur_self->body[cur_self->head_idx];
        int hx = head_pos % max_width;
        int hy = head_pos / max_width;

        for (int action = 0; action < 4; ++action) {
            if (is_backward_action(*cur_self, action)) continue;
            int nx = hx + dx[action];
            int ny = hy + dy[action];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;

            int n_pos = ny * max_width + nx;
            int16_t next_cell = node.state.grid[n_pos];
            if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;

            GameState next_state = node.state;
            vector<int> my_actions = infer_default_actions(next_state.my_snakes);
            vector<int> opp_actions;
            my_actions[0] = action;
            next_state.simulate(my_actions, opp_actions);

            const Snake* next_self = next_state.find_my_snake_by_id(snake_id);
            if (next_self == nullptr || !next_self->is_alive || next_self->length < start_length) continue;
            if (next_self->length > start_length) return true;

            uint64_t h = encode_single_snake_plan_hash(next_state, *next_self);
            int next_depth = node.depth + 1;
            auto it = best_depth.find(h);
            if (it != best_depth.end() && it->second <= next_depth) continue;
            best_depth[h] = next_depth;

            nodes.push_back({next_state, -1, next_depth, next_self->body[next_self->head_idx]});
            q.push(static_cast<int>(nodes.size()) - 1);
        }
    }

    return false;
}

static FrontierScanResult scan_reachable_frontier(const GameState& state, int snake_id, const ScanBudgetConfig& budget, int desired_goal_pos,
                                                 const unordered_set<int>& forbidden_apple_targets = {}) {
    FrontierScanResult result;
    GameState start_state = isolate_state_for_single_snake_planning(state, snake_id);
    const Snake* start_self = start_state.find_my_snake_by_id(snake_id);
    if (start_self == nullptr || !start_self->is_alive || start_self->length <= 0) return result;
    const int board_area = world_width * world_height;
    const int depth_limit = budget.depth_limit;
    const int expansion_limit = budget.expansion_limit;

    vector<int> initial_powerups;
    initial_powerups.reserve(16);
    for (int pos = 0; pos < grid_size; ++pos) {
        if (start_state.grid[pos] != CELL_POWERUP) continue;
        if (forbidden_apple_targets.find(pos) != forbidden_apple_targets.end()) continue;
        initial_powerups.push_back(pos);
    }
    const bool use_strict_followthrough_checks = board_area <= 320 && initial_powerups.size() <= 4;
    const bool use_low_escape_growth_checks = budget.expansion_limit <= 2400;

    deque<SearchNode> nodes;
    int start_head = start_self->body[start_self->head_idx];
    nodes.push_back({start_state, -1, 0, start_head});

    queue<int> q;
    q.push(0);

    unordered_map<uint64_t, int> best_depth;
    best_depth.reserve(2048);
    best_depth[encode_single_snake_plan_hash(start_state, *start_self)] = 0;
    result.explored_states = 1;
    unordered_map<int, AppleProgressCandidate> apple_progress_by_target;
    apple_progress_by_target.reserve(initial_powerups.size());
    unordered_set<int> dead_end_apple_targets;
    dead_end_apple_targets.reserve(initial_powerups.size());
    PhaseTimingBudget phase_budget;
    phase_budget.deep_scan_budget_ms = max(4, budget.scan_budget_ms);
    phase_budget.followthrough_budget_ms = max(2, budget.followthrough_budget_ms);
    ScopedSearchPhase deep_scan_phase(&phase_budget, SearchPhase::DeepScan);

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    int start_length = start_self->length;

    while (!q.empty() && !out_of_time() && result.expanded < expansion_limit) {
        int idx = q.front();
        q.pop();
        const SearchNode& node = nodes[idx];
        const Snake* cur_self = node.state.find_my_snake_by_id(snake_id);
        if (cur_self == nullptr || !cur_self->is_alive) continue;

        result.expanded++;

        if (node.first_action != -1 && !initial_powerups.empty()) {
            int followups = node.state.count_safe_followups(*cur_self);
            for (int p : initial_powerups) {
                if (node.state.grid[p] != CELL_POWERUP) continue;
                int d = manhattan_dist_pos(node.head_pos, p);
                AppleProgressCandidate& candidate = apple_progress_by_target[p];
                if (candidate.target == -1
                    || d < candidate.dist
                    || (d == candidate.dist && followups > candidate.followups)
                    || (d == candidate.dist && followups == candidate.followups && node.depth < candidate.depth)) {
                    candidate.action = node.first_action;
                    candidate.target = p;
                    candidate.dist = d;
                    candidate.depth = node.depth;
                    candidate.followups = followups;
                }
            }
        }

        if (desired_goal_pos != -1 && node.first_action != -1) {
            int followups = node.state.count_safe_followups(*cur_self);
            int dist = manhattan_dist_pos(node.head_pos, desired_goal_pos);
            if (!result.found_goal_progress
                || dist < result.goal_dist
                || (dist == result.goal_dist && followups > result.goal_followups)
                || (dist == result.goal_dist && followups == result.goal_followups && node.depth < result.goal_depth)) {
                result.found_goal_progress = true;
                result.goal_action = node.first_action;
                result.goal_head = node.head_pos;
                result.goal_dist = dist;
                result.goal_depth = node.depth;
                result.goal_followups = followups;
            }
        }

        if (node.depth >= depth_limit) continue;

        int head_pos = cur_self->body[cur_self->head_idx];
        int hx = head_pos % max_width;
        int hy = head_pos / max_width;

        for (int a = 0; a < 4; ++a) {
            if (is_backward_action(*cur_self, a)) continue;
            int nx = hx + dx[a];
            int ny = hy + dy[a];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
            int n_pos = ny * max_width + nx;
            int16_t next_cell = node.state.grid[n_pos];
            if (next_cell == CELL_WALL || next_cell >= CELL_SNAKE_BASE) continue;

            GameState next_state = node.state;
            vector<int> my_actions = infer_default_actions(next_state.my_snakes);
            vector<int> opp_actions;
            my_actions[0] = a;
            next_state.simulate(my_actions, opp_actions);

            const Snake* next_self = next_state.find_my_snake_by_id(snake_id);
            if (next_self == nullptr || !next_self->is_alive) continue;
            if (next_self->length < start_length) continue;

            int next_head = next_self->body[next_self->head_idx];
            if (!is_playable_cell(next_head)) continue;
            if (snake_body_hash(*next_self) == snake_body_hash(*cur_self) && next_self->length == cur_self->length) continue;

            int first_action = (node.first_action == -1) ? a : node.first_action;
            int next_depth = node.depth + 1;
            int followups = next_state.count_safe_followups(*next_self);
            int goal_dist = (desired_goal_pos != -1) ? manhattan_dist_pos(next_head, desired_goal_pos) : INT_MAX;

            if (next_self->length > cur_self->length && forbidden_apple_targets.find(next_head) == forbidden_apple_targets.end()) {
                int simulated_followups = count_simulated_safe_followups(next_state, *next_self);
                int simulated_followthroughs = simulated_followups;
                bool suspicious_low_escape = (followups <= 1 || simulated_followups <= 1);
                bool should_verify_escape = use_strict_followthrough_checks || (use_low_escape_growth_checks && suspicious_low_escape);
                bool growth_has_runway = simulated_followups > 0;
                if (should_verify_escape && !phase_budget_exhausted(phase_budget, SearchPhase::FollowThrough)) {
                    ScopedSearchPhase followthrough_phase(&phase_budget, SearchPhase::FollowThrough);
                    if (simulated_followups > 0) {
                        simulated_followthroughs = count_simulated_safe_followthrough_moves(next_state, *next_self);
                    }
                    if (!out_of_time()) {
                        GrowthRunwayEvaluation eval = evaluate_growth_runway(next_state, snake_id, *next_self, use_strict_followthrough_checks);
                        growth_has_runway = eval.sufficient;
                        if (!growth_has_runway && (simulated_followthroughs <= 2 || suspicious_low_escape || simulated_followups <= 0)) {
                            log_debug_growth_rejection("frontier_scan", snake_id, *next_self, eval, simulated_followups, simulated_followthroughs, followups);
                            dead_end_apple_targets.insert(next_head);
                            continue;
                        }
                    }
                } else if (simulated_followups <= 0) {
                    continue;
                }
                if (simulated_followups <= 0 && !growth_has_runway) continue;
                if (!result.found_apple
                    || next_depth < result.apple_depth
                    || (next_depth == result.apple_depth && simulated_followups > result.apple_followups)
                    || (next_depth == result.apple_depth && simulated_followups == result.apple_followups && goal_dist < result.apple_goal_dist)) {
                    result.found_apple = true;
                    result.apple_action = first_action;
                    result.apple_pos = next_head;
                    result.apple_depth = next_depth;
                    result.apple_followups = simulated_followups;
                    result.apple_goal_dist = goal_dist;
                }
            }

            uint64_t h = encode_single_snake_plan_hash(next_state, *next_self);
            auto it = best_depth.find(h);
            if (it != best_depth.end() && it->second <= next_depth) continue;
            best_depth[h] = next_depth;

            nodes.push_back({next_state, first_action, next_depth, next_head});
            q.push(static_cast<int>(nodes.size()) - 1);
            result.explored_states++;
        }
    }

    for (const auto& [target, candidate] : apple_progress_by_target) {
        if (candidate.action == -1 || candidate.target == -1) continue;
        if (dead_end_apple_targets.find(target) != dead_end_apple_targets.end()) continue;
        if (!result.found_apple_progress
            || candidate.dist < result.apple_progress_dist
            || (candidate.dist == result.apple_progress_dist && candidate.followups > result.apple_progress_followups)
            || (candidate.dist == result.apple_progress_dist && candidate.followups == result.apple_progress_followups && candidate.depth < result.apple_progress_depth)) {
            result.found_apple_progress = true;
            result.apple_progress_action = candidate.action;
            result.apple_progress_target = candidate.target;
            result.apple_progress_dist = candidate.dist;
            result.apple_progress_depth = candidate.depth;
            result.apple_progress_followups = candidate.followups;
        }
    }

    return result;
}

static ScanBudgetConfig choose_scan_budget(const GameState& state, const Snake& s, size_t snake_index, size_t total_snakes, bool has_apple_goal) {
    ScanBudgetConfig config;

    int board_area = world_width * world_height;
    config.depth_limit = (board_area >= 500) ? SCAN_DEPTH_LARGE : SCAN_DEPTH_SMALL;
    config.expansion_limit = (board_area >= 500) ? SCAN_EXPANSION_LIMIT : min(SCAN_EXPANSION_LIMIT, 4200);
    config.tier = (board_area >= 500) ? "deep" : "medium";

    if (total_snakes >= 4) {
        config.depth_limit = min(config.depth_limit, 14);
        config.expansion_limit = min(config.expansion_limit, 2400);
        config.tier = "medium";
        if (snake_index >= 1 && !has_apple_goal) {
            config.depth_limit = min(config.depth_limit, 10);
            config.expansion_limit = min(config.expansion_limit, 1500);
            config.tier = "shallow";
        }
        if (snake_index >= 2) {
            config.depth_limit = min(config.depth_limit, 8);
            config.expansion_limit = min(config.expansion_limit, 1000);
            config.tier = "shallow";
        }
    } else if (total_snakes == 3) {
        config.depth_limit = min(config.depth_limit, 18);
        config.expansion_limit = min(config.expansion_limit, 3000);
        config.tier = "medium";
    } else if (total_snakes == 2) {
        config.depth_limit = min(config.depth_limit, 22);
        config.expansion_limit = min(config.expansion_limit, 4800);
        config.tier = (board_area >= 500) ? "deep" : "medium";
    }

    if (has_apple_goal) {
        config.depth_limit = min(28, config.depth_limit + 1);
    }

    if (s.length <= 4) {
        if (total_snakes > 1) {
            config.depth_limit = min(config.depth_limit, 16);
            config.expansion_limit = min(config.expansion_limit, 2400);
        } else if (board_area < 250) {
            config.depth_limit = min(config.depth_limit, 18);
            config.expansion_limit = min(config.expansion_limit, 3200);
        }
        if (string(config.tier) == "deep") config.tier = "medium";
    }

    int remaining_ms = max(0, TURN_BUDGET_MS - elapsed_turn_ms());
    int remaining_snakes = max<int>(1, static_cast<int>(total_snakes - snake_index));
    int fair_share_ms = remaining_ms / remaining_snakes;
    config.scan_budget_ms = max(4, (fair_share_ms * DEEP_SCAN_BUDGET_PCT) / 100);
    config.followthrough_budget_ms = max(2, (fair_share_ms * FOLLOWTHROUGH_BUDGET_PCT) / 100);
    if (fair_share_ms < 16) {
        config.depth_limit = min(config.depth_limit, 8);
        config.expansion_limit = min(config.expansion_limit, 900);
        config.tier = "shallow";
    } else if (fair_share_ms < 24) {
        config.depth_limit = min(config.depth_limit, 12);
        config.expansion_limit = min(config.expansion_limit, 1800);
        config.tier = "medium";
    } else if (fair_share_ms < 32) {
        config.depth_limit = min(config.depth_limit, 18);
        config.expansion_limit = min(config.expansion_limit, 3200);
        if (string(config.tier) == "deep") config.tier = "medium";
    }

    config.depth_limit = max(6, min(config.depth_limit, 28));
    config.expansion_limit = max(600, min(config.expansion_limit, SCAN_EXPANSION_LIMIT));
    int total_phase_budget_ms = config.scan_budget_ms + config.followthrough_budget_ms;
    int max_assignable_ms = max(6, (fair_share_ms * (100 - TURN_RESERVE_BUDGET_PCT)) / 100);
    if (total_phase_budget_ms > max_assignable_ms) {
        config.scan_budget_ms = max(4, (config.scan_budget_ms * max_assignable_ms) / total_phase_budget_ms);
        config.followthrough_budget_ms = max(2, max_assignable_ms - config.scan_budget_ms);
    }
    return config;
}

static PersistentGoal get_or_refresh_center_goal(const GameState& state, const Snake& s, const unordered_set<int>& reserved_targets) {
    auto it = g_center_goal_by_snake.find(s.id);
    if (it != g_center_goal_by_snake.end()
        && it->second.expires_turn > g_turn_counter
        && is_center_goal_cell_valid(state, it->second.target_pos)
        && reserved_targets.find(it->second.target_pos) == reserved_targets.end()
        && it->second.target_pos != s.body[s.head_idx]) {
        return it->second;
    }

    int goal_pos = choose_center_anchor_cell(state, s.body[s.head_idx], reserved_targets, s.body[s.head_idx]);
    PersistentGoal goal;
    goal.target_pos = goal_pos;
    goal.expires_turn = g_turn_counter + LONG_TERM_GOAL_TTL;
    g_center_goal_by_snake[s.id] = goal;
    return goal;
}

static void clear_center_goal(int snake_id) {
    g_center_goal_by_snake.erase(snake_id);
}

static PersistentGoal get_valid_apple_goal(const GameState& state, const Snake& s) {
    auto it = g_apple_goal_by_snake.find(s.id);
    if (it == g_apple_goal_by_snake.end()) return {};
    if (it->second.expires_turn <= g_turn_counter) return {};
    if (it->second.target_pos < 0 || it->second.target_pos >= grid_size) return {};
    if (state.grid[it->second.target_pos] != CELL_POWERUP) return {};
    if (it->second.target_pos == s.body[s.head_idx]) return {};
    return it->second;
}

static void set_apple_goal(int snake_id, int target_pos) {
    if (target_pos < 0) return;
    g_apple_goal_by_snake[snake_id] = {target_pos, g_turn_counter + APPLE_GOAL_TTL};
}

static void clear_apple_goal(int snake_id) {
    g_apple_goal_by_snake.erase(snake_id);
}

static bool is_target_contested_by_stronger_snake(const GameState& state, const Snake& self, int target_pos) {
    if (target_pos < 0 || target_pos >= grid_size) return false;

    int self_head = self.body[self.head_idx];
    int self_dist = shortest_path_distance_walls_only(state, self_head, target_pos);
    if (self_dist == INT_MAX) return false;

    auto loses_to_other = [&](const Snake& other) {
        if (!other.is_alive || other.length <= 0 || other.id == self.id) return false;
        int other_head = other.body[other.head_idx];
        int other_dist = shortest_path_distance_walls_only(state, other_head, target_pos);
        if (other_dist == INT_MAX) return false;
        if (other.length > self.length && other_dist <= self_dist + 1) return true;
        if (other.length == self.length && other_dist < self_dist) return true;
        return false;
    };

    for (const Snake& other : state.my_snakes) {
        if (loses_to_other(other)) return true;
    }
    for (const Snake& other : state.opp_snakes) {
        if (loses_to_other(other)) return true;
    }
    return false;
}

int main() {
    signal(SIGSEGV, crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);
    signal(SIGFPE, crash_signal_handler);
    signal(SIGILL, crash_signal_handler);
    std::set_terminate(bot_terminate_handler);

    int my_id;
    cin >> my_id;
    cin >> world_width;
    cin >> world_height;

    vector<string> initial_rows(world_height);
    cin >> ws;
    for (int i = 0; i < world_height; i++) getline(cin, initial_rows[i]);

    int snakebots_per_player;
    cin >> snakebots_per_player;
    vector<int> my_snakebots;
    for (int i = 0; i < snakebots_per_player; i++) {
        int id;
        cin >> id;
        my_snakebots.push_back(id);
    }
    for (int i = 0; i < snakebots_per_player; i++) {
        int ignored;
        cin >> ignored;
    }

    bool is_first_turn = true;
    GameState state;
    vector<int16_t> static_walls;
    int counter = 0;

    while (1) {
        counter++;
        g_turn_counter = counter;

        int power_source_count = 0;
        if (!(cin >> power_source_count)) break;

        vector<int> powerup_positions;
        powerup_positions.reserve(power_source_count);

        if (is_first_turn) {
            total_powerups_count = power_source_count;
            max_len = 3 + total_powerups_count;
            max_width = (2 * max_len) + world_width;
            max_height = (2 * max_len) + world_height;
            grid_size = max_width * max_height;
            state.grid.assign(grid_size, CELL_EMPTY);

            int start_x = max_len;
            int start_y = max_len;
            for (int y = 0; y < world_height; y++) {
                for (int x = 0; x < world_width; x++) {
                    if (initial_rows[y][x] == '#') {
                        state.grid[(start_y + y) * max_width + (start_x + x)] = CELL_WALL;
                    }
                }
            }
            static_walls = state.grid;
            is_first_turn = false;
        }

        state.grid = static_walls;
        for (int i = 0; i < power_source_count; i++) {
            int x = 0;
            int y = 0;
            cin >> x >> y;
            int sx = max_len + x;
            int sy = max_len + y;
            int pos = sy * max_width + sx;
            state.grid[pos] = CELL_POWERUP;
            powerup_positions.push_back(pos);
        }

        int snakebot_count = 0;
        cin >> snakebot_count;
        state.my_snakes.clear();
        state.opp_snakes.clear();

        for (int i = 0; i < snakebot_count; i++) {
            int snakebot_id;
            string body_str;
            if (!(cin >> snakebot_id >> body_str)) break;

            Snake s;
            s.id = snakebot_id;
            s.is_alive = true;
            s.body.assign(max_len, -1);

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
                int body_pos = sy * max_width + sx;
                if (body_count < max_len) s.body[body_count++] = body_pos;
                state.grid[body_pos] = CELL_SNAKE_BASE + s.id;
                pos = colon + 1;
            }

            s.length = body_count;
            if (s.length <= 0) continue;
            s.head_idx = 0;
            s.tail_idx = s.length - 1;

            bool is_mine = false;
            for (int id : my_snakebots) {
                if (id == s.id) {
                    is_mine = true;
                    break;
                }
            }
            if (is_mine) state.my_snakes.push_back(s);
            else state.opp_snakes.push_back(s);
        }

        turn_start_time = high_resolution_clock::now();

        vector<string> action_strs;
        vector<int> current_my_actions(state.my_snakes.size(), 0);

        auto append_action_with_target = [&](size_t idx, int action, int target_pos) {
            const Snake& snake = state.my_snakes[idx];
            string cmd = to_string(snake.id) + " " + action_to_string(action);
            if (target_pos >= 0) {
                int tx = (target_pos % max_width) - max_len;
                int ty = (target_pos / max_width) - max_len;
                cmd += " " + to_string(tx) + " " + to_string(ty);
            }
            action_strs.push_back(cmd);
        };

        unordered_set<int> reserved_center_targets;
        unordered_set<int> reserved_apple_targets;

        for (size_t s_idx = 0; s_idx < state.my_snakes.size(); ++s_idx) {
            const Snake& s = state.my_snakes[s_idx];
            if (!s.is_alive || s.length <= 0) continue;

            int head_pos = s.body[s.head_idx];
            int desired_goal = -1;
            bool using_direct_apple_hint = false;
            PersistentGoal apple_goal = get_valid_apple_goal(state, s);
            bool apple_goal_contested = (apple_goal.target_pos != -1)
                && is_target_contested_by_stronger_snake(state, s, apple_goal.target_pos);
            if (apple_goal_contested) {
                clear_apple_goal(s.id);
                apple_goal = {};
            }

            if (apple_goal.target_pos != -1) {
                desired_goal = apple_goal.target_pos;
                reserved_apple_targets.insert(desired_goal);
            } else {
                DirectAppleHint direct_hint = find_direct_apple_hint(state, s, reserved_apple_targets);
                bool direct_hint_contested = direct_hint.found
                    && is_target_contested_by_stronger_snake(state, s, direct_hint.target_pos);
                if (direct_hint.found && !direct_hint_contested) {
                    desired_goal = direct_hint.target_pos;
                    using_direct_apple_hint = true;
                    reserved_apple_targets.insert(desired_goal);
                } else {
                    PersistentGoal center_goal = get_or_refresh_center_goal(state, s, reserved_center_targets);
                    desired_goal = center_goal.target_pos;
                    if (desired_goal != -1) reserved_center_targets.insert(desired_goal);
                }
            }

            if (g_turn_counter == 1) {
                for (int pos = 0; pos < grid_size; ++pos) {
                    if (state.grid[pos] == CELL_POWERUP && is_debug_target_apple_pos(pos)) {
                        debug_log_specific_apple_growth_probe(state, s.id, pos, 18);
                        debug_log_fixed_left_apple_route(state, s.id, "LULUUL");
                        break;
                    }
                }
            }

            ScanBudgetConfig scan_budget = choose_scan_budget(state, s, s_idx, state.my_snakes.size(), apple_goal.target_pos != -1);
            unordered_set<int> scan_forbidden_apple_targets = reserved_apple_targets;
            if (apple_goal.target_pos != -1) scan_forbidden_apple_targets.erase(apple_goal.target_pos);
            if (using_direct_apple_hint && desired_goal != -1) scan_forbidden_apple_targets.erase(desired_goal);
            FrontierScanResult scan = scan_reachable_frontier(state, s.id, scan_budget, desired_goal, scan_forbidden_apple_targets);

            AppleCommitSearchResult apple_commit;
            int apple_commit_target = -1;
            int apple_commit_depth = 4;
            if (scan.found_apple_progress && scan.apple_progress_target != -1 && scan.apple_progress_dist <= 2) {
                apple_commit_target = scan.apple_progress_target;
                int current_dist = shortest_path_distance_walls_only(state, head_pos, apple_commit_target);
                if (current_dist != INT_MAX) apple_commit_depth = min(8, current_dist + 2);
            } else if (desired_goal != -1 && state.grid[desired_goal] == CELL_POWERUP) {
                int desired_goal_dist = shortest_path_distance_walls_only(state, head_pos, desired_goal);
                if (desired_goal_dist != INT_MAX && desired_goal_dist <= 3) {
                    apple_commit_target = desired_goal;
                }
            }
            if (apple_commit_target != -1) {
                apple_commit = find_safe_commit_to_apple(state, s.id, apple_commit_target, apple_commit_depth);
            }
            bool reject_failed_apple_progress_target = (!apple_commit.found
                && scan.found_apple_progress
                && scan.apple_progress_target != -1
                && apple_commit_target == scan.apple_progress_target);
            if (reject_failed_apple_progress_target && !out_of_time()) {
                unordered_set<int> alternate_forbidden_apple_targets = scan_forbidden_apple_targets;
                alternate_forbidden_apple_targets.insert(scan.apple_progress_target);
                FrontierScanResult alternate_scan = scan_reachable_frontier(state, s.id, scan_budget, desired_goal, alternate_forbidden_apple_targets);
                if (alternate_scan.found_apple || alternate_scan.found_apple_progress || alternate_scan.found_goal_progress) {
                    scan = alternate_scan;
                    reject_failed_apple_progress_target = false;
                }
            }

            bool suppress_near_unvalidated_apple_goal = false;
            if (!apple_commit.found && desired_goal != -1 && state.grid[desired_goal] == CELL_POWERUP
                && (apple_goal.target_pos != -1 || using_direct_apple_hint)) {
                int desired_goal_dist = shortest_path_distance_walls_only(state, head_pos, desired_goal);
                suppress_near_unvalidated_apple_goal = (desired_goal_dist != INT_MAX && desired_goal_dist <= 2);
                if (suppress_near_unvalidated_apple_goal) {
                    clear_apple_goal(s.id);
                    PersistentGoal center_goal = get_or_refresh_center_goal(state, s, reserved_center_targets);
                    desired_goal = center_goal.target_pos;
                }
            }

            int chosen_action = -1;
            int chosen_target = -1;
            string mode = "fallback_legal";

            bool apple_progress_contested = scan.found_apple_progress
                && scan.apple_progress_target != -1
                && is_target_contested_by_stronger_snake(state, s, scan.apple_progress_target);
            bool prefer_goal_anchor_over_distant_apple_progress = scan.found_goal_progress
                && desired_goal != -1
                && scan.goal_dist != INT_MAX
                && scan.apple_progress_dist != INT_MAX
                && scan.apple_progress_dist >= 6
                && (scan.goal_dist + 3 <= scan.apple_progress_dist);

            if (apple_commit.found) {
                chosen_action = apple_commit.action;
                chosen_target = apple_commit_target;
                clear_center_goal(s.id);
                set_apple_goal(s.id, apple_commit_target);
                mode = "reachable_apple_commit";
            } else if (scan.found_apple && scan.apple_action != -1) {
                chosen_action = scan.apple_action;
                chosen_target = scan.apple_pos;
                clear_center_goal(s.id);
                set_apple_goal(s.id, scan.apple_pos);
                mode = "reachable_apple";
            } else if (scan.found_apple_progress && scan.apple_progress_target != -1 && !apple_progress_contested
                       && !prefer_goal_anchor_over_distant_apple_progress
                      && !reject_failed_apple_progress_target
                       && !(suppress_near_unvalidated_apple_goal && scan.apple_progress_target == desired_goal)) {
                int progress_action = scan.apple_progress_action;
                if (progress_action == -1) {
                    progress_action = choose_safe_action_for_target(state, s, scan.apple_progress_target);
                }
                if (progress_action != -1) {
                    chosen_action = progress_action;
                    chosen_target = scan.apple_progress_target;
                    clear_center_goal(s.id);
                    set_apple_goal(s.id, scan.apple_progress_target);
                    mode = "apple_progress";
                }
            } else if (scan.found_goal_progress && scan.goal_action != -1 && desired_goal != -1 && !suppress_near_unvalidated_apple_goal) {
                chosen_action = scan.goal_action;
                chosen_target = desired_goal;
                if (apple_goal.target_pos != -1) {
                    mode = "apple_goal";
                } else if (using_direct_apple_hint) {
                    set_apple_goal(s.id, desired_goal);
                    mode = "direct_apple_hint";
                } else {
                    mode = "center_goal";
                }
            }

            if (chosen_action == -1) {
                chosen_target = desired_goal;
                chosen_action = choose_safe_action_for_target(state, s, chosen_target);
                if (chosen_action == -1) chosen_action = first_legal_action_basic(state, s);
                mode = (apple_goal.target_pos != -1) ? "fallback_apple_goal" : "fallback_legal";
            }

            if (chosen_target != -1) {
                if (mode == "reachable_apple" || mode == "apple_progress" || mode == "apple_goal"
                    || mode == "direct_apple_hint" || mode == "fallback_apple_goal") {
                    reserved_apple_targets.insert(chosen_target);
                } else if (mode == "center_goal") {
                    reserved_center_targets.insert(chosen_target);
                }
            }

            bool should_stabilize_choice = (mode == "fallback_apple_goal" || mode == "fallback_legal" || scan.expanded <= 2);
            if (chosen_action != -1 && (is_action_immediately_unsafe_against_stronger_snake(state, s, chosen_action) || should_stabilize_choice)) {
                int safe_action = choose_safe_action_for_target(state, s, chosen_target);
                if (safe_action != -1 && safe_action != chosen_action) {
                    chosen_action = safe_action;
                    mode += "_safe";
                }
            }

            current_my_actions[s_idx] = chosen_action;
            append_action_with_target(s_idx, chosen_action, chosen_target);

            mylog << "FRONTIER_DECISION turn=" << counter
                  << " snake=" << s.id
                  << " head_x=" << ((head_pos % max_width) - max_len)
                  << " head_y=" << ((head_pos / max_width) - max_len)
                  << " power_count=" << powerup_positions.size()
                  << " mode=" << mode
                  << " target_x=" << ((chosen_target >= 0) ? ((chosen_target % max_width) - max_len) : -999)
                  << " target_y=" << ((chosen_target >= 0) ? ((chosen_target / max_width) - max_len) : -999)
                  << " action=" << action_to_string(chosen_action)
                  << " scan_tier=" << scan_budget.tier
                  << " scan_depth=" << scan_budget.depth_limit
                  << " expansion_limit=" << scan_budget.expansion_limit
                  << " scan_budget_ms=" << scan_budget.scan_budget_ms
                  << " followthrough_budget_ms=" << scan_budget.followthrough_budget_ms
                  << " expanded=" << scan.expanded
                  << " explored_states=" << scan.explored_states
                  << " apple_depth=" << (scan.found_apple ? scan.apple_depth : -1)
                  << " apple_progress_dist=" << (scan.found_apple_progress ? scan.apple_progress_dist : -1)
                  << " goal_dist=" << (scan.found_goal_progress ? scan.goal_dist : -1)
                  << " elapsed_ms=" << elapsed_turn_ms()
                  << '\n';
        }

        string output;
        for (size_t i = 0; i < action_strs.size(); ++i) {
            output += action_strs[i];
            if (i + 1 < action_strs.size()) output += ";";
        }
        if (output.empty()) output = "WAIT";

        auto iter_elapsed = duration_cast<microseconds>(high_resolution_clock::now() - turn_start_time);
        mylog << "Turn " << counter
              << " elapsed_us=" << iter_elapsed.count()
              << " output=" << output
              << '\n';
        mylog.flush();

        cout << output << endl;
    }
}
