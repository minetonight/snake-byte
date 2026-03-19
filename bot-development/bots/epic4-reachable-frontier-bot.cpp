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
#include <string>
#include <unordered_map>
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

static constexpr const char* BOT_LOG_PREFIX = "epic4_reachable_frontier_bot_log_";
static constexpr int TURN_BUDGET_MS = 72;
static constexpr int SCAN_EXPANSION_LIMIT = 6000;
static constexpr int SCAN_DEPTH_SMALL = 18;
static constexpr int SCAN_DEPTH_LARGE = 26;
static constexpr int LONG_TERM_GOAL_TTL = 20;
static constexpr int APPLE_GOAL_TTL = 28;

ofstream make_log_stream() {
    return ofstream(string(BOT_LOG_PREFIX) + to_string(getpid()) + ".txt", ios::app);
}

ofstream mylog = make_log_stream();
auto turn_start_time = high_resolution_clock::now();

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

static bool out_of_time() {
    return elapsed_turn_ms() >= TURN_BUDGET_MS;
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
                int tail_pos = s.body[s.tail_idx];
                int new_head_idx = (s.head_idx - 1 + ring_size(s)) % ring_size(s);
                s.body[new_head_idx] = n_pos;
                s.tail_idx = (s.tail_idx - 1 + ring_size(s)) % ring_size(s);
                s.head_idx = new_head_idx;
                if (tail_pos >= 0 && tail_pos < grid_size) grid[tail_pos] = CELL_EMPTY;
            }
        };

        move_snakes(my_snakes, my_actions);
        move_snakes(opp_snakes, opp_actions);
    }

    inline void resolve_collisions() {
        vector<Snake*> all_snakes;
        for (auto& s : my_snakes) if (s.is_alive) all_snakes.push_back(&s);
        for (auto& s : opp_snakes) if (s.is_alive) all_snakes.push_back(&s);

        vector<int> head_positions(all_snakes.size());
        vector<bool> to_destroy(all_snakes.size(), false);
        vector<bool> ate_powerup(all_snakes.size(), false);
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
            else if (cell_val == CELL_POWERUP) ate_powerup[i] = true;

            for (size_t j = i + 1; j < all_snakes.size(); ++j) {
                if (head_positions[i] == head_positions[j]) {
                    to_destroy[i] = true;
                    to_destroy[j] = true;
                }
            }
        }

        for (size_t i = 0; i < all_snakes.size(); ++i) {
            Snake* s = all_snakes[i];
            if (ate_powerup[i]) {
                s->tail_idx = (s->tail_idx + 1) % ring_size(*s);
                s->length++;
            }
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

        for (size_t i = 0; i < all_snakes.size(); ++i) {
            if (ate_powerup[i]) {
                int h_pos = head_positions[i];
                if (h_pos >= 0 && h_pos < grid_size) grid[h_pos] = CELL_EMPTY;
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
        apply_gravity();
        resolve_collisions();
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

static int choose_center_anchor_cell(const GameState& state, int exclude_pos = -1) {
    int center_x = max_len + world_width / 2;
    int center_y = max_len + world_height / 2;
    int center_pos = center_y * max_width + center_x;
    int best_pos = -1;
    int best_dist = INT_MAX;
    int best_open = -1;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};

    for (int pos = 0; pos < grid_size; ++pos) {
        if (!is_goal_cell_valid(state, pos)) continue;
        if (pos == exclude_pos) continue;
        int d = manhattan_dist_pos(pos, center_pos);
        int px = pos % max_width;
        int py = pos / max_width;
        int open_neighbors = 0;
        for (int i = 0; i < 4; ++i) {
            int nx = px + dx[i];
            int ny = py + dy[i];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
            int n_pos = ny * max_width + nx;
            if (!is_playable_cell(n_pos)) continue;
            if (state.grid[n_pos] != CELL_WALL) open_neighbors++;
        }
        if (d < best_dist || (d == best_dist && open_neighbors > best_open)) {
            best_dist = d;
            best_open = open_neighbors;
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

struct SearchNode {
    GameState state;
    int first_action = -1;
    int depth = 0;
    int head_pos = -1;
};

static FrontierScanResult scan_reachable_frontier(const GameState& state, int snake_id, int depth_limit, int desired_goal_pos) {
    FrontierScanResult result;
    GameState start_state = isolate_state_for_single_snake_planning(state, snake_id);
    const Snake* start_self = start_state.find_my_snake_by_id(snake_id);
    if (start_self == nullptr || !start_self->is_alive || start_self->length <= 0) return result;

    vector<int> initial_powerups;
    initial_powerups.reserve(16);
    for (int pos = 0; pos < grid_size; ++pos) {
        if (start_state.grid[pos] == CELL_POWERUP) initial_powerups.push_back(pos);
    }

    deque<SearchNode> nodes;
    int start_head = start_self->body[start_self->head_idx];
    nodes.push_back({start_state, -1, 0, start_head});

    queue<int> q;
    q.push(0);

    unordered_map<uint64_t, int> best_depth;
    best_depth.reserve(2048);
    best_depth[encode_single_snake_plan_hash(start_state, *start_self)] = 0;
    result.explored_states = 1;

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    int start_length = start_self->length;

    while (!q.empty() && !out_of_time() && result.expanded < SCAN_EXPANSION_LIMIT) {
        int idx = q.front();
        q.pop();
        const SearchNode& node = nodes[idx];
        const Snake* cur_self = node.state.find_my_snake_by_id(snake_id);
        if (cur_self == nullptr || !cur_self->is_alive) continue;

        result.expanded++;

        if (node.first_action != -1 && !initial_powerups.empty()) {
            int best_visible_apple_dist = INT_MAX;
            int best_visible_apple_pos = -1;
            for (int p : initial_powerups) {
                if (node.state.grid[p] != CELL_POWERUP) continue;
                int d = manhattan_dist_pos(node.head_pos, p);
                if (d < best_visible_apple_dist) {
                    best_visible_apple_dist = d;
                    best_visible_apple_pos = p;
                }
            }
            if (best_visible_apple_pos != -1) {
                int followups = node.state.count_safe_followups(*cur_self);
                if (!result.found_apple_progress
                    || best_visible_apple_dist < result.apple_progress_dist
                    || (best_visible_apple_dist == result.apple_progress_dist && followups > result.apple_progress_followups)
                    || (best_visible_apple_dist == result.apple_progress_dist && followups == result.apple_progress_followups && node.depth < result.apple_progress_depth)) {
                    result.found_apple_progress = true;
                    result.apple_progress_action = node.first_action;
                    result.apple_progress_target = best_visible_apple_pos;
                    result.apple_progress_dist = best_visible_apple_dist;
                    result.apple_progress_depth = node.depth;
                    result.apple_progress_followups = followups;
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

            if (next_self->length > cur_self->length) {
                if (!result.found_apple
                    || next_depth < result.apple_depth
                    || (next_depth == result.apple_depth && followups > result.apple_followups)
                    || (next_depth == result.apple_depth && followups == result.apple_followups && goal_dist < result.apple_goal_dist)) {
                    result.found_apple = true;
                    result.apple_action = first_action;
                    result.apple_pos = next_head;
                    result.apple_depth = next_depth;
                    result.apple_followups = followups;
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

    return result;
}

static PersistentGoal get_or_refresh_center_goal(const GameState& state, const Snake& s) {
    auto it = g_center_goal_by_snake.find(s.id);
    if (it != g_center_goal_by_snake.end()
        && it->second.expires_turn > g_turn_counter
        && is_goal_cell_valid(state, it->second.target_pos)
        && it->second.target_pos != s.body[s.head_idx]) {
        return it->second;
    }

    int goal_pos = choose_center_anchor_cell(state, s.body[s.head_idx]);
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

        for (size_t s_idx = 0; s_idx < state.my_snakes.size(); ++s_idx) {
            const Snake& s = state.my_snakes[s_idx];
            if (!s.is_alive || s.length <= 0) continue;

            int head_pos = s.body[s.head_idx];
            int desired_goal = -1;
            PersistentGoal apple_goal = get_valid_apple_goal(state, s);
            if (apple_goal.target_pos != -1) {
                desired_goal = apple_goal.target_pos;
            } else {
                PersistentGoal center_goal = get_or_refresh_center_goal(state, s);
                desired_goal = center_goal.target_pos;
            }

            int scan_depth = ((world_width * world_height) >= 500) ? SCAN_DEPTH_LARGE : SCAN_DEPTH_SMALL;
            FrontierScanResult scan = scan_reachable_frontier(state, s.id, scan_depth, desired_goal);

            int chosen_action = -1;
            int chosen_target = -1;
            string mode = "fallback_legal";

            if (scan.found_apple && scan.apple_action != -1) {
                chosen_action = scan.apple_action;
                chosen_target = scan.apple_pos;
                clear_center_goal(s.id);
                set_apple_goal(s.id, scan.apple_pos);
                mode = "reachable_apple";
            } else if (scan.found_apple_progress && scan.apple_progress_action != -1) {
                chosen_action = scan.apple_progress_action;
                chosen_target = scan.apple_progress_target;
                clear_center_goal(s.id);
                set_apple_goal(s.id, scan.apple_progress_target);
                mode = "apple_progress";
            } else if (scan.found_goal_progress && scan.goal_action != -1 && desired_goal != -1) {
                chosen_action = scan.goal_action;
                chosen_target = desired_goal;
                mode = (apple_goal.target_pos != -1) ? "apple_goal" : "center_goal";
            }

            if (chosen_action == -1) {
                chosen_action = first_legal_action_basic(state, s);
                chosen_target = desired_goal;
                mode = (apple_goal.target_pos != -1) ? "fallback_apple_goal" : "fallback_legal";
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
                  << " scan_depth=" << scan_depth
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
