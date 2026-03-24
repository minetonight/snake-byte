#include <algorithm>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace std;
using namespace std::chrono;

/*
Epic 8 architecture contract.

Layer A: High-fidelity shared simulation core.
Layer B: Coarse global evaluator.
Layer C: Opponent policy abstraction.
Layer D: Diverse beam search.
Layer E: Tactical local branching.

Story 8.1 implementation scope:
- create a shared multi-snake search state that models the whole board
- provide a full shared-state hash
- provide `simulate_joint_actions()` over all snakes
- provide beam-node scaffolding that stores the full shared state

Important guardrail:
- isolated single-snake planning is not the primary planner in this file
- no main decision path is allowed to rewrite the world into a one-snake-only state

The main move chooser in this Story 8.1 file is intentionally simple.
It operates directly on the shared state and exists only to keep the file runnable
while later stories add the true adversarial beam logic.
*/

static constexpr const char* BOT_LOG_PREFIX = "epic8_team_adversarial_beam_search_bot_log_";
static constexpr int TURN_BUDGET_MS = 50;
static constexpr int TURN_RESERVE_MS = 5;

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

ofstream make_log_stream() {
    const string log_name = string(BOT_LOG_PREFIX) + to_string(getpid()) + ".txt";
    try {
        vector<std::filesystem::path> preferred_dirs;
        preferred_dirs.push_back(std::filesystem::path("../read-logs-here"));
        preferred_dirs.push_back(std::filesystem::path("../bot-development/read-logs-here"));
        preferred_dirs.push_back(std::filesystem::path("bot-development/read-logs-here"));
        preferred_dirs.push_back(std::filesystem::path("read-logs-here"));
        for (const auto& dir : preferred_dirs) {
            std::filesystem::create_directories(dir);
            ofstream out((dir / log_name).string(), ios::app);
            if (out.is_open()) return out;
        }
    } catch (...) {
    }
    return ofstream(log_name, ios::app);
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

static int time_remaining_ms() {
    return max(0, TURN_BUDGET_MS - elapsed_turn_ms());
}

static bool out_of_time_with_guard(int extra_guard_ms) {
    return elapsed_turn_ms() >= max(1, TURN_BUDGET_MS - max(TURN_RESERVE_MS, extra_guard_ms));
}

static bool out_of_time() {
    return elapsed_turn_ms() >= max(1, TURN_BUDGET_MS - TURN_RESERVE_MS);
}

struct Snake {
    int id = -1;
    int owner = -1;  // 0 = me, 1 = opponent
    int length = 0;
    int head_idx = 0;
    int tail_idx = 0;
    bool is_alive = false;
    int consumed_powerup_pos = -1;
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

static inline uint64_t hash_combine_u64(uint64_t seed, uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

struct JointActionPlan {
    vector<int> my_actions;
    vector<int> opp_actions;
};

struct SharedStateHash {
    // This hash intentionally includes:
    // - padded grid powerup layout
    // - each live/dead snake identity, owner, length, head/tail indices
    // - exact ordered body coordinates for all live snakes
    // This is a full shared-world hash for Story 8.1.
    // It does not only hash one snake, because Epic 8 decisions must preserve
    // simultaneous multi-snake futures.
    static uint64_t encode_grid_powerups(const vector<int16_t>& grid) {
        uint64_t h = 1469598103934665603ULL;
        for (int i = 0; i < grid_size; ++i) {
            if (grid[i] == CELL_POWERUP) {
                h = hash_combine_u64(h, static_cast<uint64_t>(i + 1000003));
            }
        }
        return h;
    }

    static uint64_t encode_snake(uint64_t seed, const Snake& s) {
        seed = hash_combine_u64(seed, static_cast<uint64_t>(s.id + 1));
        seed = hash_combine_u64(seed, static_cast<uint64_t>(s.owner + 1));
        seed = hash_combine_u64(seed, static_cast<uint64_t>(s.is_alive ? 1 : 0));
        seed = hash_combine_u64(seed, static_cast<uint64_t>(s.length + 1));
        seed = hash_combine_u64(seed, static_cast<uint64_t>(s.head_idx + 1));
        seed = hash_combine_u64(seed, static_cast<uint64_t>(s.tail_idx + 1));
        if (s.is_alive) {
            for (int i = 0; i < s.length; ++i) {
                int pos = s.body[(s.head_idx + i) % ring_size(s)];
                seed = hash_combine_u64(seed, static_cast<uint64_t>(pos + 10007));
            }
        }
        return seed;
    }
};

struct SharedGameState {
    vector<int16_t> grid;
    vector<Snake> my_snakes;
    vector<Snake> opp_snakes;
    int projected_my_losses = 0;
    int projected_opp_losses = 0;

    const Snake* find_my_snake_by_id(int snake_id) const {
        for (const auto& s : my_snakes) if (s.id == snake_id) return &s;
        return nullptr;
    }

    const Snake* find_opp_snake_by_id(int snake_id) const {
        for (const auto& s : opp_snakes) if (s.id == snake_id) return &s;
        return nullptr;
    }

    int total_team_length(int owner) const {
        int total = 0;
        const vector<Snake>& snakes = (owner == 0) ? my_snakes : opp_snakes;
        for (const auto& s : snakes) {
            if (s.is_alive) total += s.length;
        }
        return total;
    }

    uint64_t full_state_hash() const {
        uint64_t h = SharedStateHash::encode_grid_powerups(grid);
        h = hash_combine_u64(h, static_cast<uint64_t>(my_snakes.size()));
        h = hash_combine_u64(h, static_cast<uint64_t>(opp_snakes.size()));
        h = hash_combine_u64(h, static_cast<uint64_t>(projected_my_losses + 1009));
        h = hash_combine_u64(h, static_cast<uint64_t>(projected_opp_losses + 2003));
        for (const auto& s : my_snakes) h = SharedStateHash::encode_snake(h, s);
        for (const auto& s : opp_snakes) h = SharedStateHash::encode_snake(h, s);
        return h;
    }

    int live_snake_count(int owner) const {
        int total = 0;
        const vector<Snake>& snakes = (owner == 0) ? my_snakes : opp_snakes;
        for (const auto& s : snakes) if (s.is_alive) total++;
        return total;
    }

    bool is_cell_grounded(int pos, const vector<bool>& grounded_snakes) const {
        int below_idx = pos + max_width;
        if (below_idx >= grid_size) return true;
        int16_t below_cell = grid[below_idx];
        if (below_cell == CELL_WALL || below_cell == CELL_POWERUP) return true;
        if (below_cell >= CELL_SNAKE_BASE) {
            int snake_id = below_cell - CELL_SNAKE_BASE;
            if (snake_id >= 0 && snake_id < static_cast<int>(grounded_snakes.size()) && grounded_snakes[snake_id]) {
                return true;
            }
        }
        return false;
    }

    void apply_gravity() {
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
                    bool grounded_now = false;
                    for (int i = 0; i < s->length; ++i) {
                        int pos = s->body[(s->head_idx + i) % ring_size(*s)];
                        if (is_cell_grounded(pos, grounded)) {
                            grounded_now = true;
                            break;
                        }
                    }
                    if (grounded_now) {
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
                for (int i = 1; i < s->length; ++i) {
                    int pos = s->body[(s->head_idx + i) % ring_size(*s)];
                    if (pos >= 0 && pos < grid_size) grid[pos] = CELL_SNAKE_BASE + s->id;
                }
            }
        }
    }

    void apply_movement(const JointActionPlan& plan) {
        auto move_snake_group = [&](vector<Snake>& snakes, const vector<int>& actions) {
            for (size_t i = 0; i < snakes.size(); ++i) {
                Snake& s = snakes[i];
                if (!s.is_alive) continue;
                s.consumed_powerup_pos = -1;
                int action = (i < actions.size()) ? actions[i] : infer_previous_action(s);
                if (action == 4) action = infer_previous_action(s);

                int h_pos = s.body[s.head_idx];
                int hx = h_pos % max_width;
                int hy = h_pos / max_width;
                int nx = hx;
                int ny = hy;
                if (action == 0) ny -= 1;
                else if (action == 1) ny += 1;
                else if (action == 2) nx -= 1;
                else if (action == 3) nx += 1;

                int n_pos = ny * max_width + nx;
                bool will_eat_powerup = n_pos >= 0 && n_pos < grid_size && grid[n_pos] == CELL_POWERUP;
                int tail_pos = s.body[s.tail_idx];
                int new_head_idx = (s.head_idx - 1 + ring_size(s)) % ring_size(s);
                s.body[new_head_idx] = n_pos;
                if (!will_eat_powerup) {
                    s.tail_idx = (s.tail_idx - 1 + ring_size(s)) % ring_size(s);
                } else {
                    s.length++;
                    s.consumed_powerup_pos = n_pos;
                }
                s.head_idx = new_head_idx;
                if (!will_eat_powerup && tail_pos >= 0 && tail_pos < grid_size) grid[tail_pos] = CELL_EMPTY;
            }
        };

        move_snake_group(my_snakes, plan.my_actions);
        move_snake_group(opp_snakes, plan.opp_actions);
    }

    void apply_eats() {
        auto consume_heads = [&](const vector<Snake>& snakes) {
            for (const Snake& s : snakes) {
                if (!s.is_alive) continue;
                int head_pos = s.body[s.head_idx];
                if (head_pos >= 0 && head_pos < grid_size && grid[head_pos] == CELL_POWERUP) {
                    grid[head_pos] = CELL_EMPTY;
                }
            }
        };
        consume_heads(my_snakes);
        consume_heads(opp_snakes);
    }

    void resolve_collisions() {
        vector<Snake*> all_snakes;
        for (auto& s : my_snakes) if (s.is_alive) all_snakes.push_back(&s);
        for (auto& s : opp_snakes) if (s.is_alive) all_snakes.push_back(&s);

        vector<int> head_positions(all_snakes.size(), -1);
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
            if (!to_destroy[i]) continue;
            int prior_length = s->length;
            if (prior_length <= 3) {
                s->is_alive = false;
                if (s->owner == 0) projected_my_losses += max(0, prior_length);
                else projected_opp_losses += max(0, prior_length);
                for (int k = 0; k < max(0, prior_length); ++k) {
                    int idx = (s->head_idx + k) % ring_size(*s);
                    int pos = s->body[idx];
                    if (pos >= 0 && pos < grid_size) grid[pos] = CELL_EMPTY;
                }
                s->length = 0;
            } else {
                if (s->owner == 0) projected_my_losses += 1;
                else projected_opp_losses += 1;
                s->length--;
                s->head_idx = (s->head_idx + 1) % ring_size(*s);
            }
        }

        for (Snake* s : all_snakes) {
            if (!s->is_alive) continue;
            for (int i = 0; i < s->length; ++i) {
                int pos = s->body[(s->head_idx + i) % ring_size(*s)];
                if (pos >= 0 && pos < grid_size) grid[pos] = CELL_SNAKE_BASE + s->id;
            }
        }
    }

    void simulate_joint_actions(const JointActionPlan& plan) {
        apply_movement(plan);
        apply_eats();
        resolve_collisions();
        apply_gravity();
    }
};

struct RootActionFamily {
    enum Type {
        DEFAULT_CONTINUE = 0,
        LEGALIZED_CONTINUE = 1,
        PLACEHOLDER_SHARED_ROOT = 2,
        SURVIVAL_ROOT = 3,
        GROWTH_ROOT = 4,
        SPACE_ROOT = 5,
        CONTEST_ROOT = 6,
        ATTACK_ROOT = 7,
        ESCAPE_ROOT = 8,
        SPLIT_HARVEST = 9,
        ANCHOR_AND_FORAGE = 10,
        SAFE_SPREAD = 11,
        CENTER_PRESSURE = 12,
        CONTEST_LEFT_CLUSTER = 13,
        CONTEST_RIGHT_CLUSTER = 14,
        DEFENSIVE_RECOVERY = 15,
    };
};

struct FriendlyRootCandidate {
    JointActionPlan plan;
    RootActionFamily::Type family = RootActionFamily::PLACEHOLDER_SHARED_ROOT;
};

enum class BeamBucket {
    SURVIVAL = 0,
    GROWTH = 1,
    SPACE = 2,
    CONTEST = 3,
    ATTACK_OR_PRESSURE = 4,
    ESCAPE_OR_RECOVERY = 5,
};

struct BeamNode {
    SharedGameState state;
    uint64_t state_hash = 0;
    uint64_t novelty_signature = 0;
    uint64_t root_signature = 0;
    int depth = 0;
    int scalar_score = INT_MIN;
    RootActionFamily::Type root_family = RootActionFamily::PLACEHOLDER_SHARED_ROOT;
    BeamBucket bucket = BeamBucket::SURVIVAL;
    bool is_hotspot = false;
    int hotspot_score = 0;
    int exact_merge_count = 0;
    int novelty_population = 0;
    bool tactical_extension = false;
    string hotspot_reasons;
    string retention_reason;
    string novelty_label;
    string tactical_summary;
    vector<int> root_actions;
};

static bool is_action_locally_legal(const SharedGameState& state, const Snake& s, int action);
static int first_legal_action_shared(const SharedGameState& state, const Snake& s);
static vector<int> legal_actions_for_snake(const SharedGameState& state, const Snake& s);
static int manhattan_dist_pos(int p1, int p2);
static int nearest_powerup_distance(const SharedGameState& state, int from_pos, int* target_pos = nullptr);
static int nearest_enemy_head_distance(const SharedGameState& state, int from_pos, int* target_pos = nullptr);
static int next_position_for_action(const Snake& s, int action);
static int choose_action_toward_target(const SharedGameState& state, const Snake& s, int target_pos);
static int choose_safe_spread_action(const SharedGameState& state, const Snake& s);
static int choose_escape_action(const SharedGameState& state, const Snake& s);
static int choose_pressure_action(const SharedGameState& state, const Snake& s);
static vector<int> infer_default_actions(const vector<Snake>& snakes);
static JointActionPlan choose_story81_baseline_plan(const SharedGameState& state);
static vector<FriendlyRootCandidate> generate_team_root_candidates(const SharedGameState& state, size_t cap);
static vector<JointActionPlan> generate_structured_followup_candidates(const SharedGameState& state, size_t cap);
static vector<JointActionPlan> generate_story82_root_candidates(const SharedGameState& state, size_t cap = 32);

static const char* beam_bucket_to_string(BeamBucket bucket) {
    if (bucket == BeamBucket::SURVIVAL) return "SURVIVAL";
    if (bucket == BeamBucket::GROWTH) return "GROWTH";
    if (bucket == BeamBucket::SPACE) return "SPACE";
    if (bucket == BeamBucket::CONTEST) return "CONTEST";
    if (bucket == BeamBucket::ATTACK_OR_PRESSURE) return "ATTACK_OR_PRESSURE";
    if (bucket == BeamBucket::ESCAPE_OR_RECOVERY) return "ESCAPE_OR_RECOVERY";
    return "SURVIVAL";
}

static const char* root_family_to_string(RootActionFamily::Type family) {
    if (family == RootActionFamily::DEFAULT_CONTINUE) return "DEFAULT_CONTINUE";
    if (family == RootActionFamily::LEGALIZED_CONTINUE) return "LEGALIZED_CONTINUE";
    if (family == RootActionFamily::PLACEHOLDER_SHARED_ROOT) return "PLACEHOLDER_SHARED_ROOT";
    if (family == RootActionFamily::SURVIVAL_ROOT) return "SURVIVAL_ROOT";
    if (family == RootActionFamily::GROWTH_ROOT) return "GROWTH_ROOT";
    if (family == RootActionFamily::SPACE_ROOT) return "SPACE_ROOT";
    if (family == RootActionFamily::CONTEST_ROOT) return "CONTEST_ROOT";
    if (family == RootActionFamily::ATTACK_ROOT) return "ATTACK_ROOT";
    if (family == RootActionFamily::ESCAPE_ROOT) return "ESCAPE_ROOT";
    if (family == RootActionFamily::SPLIT_HARVEST) return "SPLIT_HARVEST";
    if (family == RootActionFamily::ANCHOR_AND_FORAGE) return "ANCHOR_AND_FORAGE";
    if (family == RootActionFamily::SAFE_SPREAD) return "SAFE_SPREAD";
    if (family == RootActionFamily::CENTER_PRESSURE) return "CENTER_PRESSURE";
    if (family == RootActionFamily::CONTEST_LEFT_CLUSTER) return "CONTEST_LEFT_CLUSTER";
    if (family == RootActionFamily::CONTEST_RIGHT_CLUSTER) return "CONTEST_RIGHT_CLUSTER";
    if (family == RootActionFamily::DEFENSIVE_RECOVERY) return "DEFENSIVE_RECOVERY";
    return "PLACEHOLDER_SHARED_ROOT";
}

static uint64_t encode_actions_signature(const vector<int>& actions) {
    uint64_t h = 1099511628211ULL;
    for (size_t i = 0; i < actions.size(); ++i) {
        h = hash_combine_u64(h, static_cast<uint64_t>(i + 17));
        h = hash_combine_u64(h, static_cast<uint64_t>(actions[i] + 97));
    }
    return h;
}

static int clamp_band(int value, int divisor, int max_band) {
    if (divisor <= 0) return 0;
    int band = value / divisor;
    if (band < 0) band = 0;
    if (band > max_band) band = max_band;
    return band;
}

static string actions_to_string(const vector<int>& actions) {
    string out;
    for (size_t i = 0; i < actions.size(); ++i) {
        if (i) out += ",";
        out += action_to_string(actions[i]);
    }
    return out;
}

enum class EnemyReplyPolicy {
    DefaultContinue,
    SafeSpace,
    AppleRace,
    HeadPressure,
};

struct EnemyReplyChoice {
    int action = 0;
    EnemyReplyPolicy policy = EnemyReplyPolicy::DefaultContinue;
};

struct OpponentReplyPlan {
    vector<int> opp_actions;
    vector<EnemyReplyPolicy> opp_policies;
    int hot_enemy_count = 0;
};

struct HotspotSummary {
    bool is_hotspot = false;
    bool head_collision_risk = false;
    bool contested_apple_race = false;
    bool corridor_pressure = false;
    bool constrained_growth = false;
    bool beheading_swing = false;
    int score = 0;
    int hot_enemy_count = 0;
    string reasons;
};

struct TacticalBranchSummary {
    bool activated = false;
    vector<int> my_indices;
    vector<int> opp_indices;
    string summary;
};

static const char* enemy_reply_policy_to_string(EnemyReplyPolicy policy);
static vector<OpponentReplyPlan> generate_opponent_reply_plans(const SharedGameState& state, const HotspotSummary& hotspot, size_t cap = 12);
static TacticalBranchSummary analyze_tactical_branch(const SharedGameState& state, const HotspotSummary& hotspot);
static vector<JointActionPlan> generate_tactical_followup_candidates(const SharedGameState& state, const TacticalBranchSummary& tactical, size_t cap);
static vector<OpponentReplyPlan> generate_tactical_opponent_reply_plans(const SharedGameState& state, const HotspotSummary& hotspot, const TacticalBranchSummary& tactical, size_t cap);

struct TerritoryAnalysis {
    vector<int> my_dist;
    vector<int> opp_dist;
    int my_territory = 0;
    int opp_territory = 0;
    int contested_territory = 0;
    int my_owned_apples = 0;
    int opp_owned_apples = 0;
    int contested_apples = 0;
};

struct EvaluatorBreakdown {
    int terminal_score = 0;
    int length_delta = 0;
    int loss_risk_penalty = 0;
    int mobility_delta = 0;
    int territory_delta = 0;
    int apple_ownership_delta = 0;
    int head_danger_penalty = 0;
    int corridor_trap_penalty = 0;
    int coordination_penalty = 0;
    int total_score = 0;
};

struct TacticalLexicoScore {
    int my_live = 0;
    int opp_live = 0;
    int my_min_length = -1;
    int my_total_length = 0;
    int opp_total_length = 0;
    int future_growth_count = 0;
    int mobility = 0;
    int territory_delta = 0;
    int apple_delta = 0;
    int positional_quality = 0;
    int scalar_fallback = INT_MIN;
};

struct BeamSearchResult {
    JointActionPlan chosen_plan;
    SharedGameState best_next_state;
    EvaluatorBreakdown best_eval;
    RootActionFamily::Type best_family = RootActionFamily::PLACEHOLDER_SHARED_ROOT;
    BeamBucket best_bucket = BeamBucket::SURVIVAL;
    OpponentReplyPlan best_reply;
    HotspotSummary best_hotspot;
    vector<BeamNode> final_frontier;
    int completed_depth = 0;
    int expanded_nodes = 0;
    string winner_retention_reason;
    string winner_over_runner_up_reason;
    bool diversity_preserved_winner = false;
    string diagnostics;
};

struct SearchTuning {
    size_t root_candidate_cap = 12;
    size_t root_beam_cap = 10;
    size_t frontier_beam_cap = 8;
    size_t reply_cap = 10;
    size_t tactical_root_cap = 8;
    size_t followup_cap = 4;
    int max_depth = 2;
};

static bool is_analysis_traversable(int16_t cell) {
    return cell == CELL_EMPTY || cell == CELL_POWERUP;
}

static vector<int> multi_source_bfs_distances(const SharedGameState& state, const vector<Snake>& snakes) {
    vector<int> dist(grid_size, -1);
    deque<int> q;
    for (const Snake& s : snakes) {
        if (!s.is_alive || s.length <= 0) continue;
        int head = s.body[s.head_idx];
        if (head < 0 || head >= grid_size) continue;
        if (dist[head] == -1) {
            dist[head] = 0;
            q.push_back(head);
        }
    }

    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    while (!q.empty()) {
        int pos = q.front();
        q.pop_front();
        int x = pos % max_width;
        int y = pos / max_width;
        for (int i = 0; i < 4; ++i) {
            int nx = x + dx[i];
            int ny = y + dy[i];
            if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
            int n_pos = ny * max_width + nx;
            if (dist[n_pos] != -1) continue;
            if (!is_analysis_traversable(state.grid[n_pos])) continue;
            dist[n_pos] = dist[pos] + 1;
            q.push_back(n_pos);
        }
    }
    return dist;
}

static TerritoryAnalysis analyze_territory_and_apples(const SharedGameState& state) {
    TerritoryAnalysis analysis;
    analysis.my_dist = multi_source_bfs_distances(state, state.my_snakes);
    analysis.opp_dist = multi_source_bfs_distances(state, state.opp_snakes);

    for (int pos = 0; pos < grid_size; ++pos) {
        if (!is_analysis_traversable(state.grid[pos])) continue;
        int md = analysis.my_dist[pos];
        int od = analysis.opp_dist[pos];
        bool is_apple = state.grid[pos] == CELL_POWERUP;

        if (md == -1 && od == -1) continue;
        if (od == -1 || (md != -1 && md < od)) {
            analysis.my_territory++;
            if (is_apple) analysis.my_owned_apples++;
        } else if (md == -1 || od < md) {
            analysis.opp_territory++;
            if (is_apple) analysis.opp_owned_apples++;
        } else {
            analysis.contested_territory++;
            if (is_apple) analysis.contested_apples++;
        }
    }

    return analysis;
}

static int count_remaining_powerups(const SharedGameState& state) {
    int count = 0;
    for (int i = 0; i < grid_size; ++i) {
        if (state.grid[i] == CELL_POWERUP) count++;
    }
    return count;
}

static int count_legal_actions_for_snake(const SharedGameState& state, const Snake& s) {
    int count = 0;
    for (int action = 0; action < 4; ++action) {
        if (is_action_locally_legal(state, s, action)) count++;
    }
    return count;
}

static int count_team_mobility(const SharedGameState& state, int owner) {
    int total = 0;
    const vector<Snake>& snakes = (owner == 0) ? state.my_snakes : state.opp_snakes;
    for (const Snake& s : snakes) {
        if (!s.is_alive || s.length <= 0) continue;
        total += count_legal_actions_for_snake(state, s);
    }
    return total;
}

static int estimate_head_danger_penalty(const SharedGameState& state) {
    int penalty = 0;
    for (const Snake& mine : state.my_snakes) {
        if (!mine.is_alive || mine.length <= 0) continue;
        int my_head = mine.body[mine.head_idx];
        int mx = my_head % max_width;
        int my = my_head / max_width;
        for (const Snake& opp : state.opp_snakes) {
            if (!opp.is_alive || opp.length <= 0) continue;
            if (opp.length < mine.length) continue;
            int opp_head = opp.body[opp.head_idx];
            int ox = opp_head % max_width;
            int oy = opp_head / max_width;
            if (abs(mx - ox) + abs(my - oy) <= 2) penalty += 35;
        }
    }
    return penalty;
}

static int estimate_corridor_trap_penalty(const SharedGameState& state) {
    int penalty = 0;
    for (const Snake& mine : state.my_snakes) {
        if (!mine.is_alive || mine.length <= 0) continue;
        int legal = count_legal_actions_for_snake(state, mine);
        if (legal <= 1) penalty += 80;
        else if (legal == 2) penalty += 20;
    }
    for (const Snake& opp : state.opp_snakes) {
        if (!opp.is_alive || opp.length <= 0) continue;
        int legal = count_legal_actions_for_snake(state, opp);
        if (legal <= 1) penalty -= 60;
        else if (legal == 2) penalty -= 10;
    }
    return penalty;
}

static int estimate_coordination_penalty(const SharedGameState& state) {
    int penalty = 0;
    for (size_t i = 0; i < state.my_snakes.size(); ++i) {
        const Snake& a = state.my_snakes[i];
        if (!a.is_alive || a.length <= 0) continue;
        int ah = a.body[a.head_idx];
        for (size_t j = i + 1; j < state.my_snakes.size(); ++j) {
            const Snake& b = state.my_snakes[j];
            if (!b.is_alive || b.length <= 0) continue;
            int bh = b.body[b.head_idx];
            int d = abs((ah % max_width) - (bh % max_width)) + abs((ah / max_width) - (bh / max_width));
            if (d <= 1) penalty += 50;
            else if (d == 2) penalty += 12;
        }
    }
    return penalty;
}

static HotspotSummary analyze_hotspots(const SharedGameState& state) {
    HotspotSummary summary;

    int remaining_powerups = count_remaining_powerups(state);

    for (const Snake& mine : state.my_snakes) {
        if (!mine.is_alive || mine.length <= 0) continue;
        int my_head = mine.body[mine.head_idx];

        int my_legal = count_legal_actions_for_snake(state, mine);
        if (my_legal <= 1) {
            summary.corridor_pressure = true;
            summary.score += 3;
        }

        if (mine.consumed_powerup_pos >= 0 && my_legal <= 2) {
            summary.constrained_growth = true;
            summary.score += 3;
        }

        for (const Snake& opp : state.opp_snakes) {
            if (!opp.is_alive || opp.length <= 0) continue;
            int opp_head = opp.body[opp.head_idx];
            int d = manhattan_dist_pos(my_head, opp_head);
            if (d <= 2) {
                summary.head_collision_risk = true;
                summary.score += 4;
            }
            if (d <= 6) summary.hot_enemy_count++;

            if (remaining_powerups > 0) {
                int my_apple = nearest_powerup_distance(state, my_head);
                int opp_apple = nearest_powerup_distance(state, opp_head);
                if (my_apple != INT_MAX && opp_apple != INT_MAX && abs(my_apple - opp_apple) <= 1 && min(my_apple, opp_apple) <= 4) {
                    summary.contested_apple_race = true;
                    summary.score += 2;
                }
            }

            if (opp.length >= mine.length && d <= 2) {
                summary.beheading_swing = true;
                summary.score += 2;
            }
        }
    }

    if (summary.head_collision_risk) summary.reasons += "HEAD_COLLISION|";
    if (summary.contested_apple_race) summary.reasons += "CONTESTED_APPLE|";
    if (summary.corridor_pressure) summary.reasons += "CORRIDOR|";
    if (summary.constrained_growth) summary.reasons += "CONSTRAINED_GROWTH|";
    if (summary.beheading_swing) summary.reasons += "BEHEADING_SWING|";
    if (!summary.reasons.empty()) summary.reasons.pop_back();

    summary.is_hotspot = summary.score >= 4;
    return summary;
}

static EvaluatorBreakdown evaluate_shared_state(const SharedGameState& state) {
    EvaluatorBreakdown eval;

    int my_live = state.live_snake_count(0);
    int opp_live = state.live_snake_count(1);
    int my_len = state.total_team_length(0);
    int opp_len = state.total_team_length(1);

    bool no_powerups = true;
    for (int i = 0; i < grid_size; ++i) {
        if (state.grid[i] == CELL_POWERUP) {
            no_powerups = false;
            break;
        }
    }

    if (my_live == 0 && opp_live > 0) eval.terminal_score = -1000000;
    else if (opp_live == 0 && my_live > 0) eval.terminal_score = 1000000;
    else if (no_powerups) {
        if (my_len != opp_len) eval.terminal_score = (my_len > opp_len) ? 250000 : -250000;
        else {
            int loss_delta = state.projected_opp_losses - state.projected_my_losses;
            eval.terminal_score = loss_delta * 2000;
        }
    }

    eval.length_delta = (my_len - opp_len) * 30;
    eval.loss_risk_penalty = (state.projected_opp_losses - state.projected_my_losses) * 45;

    TerritoryAnalysis territory = analyze_territory_and_apples(state);
    eval.territory_delta = (territory.my_territory - territory.opp_territory) * 2 + territory.contested_territory / 4;
    eval.apple_ownership_delta = (territory.my_owned_apples - territory.opp_owned_apples) * 70 + territory.contested_apples * 8;

    int my_mobility = count_team_mobility(state, 0);
    int opp_mobility = count_team_mobility(state, 1);
    eval.mobility_delta = (my_mobility - opp_mobility) * 18;

    eval.head_danger_penalty = -estimate_head_danger_penalty(state);
    eval.corridor_trap_penalty = -estimate_corridor_trap_penalty(state);
    eval.coordination_penalty = -estimate_coordination_penalty(state);

    eval.total_score = eval.terminal_score
        + eval.length_delta
        + eval.loss_risk_penalty
        + eval.mobility_delta
        + eval.territory_delta
        + eval.apple_ownership_delta
        + eval.head_danger_penalty
        + eval.corridor_trap_penalty
        + eval.coordination_penalty;

    return eval;
}

static bool is_hopeless_eval(const EvaluatorBreakdown& eval) {
    return eval.terminal_score <= -1000000 || eval.total_score <= -300000;
}

static int count_future_growth_potential(const SharedGameState& state) {
    int count = 0;
    for (const Snake& mine : state.my_snakes) {
        if (!mine.is_alive || mine.length <= 0) continue;
        int head = mine.body[mine.head_idx];
        int nearest = nearest_powerup_distance(state, head);
        if (nearest <= 4) count++;
    }
    return count;
}

static int min_friendly_length(const SharedGameState& state) {
    int best = INT_MAX;
    for (const Snake& mine : state.my_snakes) {
        if (!mine.is_alive || mine.length <= 0) continue;
        best = min(best, mine.length);
    }
    return (best == INT_MAX) ? -1 : best;
}

/*
Lexicographic tactical comparison order for Story 8.9:
1. keep more friendly snakes alive while eliminating enemy snakes when possible
2. avoid catastrophic friendly shrinkage via minimum and total friendly length
3. preserve future growth potential
4. preserve followups / mobility
5. improve territory and apple ownership
6. break remaining ties with positional quality and scalar fallback
*/
static TacticalLexicoScore build_tactical_lexico_score(const SharedGameState& state, const EvaluatorBreakdown& eval) {
    TacticalLexicoScore score;
    score.my_live = state.live_snake_count(0);
    score.opp_live = state.live_snake_count(1);
    score.my_min_length = min_friendly_length(state);
    score.my_total_length = state.total_team_length(0);
    score.opp_total_length = state.total_team_length(1);
    score.future_growth_count = count_future_growth_potential(state);
    score.mobility = count_team_mobility(state, 0);
    score.territory_delta = eval.territory_delta;
    score.apple_delta = eval.apple_ownership_delta;
    score.positional_quality = eval.head_danger_penalty + eval.corridor_trap_penalty + eval.coordination_penalty;
    score.scalar_fallback = eval.total_score;
    return score;
}

static bool is_better_tactical_lexico_score(const TacticalLexicoScore& candidate, const TacticalLexicoScore& incumbent) {
    if (candidate.my_live != incumbent.my_live) return candidate.my_live > incumbent.my_live;
    if (candidate.opp_live != incumbent.opp_live) return candidate.opp_live < incumbent.opp_live;
    if (candidate.my_min_length != incumbent.my_min_length) return candidate.my_min_length > incumbent.my_min_length;
    if (candidate.my_total_length != incumbent.my_total_length) return candidate.my_total_length > incumbent.my_total_length;
    if (candidate.opp_total_length != incumbent.opp_total_length) return candidate.opp_total_length < incumbent.opp_total_length;
    if (candidate.future_growth_count != incumbent.future_growth_count) return candidate.future_growth_count > incumbent.future_growth_count;
    if (candidate.mobility != incumbent.mobility) return candidate.mobility > incumbent.mobility;
    if (candidate.territory_delta != incumbent.territory_delta) return candidate.territory_delta > incumbent.territory_delta;
    if (candidate.apple_delta != incumbent.apple_delta) return candidate.apple_delta > incumbent.apple_delta;
    if (candidate.positional_quality != incumbent.positional_quality) return candidate.positional_quality > incumbent.positional_quality;
    if (candidate.scalar_fallback != incumbent.scalar_fallback) return candidate.scalar_fallback > incumbent.scalar_fallback;
    return false;
}

static bool is_better_beam_node(const BeamNode& candidate, const BeamNode& incumbent) {
    if (candidate.tactical_extension && incumbent.tactical_extension) {
        TacticalLexicoScore ta = build_tactical_lexico_score(candidate.state, evaluate_shared_state(candidate.state));
        TacticalLexicoScore tb = build_tactical_lexico_score(incumbent.state, evaluate_shared_state(incumbent.state));
        if (is_better_tactical_lexico_score(ta, tb)) return true;
        if (is_better_tactical_lexico_score(tb, ta)) return false;
    }
    if (candidate.scalar_score != incumbent.scalar_score) return candidate.scalar_score > incumbent.scalar_score;
    return candidate.state_hash < incumbent.state_hash;
}

static string build_reply_policy_string(const OpponentReplyPlan& reply) {
    if (reply.opp_policies.empty()) return "NONE";
    string out;
    for (size_t i = 0; i < reply.opp_policies.size(); ++i) {
        if (i) out += ",";
        out += enemy_reply_policy_to_string(reply.opp_policies[i]);
    }
    return out;
}

static string build_eval_breakdown_string(const EvaluatorBreakdown& eval) {
    return "eval{term=" + to_string(eval.terminal_score)
        + ",len=" + to_string(eval.length_delta)
        + ",loss=" + to_string(eval.loss_risk_penalty)
        + ",terr=" + to_string(eval.territory_delta)
        + ",apple=" + to_string(eval.apple_ownership_delta)
        + ",mob=" + to_string(eval.mobility_delta)
        + ",danger=" + to_string(eval.head_danger_penalty)
        + ",runway=" + to_string(eval.corridor_trap_penalty)
        + ",coord=" + to_string(eval.coordination_penalty)
        + ",total=" + to_string(eval.total_score) + "}";
}

static bool winner_preserved_by_diversity(const BeamNode& node) {
    return node.retention_reason == "novelty_seed"
        || node.retention_reason == "root_quota"
        || node.retention_reason == "bucket_quota"
        || node.retention_reason == "family_quota";
}

static string build_tactical_winner_reason(const TacticalLexicoScore& winner, const TacticalLexicoScore& runner_up) {
    if (winner.my_live != runner_up.my_live) return "tactical_my_live(" + to_string(winner.my_live) + ">" + to_string(runner_up.my_live) + ")";
    if (winner.opp_live != runner_up.opp_live) return "tactical_opp_live(" + to_string(winner.opp_live) + "<" + to_string(runner_up.opp_live) + ")";
    if (winner.my_min_length != runner_up.my_min_length) return "tactical_my_min_len(" + to_string(winner.my_min_length) + ">" + to_string(runner_up.my_min_length) + ")";
    if (winner.my_total_length != runner_up.my_total_length) return "tactical_my_total_len(" + to_string(winner.my_total_length) + ">" + to_string(runner_up.my_total_length) + ")";
    if (winner.opp_total_length != runner_up.opp_total_length) return "tactical_opp_total_len(" + to_string(winner.opp_total_length) + "<" + to_string(runner_up.opp_total_length) + ")";
    if (winner.future_growth_count != runner_up.future_growth_count) return "tactical_growth(" + to_string(winner.future_growth_count) + ">" + to_string(runner_up.future_growth_count) + ")";
    if (winner.mobility != runner_up.mobility) return "tactical_mobility(" + to_string(winner.mobility) + ">" + to_string(runner_up.mobility) + ")";
    if (winner.territory_delta != runner_up.territory_delta) return "tactical_territory(" + to_string(winner.territory_delta) + ">" + to_string(runner_up.territory_delta) + ")";
    if (winner.apple_delta != runner_up.apple_delta) return "tactical_apple(" + to_string(winner.apple_delta) + ">" + to_string(runner_up.apple_delta) + ")";
    if (winner.positional_quality != runner_up.positional_quality) return "tactical_position(" + to_string(winner.positional_quality) + ">" + to_string(runner_up.positional_quality) + ")";
    if (winner.scalar_fallback != runner_up.scalar_fallback) return "tactical_scalar(" + to_string(winner.scalar_fallback) + ">" + to_string(runner_up.scalar_fallback) + ")";
    return "tactical_hash_tiebreak";
}

static string build_scalar_winner_reason(const EvaluatorBreakdown& winner, const EvaluatorBreakdown& runner_up,
                                         uint64_t winner_hash, uint64_t runner_hash) {
    vector<pair<string, int>> deltas = {
        {"term", winner.terminal_score - runner_up.terminal_score},
        {"len", winner.length_delta - runner_up.length_delta},
        {"loss", winner.loss_risk_penalty - runner_up.loss_risk_penalty},
        {"terr", winner.territory_delta - runner_up.territory_delta},
        {"apple", winner.apple_ownership_delta - runner_up.apple_ownership_delta},
        {"mob", winner.mobility_delta - runner_up.mobility_delta},
        {"danger", winner.head_danger_penalty - runner_up.head_danger_penalty},
        {"runway", winner.corridor_trap_penalty - runner_up.corridor_trap_penalty},
        {"coord", winner.coordination_penalty - runner_up.coordination_penalty},
    };
    auto best_it = max_element(deltas.begin(), deltas.end(), [](const pair<string, int>& a, const pair<string, int>& b) {
        return abs(a.second) < abs(b.second);
    });
    if (winner.total_score != runner_up.total_score) {
        return "score_delta(" + to_string(winner.total_score - runner_up.total_score) + ")/"
            + best_it->first + "(" + to_string(best_it->second) + ")";
    }
    return "score_tied/hash(" + to_string(winner_hash) + "<" + to_string(runner_hash) + ")";
}

static string build_winner_over_runner_up_reason(const BeamNode& winner, const BeamNode* runner_up) {
    if (runner_up == nullptr) return "only_frontier_candidate";
    if (winner.tactical_extension && runner_up->tactical_extension) {
        TacticalLexicoScore winner_score = build_tactical_lexico_score(winner.state, evaluate_shared_state(winner.state));
        TacticalLexicoScore runner_score = build_tactical_lexico_score(runner_up->state, evaluate_shared_state(runner_up->state));
        return build_tactical_winner_reason(winner_score, runner_score);
    }
    EvaluatorBreakdown winner_eval = evaluate_shared_state(winner.state);
    EvaluatorBreakdown runner_eval = evaluate_shared_state(runner_up->state);
    return build_scalar_winner_reason(winner_eval, runner_eval, winner.state_hash, runner_up->state_hash);
}

static BeamBucket classify_beam_bucket(const SharedGameState& state, const EvaluatorBreakdown& eval,
                                       const HotspotSummary& hotspot) {
    if (eval.terminal_score <= -250000 || hotspot.corridor_pressure || eval.mobility_delta <= -36) {
        return BeamBucket::SURVIVAL;
    }
    if (hotspot.contested_apple_race) return BeamBucket::CONTEST;
    if (hotspot.beheading_swing || (hotspot.head_collision_risk && eval.length_delta >= 0 && eval.total_score >= 0)) {
        return BeamBucket::ATTACK_OR_PRESSURE;
    }
    if (eval.apple_ownership_delta >= 70) return BeamBucket::GROWTH;
    if (eval.territory_delta >= 30) return BeamBucket::SPACE;
    if (eval.length_delta < 0 || eval.loss_risk_penalty < 0 || hotspot.constrained_growth) {
        return BeamBucket::ESCAPE_OR_RECOVERY;
    }
    if (state.live_snake_count(0) < state.live_snake_count(1)) return BeamBucket::ESCAPE_OR_RECOVERY;
    return BeamBucket::SPACE;
}

static RootActionFamily::Type infer_root_family(const SharedGameState& state, const vector<int>& root_actions,
                                                BeamBucket bucket) {
    vector<int> defaults = infer_default_actions(state.my_snakes);
    bool exact_default = root_actions.size() == defaults.size();
    for (size_t i = 0; exact_default && i < root_actions.size(); ++i) {
        if (root_actions[i] != defaults[i]) exact_default = false;
    }
    if (exact_default) return RootActionFamily::DEFAULT_CONTINUE;

    bool all_legal = root_actions.size() == state.my_snakes.size();
    for (size_t i = 0; all_legal && i < state.my_snakes.size(); ++i) {
        if (!is_action_locally_legal(state, state.my_snakes[i], root_actions[i])) all_legal = false;
    }
    if (all_legal && bucket == BeamBucket::SURVIVAL) return RootActionFamily::LEGALIZED_CONTINUE;
    if (bucket == BeamBucket::SURVIVAL) return RootActionFamily::SURVIVAL_ROOT;
    if (bucket == BeamBucket::GROWTH) return RootActionFamily::GROWTH_ROOT;
    if (bucket == BeamBucket::SPACE) return RootActionFamily::SPACE_ROOT;
    if (bucket == BeamBucket::CONTEST) return RootActionFamily::CONTEST_ROOT;
    if (bucket == BeamBucket::ATTACK_OR_PRESSURE) return RootActionFamily::ATTACK_ROOT;
    if (bucket == BeamBucket::ESCAPE_OR_RECOVERY) return RootActionFamily::ESCAPE_ROOT;
    return RootActionFamily::PLACEHOLDER_SHARED_ROOT;
}

static string build_novelty_label(const SharedGameState& state, BeamBucket bucket,
                                  RootActionFamily::Type family, const TerritoryAnalysis& territory) {
    string label = string(beam_bucket_to_string(bucket)) + "/" + root_family_to_string(family);
    label += ":alive=" + to_string(state.live_snake_count(0)) + "v" + to_string(state.live_snake_count(1));
    label += ":apple=" + to_string(clamp_band(territory.my_owned_apples, 1, 4))
        + "-" + to_string(clamp_band(territory.opp_owned_apples, 1, 4))
        + "-c" + to_string(clamp_band(territory.contested_apples, 1, 4));
    label += ":terr=" + to_string(clamp_band(territory.my_territory, 8, 6))
        + "-" + to_string(clamp_band(territory.opp_territory, 8, 6))
        + "-c" + to_string(clamp_band(territory.contested_territory, 8, 6));
    return label;
}

static uint64_t compute_novelty_signature(const SharedGameState& state, BeamBucket bucket,
                                         RootActionFamily::Type family) {
    TerritoryAnalysis territory = analyze_territory_and_apples(state);
    uint64_t h = 7809847782465536322ULL;
    h = hash_combine_u64(h, static_cast<uint64_t>(static_cast<int>(bucket) + 31));
    h = hash_combine_u64(h, static_cast<uint64_t>(static_cast<int>(family) + 71));

    int my_alive_mask = 0;
    int opp_alive_mask = 0;
    for (size_t i = 0; i < state.my_snakes.size() && i < 8; ++i) if (state.my_snakes[i].is_alive) my_alive_mask |= (1 << i);
    for (size_t i = 0; i < state.opp_snakes.size() && i < 8; ++i) if (state.opp_snakes[i].is_alive) opp_alive_mask |= (1 << i);
    h = hash_combine_u64(h, static_cast<uint64_t>(my_alive_mask + 1009));
    h = hash_combine_u64(h, static_cast<uint64_t>(opp_alive_mask + 2003));

    auto quantize_head = [&](const Snake& s) {
        if (!s.is_alive || s.length <= 0) return -1;
        int head = s.body[s.head_idx];
        int x = head % max_width;
        int y = head / max_width;
        return (x / 3) * 101 + (y / 3);
    };

    for (const Snake& s : state.my_snakes) h = hash_combine_u64(h, static_cast<uint64_t>(quantize_head(s) + 5003));
    for (const Snake& s : state.opp_snakes) h = hash_combine_u64(h, static_cast<uint64_t>(quantize_head(s) + 7001));

    int apple_band = min(7, count_remaining_powerups(state));
    h = hash_combine_u64(h, static_cast<uint64_t>(apple_band + 9001));
    h = hash_combine_u64(h, static_cast<uint64_t>(state.live_snake_count(0) * 13 + state.live_snake_count(1) * 17));
    h = hash_combine_u64(h, static_cast<uint64_t>(clamp_band(territory.my_owned_apples, 1, 4) + 11003));
    h = hash_combine_u64(h, static_cast<uint64_t>(clamp_band(territory.opp_owned_apples, 1, 4) + 12007));
    h = hash_combine_u64(h, static_cast<uint64_t>(clamp_band(territory.contested_apples, 1, 4) + 13001));
    h = hash_combine_u64(h, static_cast<uint64_t>(clamp_band(territory.my_territory, 8, 6) + 14009));
    h = hash_combine_u64(h, static_cast<uint64_t>(clamp_band(territory.opp_territory, 8, 6) + 15013));
    h = hash_combine_u64(h, static_cast<uint64_t>(clamp_band(territory.contested_territory, 8, 6) + 16001));
    return h;
}

static BeamNode make_beam_node(const SharedGameState& state, int depth, const vector<int>& root_actions,
                               const EvaluatorBreakdown& eval, const OpponentReplyPlan& reply,
                               const HotspotSummary& hotspot, RootActionFamily::Type family) {
    BeamNode node;
    node.state = state;
    node.state_hash = node.state.full_state_hash();
    node.depth = depth;
    node.scalar_score = eval.total_score;
    node.root_family = family;
    node.bucket = classify_beam_bucket(node.state, eval, hotspot);
    node.root_actions = root_actions;
    node.root_signature = encode_actions_signature(root_actions);
    TerritoryAnalysis territory = analyze_territory_and_apples(node.state);
    node.novelty_signature = compute_novelty_signature(node.state, node.bucket, node.root_family);
    node.novelty_label = build_novelty_label(node.state, node.bucket, node.root_family, territory);
    node.is_hotspot = hotspot.is_hotspot;
    node.hotspot_score = hotspot.score;
    node.hotspot_reasons = hotspot.reasons;
    if (!reply.opp_policies.empty()) {
        node.retention_reason = enemy_reply_policy_to_string(reply.opp_policies.front());
    }
    return node;
}

static vector<int> beam_bucket_quotas(size_t total_cap) {
    vector<int> quotas(6, max<int>(1, static_cast<int>(total_cap / 8)));
    quotas[static_cast<int>(BeamBucket::SURVIVAL)] = max(2, static_cast<int>(total_cap / 6));
    quotas[static_cast<int>(BeamBucket::GROWTH)] = max(1, static_cast<int>(total_cap / 8));
    quotas[static_cast<int>(BeamBucket::SPACE)] = max(2, static_cast<int>(total_cap / 6));
    quotas[static_cast<int>(BeamBucket::CONTEST)] = max(1, static_cast<int>(total_cap / 8));
    quotas[static_cast<int>(BeamBucket::ATTACK_OR_PRESSURE)] = max(1, static_cast<int>(total_cap / 8));
    quotas[static_cast<int>(BeamBucket::ESCAPE_OR_RECOVERY)] = max(1, static_cast<int>(total_cap / 8));
    return quotas;
}

static SearchTuning compute_search_tuning(const SharedGameState& root_state, const HotspotSummary& hotspot,
                                          const TacticalBranchSummary& tactical, size_t branching_pressure) {
    SearchTuning tuning;
    int total_live = root_state.live_snake_count(0) + root_state.live_snake_count(1);
    int board_area = world_width * world_height;
    int remaining = time_remaining_ms();

    tuning.max_depth = hotspot.is_hotspot ? 3 : 2;
    tuning.root_candidate_cap = hotspot.is_hotspot ? 18 : 12;
    tuning.root_beam_cap = hotspot.is_hotspot ? 14 : 10;
    tuning.frontier_beam_cap = hotspot.is_hotspot ? 12 : 8;
    tuning.reply_cap = hotspot.is_hotspot ? 18 : 10;
    tuning.tactical_root_cap = tactical.activated ? 10 : 6;
    tuning.followup_cap = hotspot.is_hotspot ? 8 : 4;

    if (total_live >= 6) {
        tuning.root_candidate_cap = max<size_t>(8, tuning.root_candidate_cap - 4);
        tuning.root_beam_cap = max<size_t>(6, tuning.root_beam_cap - 3);
        tuning.frontier_beam_cap = max<size_t>(5, tuning.frontier_beam_cap - 3);
        tuning.reply_cap = max<size_t>(6, tuning.reply_cap - 4);
        tuning.max_depth = min(tuning.max_depth, 2);
    }
    if (board_area >= 220) {
        tuning.root_candidate_cap += 2;
        tuning.frontier_beam_cap += 1;
    }
    if (branching_pressure >= 16) {
        tuning.root_candidate_cap = max<size_t>(8, tuning.root_candidate_cap - 2);
        tuning.frontier_beam_cap = max<size_t>(5, tuning.frontier_beam_cap - 1);
    }
    if (remaining <= 20) {
        tuning.max_depth = 1;
        tuning.root_candidate_cap = min<size_t>(tuning.root_candidate_cap, 8);
        tuning.root_beam_cap = min<size_t>(tuning.root_beam_cap, 6);
        tuning.frontier_beam_cap = min<size_t>(tuning.frontier_beam_cap, 5);
        tuning.reply_cap = min<size_t>(tuning.reply_cap, 6);
        tuning.tactical_root_cap = min<size_t>(tuning.tactical_root_cap, 4);
        tuning.followup_cap = min<size_t>(tuning.followup_cap, 3);
    } else if (remaining <= 28) {
        tuning.max_depth = min(tuning.max_depth, 2);
        tuning.root_candidate_cap = max<size_t>(8, tuning.root_candidate_cap - 2);
        tuning.reply_cap = max<size_t>(6, tuning.reply_cap - 2);
    }

    return tuning;
}

static BeamNode evaluate_plan_node(const SharedGameState& base_state, const JointActionPlan& plan,
                                   int depth, const vector<int>& root_actions,
                                   RootActionFamily::Type family, size_t reply_cap,
                                   OpponentReplyPlan* chosen_reply_out = nullptr,
                                   EvaluatorBreakdown* chosen_eval_out = nullptr,
                                   SharedGameState* chosen_state_out = nullptr,
                                   HotspotSummary* chosen_hotspot_out = nullptr) {
    HotspotSummary base_hotspot = analyze_hotspots(base_state);
    TacticalBranchSummary tactical = analyze_tactical_branch(base_state, base_hotspot);
    vector<OpponentReplyPlan> reply_candidates = tactical.activated
        ? generate_tactical_opponent_reply_plans(base_state, base_hotspot, tactical, reply_cap + 4)
        : generate_opponent_reply_plans(base_state, base_hotspot, reply_cap);
    if (reply_candidates.empty()) {
        OpponentReplyPlan reply;
        reply.opp_actions = infer_default_actions(base_state.opp_snakes);
        reply.opp_policies.assign(reply.opp_actions.size(), EnemyReplyPolicy::DefaultContinue);
        reply_candidates.push_back(reply);
    }

    int worst_reply_score = INT_MAX;
    EvaluatorBreakdown worst_eval;
    SharedGameState worst_state;
    OpponentReplyPlan worst_reply;
    TacticalLexicoScore worst_tactical_score;
    bool have_worst = false;
    for (const OpponentReplyPlan& reply : reply_candidates) {
        SharedGameState next_state = base_state;
        JointActionPlan combined = plan;
        combined.opp_actions = reply.opp_actions;
        next_state.simulate_joint_actions(combined);
        EvaluatorBreakdown eval = evaluate_shared_state(next_state);
        TacticalLexicoScore tactical_score = build_tactical_lexico_score(next_state, eval);
        bool take = false;
        if (!have_worst) {
            take = true;
        } else if (tactical.activated) {
            take = is_better_tactical_lexico_score(worst_tactical_score, tactical_score);
        } else {
            take = eval.total_score < worst_reply_score;
        }
        if (take) {
            have_worst = true;
            worst_reply_score = eval.total_score;
            worst_eval = eval;
            worst_state = std::move(next_state);
            worst_reply = reply;
            worst_tactical_score = tactical_score;
        }
    }

    HotspotSummary result_hotspot = analyze_hotspots(worst_state);
    RootActionFamily::Type resolved_family = family;
    BeamBucket resolved_bucket = classify_beam_bucket(worst_state, worst_eval, result_hotspot);
    if (resolved_family == RootActionFamily::PLACEHOLDER_SHARED_ROOT) {
        resolved_family = infer_root_family(base_state, root_actions, resolved_bucket);
    }
    if (chosen_reply_out != nullptr) *chosen_reply_out = worst_reply;
    if (chosen_eval_out != nullptr) *chosen_eval_out = worst_eval;
    if (chosen_state_out != nullptr) *chosen_state_out = worst_state;
    if (chosen_hotspot_out != nullptr) *chosen_hotspot_out = result_hotspot;
    BeamNode node = make_beam_node(worst_state, depth, root_actions, worst_eval, worst_reply, result_hotspot, resolved_family);
    node.bucket = resolved_bucket;
    node.novelty_signature = compute_novelty_signature(node.state, node.bucket, node.root_family);
    node.tactical_extension = tactical.activated;
    node.tactical_summary = tactical.summary;
    return node;
}

static vector<BeamNode> select_diverse_beam(vector<BeamNode> candidates, size_t total_cap, string& summary) {
    vector<BeamNode> selected;
    summary.clear();
    if (candidates.empty()) return selected;

    unordered_map<uint64_t, BeamNode> best_by_exact;
    unordered_map<uint64_t, int> exact_population;
    for (const BeamNode& node : candidates) {
        exact_population[node.state_hash]++;
        auto it = best_by_exact.find(node.state_hash);
        if (it == best_by_exact.end() || node.scalar_score > it->second.scalar_score) {
            best_by_exact[node.state_hash] = node;
        }
    }

    vector<BeamNode> unique_candidates;
    unique_candidates.reserve(best_by_exact.size());
    for (auto& kv : best_by_exact) {
        kv.second.exact_merge_count = exact_population[kv.first] - 1;
        unique_candidates.push_back(kv.second);
    }

    vector<int> bucket_quota = beam_bucket_quotas(total_cap);
    unordered_map<int, int> bucket_count;
    unordered_map<int, int> family_count;
    unordered_map<uint64_t, int> novelty_count;
    unordered_map<uint64_t, int> novelty_population;
    unordered_set<uint64_t> kept_root_signatures;
    unordered_set<uint64_t> kept_state_hashes;

    for (const BeamNode& node : unique_candidates) novelty_population[node.novelty_signature]++;

    sort(unique_candidates.begin(), unique_candidates.end(), [&](const BeamNode& a, const BeamNode& b) {
        if (a.tactical_extension || b.tactical_extension) {
            TacticalLexicoScore ta = build_tactical_lexico_score(a.state, evaluate_shared_state(a.state));
            TacticalLexicoScore tb = build_tactical_lexico_score(b.state, evaluate_shared_state(b.state));
            if (is_better_tactical_lexico_score(ta, tb)) return true;
            if (is_better_tactical_lexico_score(tb, ta)) return false;
        }
        int pa = a.scalar_score - max(0, novelty_population[a.novelty_signature] - 1) * 6 - a.exact_merge_count * 2;
        int pb = b.scalar_score - max(0, novelty_population[b.novelty_signature] - 1) * 6 - b.exact_merge_count * 2;
        if (pa != pb) return pa > pb;
        if (a.scalar_score != b.scalar_score) return a.scalar_score > b.scalar_score;
        return a.state_hash < b.state_hash;
    });

    auto family_quota = [&](RootActionFamily::Type family) {
        if (family == RootActionFamily::DEFAULT_CONTINUE || family == RootActionFamily::LEGALIZED_CONTINUE) {
            return max(2, static_cast<int>(total_cap / 5));
        }
        return max(1, static_cast<int>(total_cap / 4));
    };

    auto novelty_cap = [&](const BeamNode& node) {
        if (node.bucket == BeamBucket::SURVIVAL || node.bucket == BeamBucket::SPACE) return 2;
        return 1;
    };

    auto try_add = [&](const BeamNode& source, const string& reason,
                       bool ignore_bucket, bool ignore_family, bool ignore_novelty) {
        if (selected.size() >= total_cap) return false;
        if (kept_state_hashes.count(source.state_hash)) return false;

        int bucket_idx = static_cast<int>(source.bucket);
        int family_idx = static_cast<int>(source.root_family);
        if (!ignore_bucket && bucket_count[bucket_idx] >= bucket_quota[bucket_idx]) return false;
        if (!ignore_family && family_count[family_idx] >= family_quota(source.root_family)) return false;
        if (!ignore_novelty && novelty_count[source.novelty_signature] >= novelty_cap(source)) return false;

        BeamNode node = source;
        node.retention_reason = reason;
        node.novelty_population = novelty_population[source.novelty_signature];
        selected.push_back(node);
        kept_state_hashes.insert(node.state_hash);
        kept_root_signatures.insert(node.root_signature);
        bucket_count[bucket_idx]++;
        family_count[family_idx]++;
        novelty_count[node.novelty_signature]++;
        return true;
    };

    unordered_set<uint64_t> novelty_seeded;
    for (const BeamNode& node : unique_candidates) {
        if (selected.size() >= total_cap) break;
        if (!novelty_seeded.insert(node.novelty_signature).second) continue;
        try_add(node, "novelty_seed", true, true, true);
    }

    for (const BeamNode& node : unique_candidates) {
        if (selected.size() >= total_cap) break;
        if (kept_root_signatures.count(node.root_signature)) continue;
        if (node.scalar_score <= -300000) continue;
        try_add(node, "root_quota", true, true, true);
    }

    for (int bucket_idx = 0; bucket_idx < 6 && selected.size() < total_cap; ++bucket_idx) {
        for (const BeamNode& node : unique_candidates) {
            if (selected.size() >= total_cap) break;
            if (static_cast<int>(node.bucket) != bucket_idx) continue;
            if (bucket_count[bucket_idx] >= bucket_quota[bucket_idx]) break;
            try_add(node, "bucket_quota", false, true, false);
        }
    }

    for (const BeamNode& node : unique_candidates) {
        if (selected.size() >= total_cap) break;
        try_add(node, "family_quota", true, false, false);
    }

    for (const BeamNode& node : unique_candidates) {
        if (selected.size() >= total_cap) break;
        try_add(node, "score_fill", false, false, false);
    }

    summary = "retained=" + to_string(selected.size())
        + ",exact_groups=" + to_string(unique_candidates.size())
        + ",exact_merged=" + to_string(static_cast<int>(candidates.size() - unique_candidates.size()))
        + ",novelty_groups=" + to_string(novelty_population.size());
    for (size_t i = 0; i < selected.size(); ++i) {
        const BeamNode& node = selected[i];
        summary += "|d" + to_string(node.depth)
            + ":s=" + to_string(node.scalar_score)
            + ",b=" + beam_bucket_to_string(node.bucket)
            + ",f=" + root_family_to_string(node.root_family)
            + ",r=" + node.retention_reason
            + ",tx=" + string(node.tactical_extension ? "1" : "0")
            + ",np=" + to_string(node.novelty_population)
            + ",xm=" + to_string(node.exact_merge_count)
            + ",nl=" + node.novelty_label
            + ",ts=" + node.tactical_summary
            + ",a=" + actions_to_string(node.root_actions);
    }
    return selected;
}

static vector<BeamNode> expand_beam_frontier(const vector<BeamNode>& frontier, int next_depth, string& expansion_summary,
                                             int& expanded_nodes, size_t followup_cap_override = 0) {
    vector<BeamNode> candidates;
    expansion_summary.clear();
    expanded_nodes = 0;
    for (const BeamNode& node : frontier) {
        if (out_of_time()) break;
        size_t root_cap = node.is_hotspot ? 8 : 4;
        if (followup_cap_override > 0) root_cap = min(root_cap, followup_cap_override);
        size_t reply_cap = node.is_hotspot ? 10 : 6;
        HotspotSummary hotspot = analyze_hotspots(node.state);
        TacticalBranchSummary tactical = analyze_tactical_branch(node.state, hotspot);
        vector<JointActionPlan> plans = tactical.activated
            ? generate_tactical_followup_candidates(node.state, tactical, max<size_t>(root_cap, followup_cap_override > 0 ? followup_cap_override : root_cap))
            : generate_structured_followup_candidates(node.state, root_cap);
        for (const JointActionPlan& plan : plans) {
            if (out_of_time()) break;
            BeamNode child = evaluate_plan_node(node.state, plan, next_depth, node.root_actions, node.root_family, reply_cap);
            candidates.push_back(std::move(child));
            expanded_nodes++;
        }
        expansion_summary += "parent=" + actions_to_string(node.root_actions)
            + ":tactical=" + string(tactical.activated ? "1" : "0")
            + ":participants=" + tactical.summary
            + ":expanded=" + to_string(plans.size()) + ";";
    }
    return candidates;
}

static BeamSearchResult choose_story85_beam_plan(const SharedGameState& root_state) {
    BeamSearchResult result;
    JointActionPlan fallback = choose_story81_baseline_plan(root_state);
    result.chosen_plan = fallback;

    HotspotSummary root_hotspot = analyze_hotspots(root_state);
    TacticalBranchSummary root_tactical = analyze_tactical_branch(root_state, root_hotspot);
    SearchTuning tuning = compute_search_tuning(root_state, root_hotspot, root_tactical, root_state.my_snakes.size() * 4);
    vector<FriendlyRootCandidate> root_candidates = generate_team_root_candidates(root_state, tuning.root_candidate_cap);
    if (root_tactical.activated) {
        vector<JointActionPlan> tactical_root_plans = generate_tactical_followup_candidates(root_state, root_tactical, tuning.tactical_root_cap);
        for (const JointActionPlan& plan : tactical_root_plans) {
            if (root_candidates.size() >= tuning.root_candidate_cap) break;
            FriendlyRootCandidate candidate;
            candidate.family = RootActionFamily::PLACEHOLDER_SHARED_ROOT;
            candidate.plan = plan;
            root_candidates.push_back(candidate);
        }
    }
    if (root_candidates.empty()) {
        FriendlyRootCandidate fallback_candidate;
        fallback_candidate.family = RootActionFamily::LEGALIZED_CONTINUE;
        fallback_candidate.plan = fallback;
        root_candidates.push_back(fallback_candidate);
    }

    vector<BeamNode> root_nodes;
    int root_phase_start = elapsed_turn_ms();
    for (const FriendlyRootCandidate& candidate : root_candidates) {
        if (out_of_time()) break;
        BeamNode node = evaluate_plan_node(root_state, candidate.plan, 1, candidate.plan.my_actions, candidate.family, tuning.reply_cap);
        root_nodes.push_back(std::move(node));
    }
    if (root_nodes.empty()) {
        BeamNode fallback_node = evaluate_plan_node(root_state, fallback, 1, fallback.my_actions, RootActionFamily::LEGALIZED_CONTINUE, tuning.reply_cap);
        root_nodes.push_back(std::move(fallback_node));
    }

    string root_summary;
    vector<BeamNode> frontier = select_diverse_beam(root_nodes, tuning.root_beam_cap, root_summary);
    vector<BeamNode> best_completed_frontier = frontier;
    result.completed_depth = 1;
    result.expanded_nodes = static_cast<int>(root_nodes.size());
    string diagnostics = "tuning{remaining_ms=" + to_string(time_remaining_ms())
        + ",root_candidates=" + to_string(tuning.root_candidate_cap)
        + ",root_beam=" + to_string(tuning.root_beam_cap)
        + ",frontier_beam=" + to_string(tuning.frontier_beam_cap)
        + ",reply_cap=" + to_string(tuning.reply_cap)
        + ",followup_cap=" + to_string(tuning.followup_cap)
        + ",max_depth=" + to_string(tuning.max_depth)
        + "} depth1{root_tactical=" + string(root_tactical.activated ? "1" : "0")
        + ",participants=" + root_tactical.summary
        + ",phase_ms=" + to_string(elapsed_turn_ms() - root_phase_start)
        + ";" + root_summary + "}";

    for (int depth = 2; depth <= tuning.max_depth; ++depth) {
        if (out_of_time_with_guard(8)) break;
        int phase_start = elapsed_turn_ms();
        string expansion_summary;
        int expanded_nodes = 0;
        vector<BeamNode> expanded = expand_beam_frontier(frontier, depth, expansion_summary, expanded_nodes, tuning.followup_cap);
        if (expanded.empty()) break;
        if (out_of_time_with_guard(6)) {
            diagnostics += " depth" + to_string(depth) + "{aborted=1,phase_ms=" + to_string(elapsed_turn_ms() - phase_start)
                + ",expanded=" + to_string(expanded_nodes) + "}";
            break;
        }
        string select_summary;
        frontier = select_diverse_beam(expanded, tuning.frontier_beam_cap, select_summary);
        best_completed_frontier = frontier;
        result.completed_depth = depth;
        result.expanded_nodes += expanded_nodes;
        diagnostics += " depth" + to_string(depth) + "{phase_ms=" + to_string(elapsed_turn_ms() - phase_start)
            + ",expanded=" + to_string(expanded_nodes) + ";" + expansion_summary + select_summary + "}";
    }

    frontier = best_completed_frontier;

    if (!frontier.empty()) {
        auto best_it = max_element(frontier.begin(), frontier.end(), [](const BeamNode& a, const BeamNode& b) {
            return !is_better_beam_node(a, b);
        });
        const BeamNode* runner_up = nullptr;
        for (const BeamNode& node : frontier) {
            if (node.state_hash == best_it->state_hash) continue;
            if (runner_up == nullptr || is_better_beam_node(node, *runner_up)) runner_up = &node;
        }
        result.chosen_plan.my_actions = best_it->root_actions;
        result.chosen_plan.opp_actions = infer_default_actions(root_state.opp_snakes);
        result.best_family = best_it->root_family;
        result.best_bucket = best_it->bucket;
        result.winner_retention_reason = best_it->retention_reason;
        result.diversity_preserved_winner = winner_preserved_by_diversity(*best_it);
        result.winner_over_runner_up_reason = build_winner_over_runner_up_reason(*best_it, runner_up);
        JointActionPlan selected_root_plan = result.chosen_plan;
        BeamNode resolved = evaluate_plan_node(root_state, selected_root_plan, 1, selected_root_plan.my_actions,
                                              result.best_family, tuning.reply_cap,
                                              &result.best_reply, &result.best_eval,
                                              &result.best_next_state, &result.best_hotspot);
        result.best_family = resolved.root_family;
        result.best_bucket = resolved.bucket;
        result.final_frontier = frontier;
        diagnostics += " decision{winner_family=" + string(root_family_to_string(best_it->root_family))
            + ",winner_bucket=" + string(beam_bucket_to_string(best_it->bucket))
            + ",winner_retention=" + best_it->retention_reason
            + ",diversity_preserved=" + string(result.diversity_preserved_winner ? "1" : "0")
            + ",reason=" + result.winner_over_runner_up_reason;
        if (runner_up != nullptr) {
            diagnostics += ",runner_family=" + string(root_family_to_string(runner_up->root_family))
                + ",runner_bucket=" + string(beam_bucket_to_string(runner_up->bucket))
                + ",runner_score=" + to_string(runner_up->scalar_score)
                + ",runner_retention=" + runner_up->retention_reason;
        }
        diagnostics += "}";
    } else {
        result.best_next_state = root_state;
        result.best_next_state.simulate_joint_actions(fallback);
        result.best_eval = evaluate_shared_state(result.best_next_state);
        result.best_family = RootActionFamily::LEGALIZED_CONTINUE;
        result.best_bucket = BeamBucket::SURVIVAL;
        result.best_hotspot = analyze_hotspots(result.best_next_state);
        result.best_reply.opp_actions = infer_default_actions(root_state.opp_snakes);
        result.best_reply.opp_policies.assign(result.best_reply.opp_actions.size(), EnemyReplyPolicy::DefaultContinue);
        result.winner_retention_reason = "fallback";
        result.winner_over_runner_up_reason = "frontier_empty";
        result.diversity_preserved_winner = false;
    }

    if (result.chosen_plan.my_actions.empty()) result.chosen_plan = fallback;
    result.diagnostics = diagnostics;
    return result;
}

static vector<int> infer_default_actions(const vector<Snake>& snakes) {
    vector<int> actions(snakes.size(), 0);
    for (size_t i = 0; i < snakes.size(); ++i) actions[i] = infer_previous_action(snakes[i]);
    return actions;
}

static const char* enemy_reply_policy_to_string(EnemyReplyPolicy policy) {
    if (policy == EnemyReplyPolicy::DefaultContinue) return "DEFAULT_CONTINUE";
    if (policy == EnemyReplyPolicy::SafeSpace) return "SAFE_SPACE";
    if (policy == EnemyReplyPolicy::AppleRace) return "APPLE_RACE";
    if (policy == EnemyReplyPolicy::HeadPressure) return "HEAD_PRESSURE";
    return "DEFAULT_CONTINUE";
}

static int manhattan_dist_pos(int p1, int p2) {
    int x1 = p1 % max_width;
    int y1 = p1 / max_width;
    int x2 = p2 % max_width;
    int y2 = p2 / max_width;
    return abs(x1 - x2) + abs(y1 - y2);
}

static bool is_action_locally_legal(const SharedGameState& state, const Snake& s, int action) {
    if (!s.is_alive || s.length <= 0) return false;
    if (is_backward_action(s, action)) return false;
    int head_pos = s.body[s.head_idx];
    int hx = head_pos % max_width;
    int hy = head_pos / max_width;
    int nx = hx;
    int ny = hy;
    if (action == 0) ny -= 1;
    else if (action == 1) ny += 1;
    else if (action == 2) nx -= 1;
    else if (action == 3) nx += 1;
    if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) return false;
    int n_pos = ny * max_width + nx;
    int16_t cell = state.grid[n_pos];
    return !(cell == CELL_WALL || cell >= CELL_SNAKE_BASE);
}

static int first_legal_action_shared(const SharedGameState& state, const Snake& s) {
    int preferred = infer_previous_action(s);
    if (is_action_locally_legal(state, s, preferred)) return preferred;
    for (int action = 0; action < 4; ++action) {
        if (is_action_locally_legal(state, s, action)) return action;
    }
    return preferred;
}

static int nearest_powerup_distance(const SharedGameState& state, int from_pos, int* target_pos) {
    int best = INT_MAX;
    int best_pos = -1;
    for (int pos = 0; pos < grid_size; ++pos) {
        if (state.grid[pos] != CELL_POWERUP) continue;
        int d = manhattan_dist_pos(from_pos, pos);
        if (d < best) {
            best = d;
            best_pos = pos;
        }
    }
    if (target_pos != nullptr) *target_pos = best_pos;
    return best;
}

static int nearest_my_head_distance(const SharedGameState& state, int from_pos, int* target_pos = nullptr) {
    int best = INT_MAX;
    int best_pos = -1;
    for (const Snake& s : state.my_snakes) {
        if (!s.is_alive || s.length <= 0) continue;
        int head = s.body[s.head_idx];
        int d = manhattan_dist_pos(from_pos, head);
        if (d < best) {
            best = d;
            best_pos = head;
        }
    }
    if (target_pos != nullptr) *target_pos = best_pos;
    return best;
}

static int local_open_neighbor_count(const SharedGameState& state, int pos) {
    int count = 0;
    int x = pos % max_width;
    int y = pos / max_width;
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    for (int i = 0; i < 4; ++i) {
        int nx = x + dx[i];
        int ny = y + dy[i];
        if (nx < 0 || nx >= max_width || ny < 0 || ny >= max_height) continue;
        int n_pos = ny * max_width + nx;
        int16_t cell = state.grid[n_pos];
        if (cell == CELL_EMPTY || cell == CELL_POWERUP) count++;
    }
    return count;
}

static bool is_hot_enemy_snake(const SharedGameState& state, const Snake& enemy) {
    if (!enemy.is_alive || enemy.length <= 0) return false;
    int enemy_head = enemy.body[enemy.head_idx];
    int nearest_head = nearest_my_head_distance(state, enemy_head);
    int nearest_apple = nearest_powerup_distance(state, enemy_head);
    return nearest_head <= 6 || nearest_apple <= 4;
}

static EnemyReplyChoice choose_enemy_safe_space_reply(const SharedGameState& state, const Snake& enemy) {
    EnemyReplyChoice choice;
    choice.action = first_legal_action_shared(state, enemy);
    choice.policy = EnemyReplyPolicy::SafeSpace;

    int best_score = INT_MIN;
    for (int action : legal_actions_for_snake(state, enemy)) {
        int head = enemy.body[enemy.head_idx];
        int hx = head % max_width;
        int hy = head / max_width;
        int nx = hx + (action == 2 ? -1 : action == 3 ? 1 : 0);
        int ny = hy + (action == 0 ? -1 : action == 1 ? 1 : 0);
        int n_pos = ny * max_width + nx;
        int score = local_open_neighbor_count(state, n_pos) * 10;
        if (score > best_score) {
            best_score = score;
            choice.action = action;
        }
    }
    return choice;
}

static EnemyReplyChoice choose_enemy_apple_race_reply(const SharedGameState& state, const Snake& enemy) {
    EnemyReplyChoice choice;
    choice.action = first_legal_action_shared(state, enemy);
    choice.policy = EnemyReplyPolicy::AppleRace;

    int target_pos = -1;
    int best_dist = nearest_powerup_distance(state, enemy.body[enemy.head_idx], &target_pos);
    if (best_dist == INT_MAX || target_pos == -1) return choice;

    int best_score = INT_MAX;
    for (int action : legal_actions_for_snake(state, enemy)) {
        int head = enemy.body[enemy.head_idx];
        int hx = head % max_width;
        int hy = head / max_width;
        int nx = hx + (action == 2 ? -1 : action == 3 ? 1 : 0);
        int ny = hy + (action == 0 ? -1 : action == 1 ? 1 : 0);
        int n_pos = ny * max_width + nx;
        int score = manhattan_dist_pos(n_pos, target_pos);
        if (score < best_score) {
            best_score = score;
            choice.action = action;
        }
    }
    return choice;
}

static EnemyReplyChoice choose_enemy_head_pressure_reply(const SharedGameState& state, const Snake& enemy) {
    EnemyReplyChoice choice;
    choice.action = first_legal_action_shared(state, enemy);
    choice.policy = EnemyReplyPolicy::HeadPressure;

    int target_pos = -1;
    int nearest_head = nearest_my_head_distance(state, enemy.body[enemy.head_idx], &target_pos);
    if (nearest_head == INT_MAX || target_pos == -1) return choice;

    int best_score = INT_MAX;
    for (int action : legal_actions_for_snake(state, enemy)) {
        int head = enemy.body[enemy.head_idx];
        int hx = head % max_width;
        int hy = head / max_width;
        int nx = hx + (action == 2 ? -1 : action == 3 ? 1 : 0);
        int ny = hy + (action == 0 ? -1 : action == 1 ? 1 : 0);
        int n_pos = ny * max_width + nx;
        int score = manhattan_dist_pos(n_pos, target_pos);
        if (score < best_score) {
            best_score = score;
            choice.action = action;
        }
    }
    return choice;
}

static vector<EnemyReplyChoice> generate_enemy_reply_choices(const SharedGameState& state, const Snake& enemy, const HotspotSummary& hotspot) {
    vector<EnemyReplyChoice> choices;

    EnemyReplyChoice default_choice;
    default_choice.action = first_legal_action_shared(state, enemy);
    default_choice.policy = EnemyReplyPolicy::DefaultContinue;
    choices.push_back(default_choice);

    if (!is_hot_enemy_snake(state, enemy) && !hotspot.is_hotspot) return choices;

    auto push_unique = [&](const EnemyReplyChoice& candidate) {
        for (const auto& existing : choices) {
            if (existing.action == candidate.action && existing.policy == candidate.policy) return;
        }
        bool duplicate_action = false;
        for (const auto& existing : choices) {
            if (existing.action == candidate.action) {
                duplicate_action = true;
                break;
            }
        }
        if (!duplicate_action) choices.push_back(candidate);
    };

    push_unique(choose_enemy_safe_space_reply(state, enemy));
    if (nearest_powerup_distance(state, enemy.body[enemy.head_idx]) != INT_MAX) {
        push_unique(choose_enemy_apple_race_reply(state, enemy));
    }
    if (nearest_my_head_distance(state, enemy.body[enemy.head_idx]) <= 6) {
        push_unique(choose_enemy_head_pressure_reply(state, enemy));
    }

    size_t reply_cap = hotspot.is_hotspot ? 4 : 3;
    if (choices.size() > reply_cap) choices.resize(reply_cap);
    return choices;
}

static void generate_opponent_reply_plans_dfs(const SharedGameState& state, size_t idx,
                                              vector<int>& current_actions,
                                              vector<EnemyReplyPolicy>& current_policies,
                                              vector<OpponentReplyPlan>& out,
                                              size_t cap,
                                              int hot_enemy_count,
                                              int expanded_hot_enemies,
                                              const HotspotSummary& hotspot) {
    if (out.size() >= cap) return;
    if (idx >= state.opp_snakes.size()) {
        OpponentReplyPlan plan;
        plan.opp_actions = current_actions;
        plan.opp_policies = current_policies;
        plan.hot_enemy_count = hot_enemy_count;
        out.push_back(std::move(plan));
        return;
    }

    const Snake& enemy = state.opp_snakes[idx];
    if (!enemy.is_alive || enemy.length <= 0) {
        current_actions[idx] = infer_previous_action(enemy);
        current_policies[idx] = EnemyReplyPolicy::DefaultContinue;
        generate_opponent_reply_plans_dfs(state, idx + 1, current_actions, current_policies, out, cap, hot_enemy_count, expanded_hot_enemies, hotspot);
        return;
    }

    bool hot = is_hot_enemy_snake(state, enemy);
    vector<EnemyReplyChoice> choices;
    int max_expanded_hot_enemies = hotspot.is_hotspot ? 3 : 2;
    if ((hot || hotspot.is_hotspot) && expanded_hot_enemies < max_expanded_hot_enemies) {
        choices = generate_enemy_reply_choices(state, enemy, hotspot);
    } else {
        EnemyReplyChoice choice;
        choice.action = first_legal_action_shared(state, enemy);
        choice.policy = EnemyReplyPolicy::DefaultContinue;
        choices.push_back(choice);
    }

    for (const EnemyReplyChoice& choice : choices) {
        current_actions[idx] = choice.action;
        current_policies[idx] = choice.policy;
        generate_opponent_reply_plans_dfs(
            state,
            idx + 1,
            current_actions,
            current_policies,
            out,
            cap,
            hot_enemy_count + (hot ? 1 : 0),
            expanded_hot_enemies + (((hot || hotspot.is_hotspot) && choices.size() > 1) ? 1 : 0),
            hotspot);
        if (out.size() >= cap) return;
    }
}

static vector<OpponentReplyPlan> generate_opponent_reply_plans(const SharedGameState& state, const HotspotSummary& hotspot, size_t cap) {
    vector<OpponentReplyPlan> plans;
    vector<int> actions = infer_default_actions(state.opp_snakes);
    vector<EnemyReplyPolicy> policies(state.opp_snakes.size(), EnemyReplyPolicy::DefaultContinue);
    generate_opponent_reply_plans_dfs(state, 0, actions, policies, plans, cap, 0, 0, hotspot);
    return plans;
}

static string join_snake_ids(const vector<Snake>& snakes, const vector<int>& indices) {
    string out;
    for (size_t i = 0; i < indices.size(); ++i) {
        int idx = indices[i];
        if (idx < 0 || idx >= static_cast<int>(snakes.size())) continue;
        if (!out.empty()) out += ",";
        out += to_string(snakes[idx].id);
    }
    if (out.empty()) out = "NONE";
    return out;
}

static TacticalBranchSummary analyze_tactical_branch(const SharedGameState& state, const HotspotSummary& hotspot) {
    TacticalBranchSummary tactical;
    if (!hotspot.is_hotspot) {
        tactical.summary = "OFF";
        return tactical;
    }

    vector<pair<int, int>> my_ranked;
    for (size_t i = 0; i < state.my_snakes.size(); ++i) {
        const Snake& s = state.my_snakes[i];
        if (!s.is_alive || s.length <= 0) continue;
        int head = s.body[s.head_idx];
        int urgency = 0;
        int legal = count_legal_actions_for_snake(state, s);
        if (legal <= 2) urgency += 100;
        int enemy_dist = nearest_enemy_head_distance(state, head);
        if (enemy_dist <= 2) urgency += 90;
        else if (enemy_dist <= 4) urgency += 60;
        else if (enemy_dist <= 6) urgency += 35;
        int apple_dist = nearest_powerup_distance(state, head);
        if (apple_dist <= 2) urgency += 20;
        if (s.consumed_powerup_pos >= 0) urgency += 25;
        my_ranked.push_back({-urgency, static_cast<int>(i)});
    }
    sort(my_ranked.begin(), my_ranked.end());
    for (const auto& entry : my_ranked) {
        if (tactical.my_indices.size() >= 2) break;
        if (-entry.first >= 35) tactical.my_indices.push_back(entry.second);
    }

    vector<pair<int, int>> opp_ranked;
    for (size_t i = 0; i < state.opp_snakes.size(); ++i) {
        const Snake& s = state.opp_snakes[i];
        if (!s.is_alive || s.length <= 0) continue;
        int head = s.body[s.head_idx];
        int urgency = 0;
        int my_dist = nearest_my_head_distance(state, head);
        if (my_dist <= 2) urgency += 90;
        else if (my_dist <= 4) urgency += 60;
        else if (my_dist <= 6) urgency += 35;
        int apple_dist = nearest_powerup_distance(state, head);
        if (apple_dist <= 3) urgency += 25;
        if (is_hot_enemy_snake(state, s)) urgency += 20;
        opp_ranked.push_back({-urgency, static_cast<int>(i)});
    }
    sort(opp_ranked.begin(), opp_ranked.end());
    for (const auto& entry : opp_ranked) {
        if (tactical.opp_indices.size() >= 2) break;
        if (-entry.first >= 35) tactical.opp_indices.push_back(entry.second);
    }

    tactical.activated = !tactical.my_indices.empty() || !tactical.opp_indices.empty();
    tactical.summary = string("MY=") + join_snake_ids(state.my_snakes, tactical.my_indices)
        + ";OPP=" + join_snake_ids(state.opp_snakes, tactical.opp_indices);
    return tactical;
}

static vector<int> tactical_action_options_for_snake(const SharedGameState& state, const Snake& s) {
    vector<int> options;
    int enemy_dist = nearest_enemy_head_distance(state, s.body[s.head_idx]);
    int apple_pos = -1;
    int apple_dist = nearest_powerup_distance(state, s.body[s.head_idx], &apple_pos);

    auto push_unique = [&](int action) {
        if (action < 0 || action >= 4) return;
        if (!is_action_locally_legal(state, s, action)) return;
        for (int existing : options) if (existing == action) return;
        options.push_back(action);
    };

    if (count_legal_actions_for_snake(state, s) <= 2) push_unique(choose_escape_action(state, s));
    if (enemy_dist <= 4) push_unique(choose_pressure_action(state, s));
    if (apple_dist != INT_MAX && apple_pos != -1) push_unique(choose_action_toward_target(state, s, apple_pos));
    push_unique(choose_safe_spread_action(state, s));
    push_unique(first_legal_action_shared(state, s));

    vector<int> legal = legal_actions_for_snake(state, s);
    for (int action : legal) {
        if (options.size() >= 3) break;
        push_unique(action);
    }
    return options;
}

static void generate_tactical_followup_candidates_dfs(const SharedGameState& state,
                                                      const vector<int>& participant_indices,
                                                      const vector<vector<int>>& participant_options,
                                                      size_t depth_idx,
                                                      vector<int>& current_actions,
                                                      unordered_set<int>& reserved_positions,
                                                      vector<JointActionPlan>& out,
                                                      size_t cap) {
    if (out.size() >= cap) return;
    if (depth_idx >= participant_indices.size()) {
        JointActionPlan plan;
        plan.my_actions = current_actions;
        plan.opp_actions = infer_default_actions(state.opp_snakes);
        for (size_t i = 0; i < state.opp_snakes.size(); ++i) {
            plan.opp_actions[i] = first_legal_action_shared(state, state.opp_snakes[i]);
        }
        out.push_back(std::move(plan));
        return;
    }

    int snake_idx = participant_indices[depth_idx];
    const Snake& s = state.my_snakes[snake_idx];
    for (int action : participant_options[depth_idx]) {
        int n_pos = next_position_for_action(s, action);
        if (reserved_positions.count(n_pos)) continue;
        current_actions[snake_idx] = action;
        reserved_positions.insert(n_pos);
        generate_tactical_followup_candidates_dfs(state, participant_indices, participant_options, depth_idx + 1,
                                                  current_actions, reserved_positions, out, cap);
        reserved_positions.erase(n_pos);
        if (out.size() >= cap) return;
    }
}

static vector<JointActionPlan> generate_tactical_followup_candidates(const SharedGameState& state, const TacticalBranchSummary& tactical, size_t cap) {
    vector<JointActionPlan> plans;
    JointActionPlan base_plan = choose_story81_baseline_plan(state);
    if (!tactical.activated || tactical.my_indices.empty()) {
        plans.push_back(base_plan);
        return plans;
    }

    vector<vector<int>> participant_options;
    unordered_set<int> reserved_positions;
    for (size_t i = 0; i < state.my_snakes.size(); ++i) {
        bool participant = false;
        for (int idx : tactical.my_indices) {
            if (idx == static_cast<int>(i)) {
                participant = true;
                break;
            }
        }
        if (!participant) {
            const Snake& s = state.my_snakes[i];
            if (!s.is_alive || s.length <= 0) continue;
            reserved_positions.insert(next_position_for_action(s, base_plan.my_actions[i]));
        }
    }

    for (int idx : tactical.my_indices) {
        participant_options.push_back(tactical_action_options_for_snake(state, state.my_snakes[idx]));
    }

    vector<int> current_actions = base_plan.my_actions;
    generate_tactical_followup_candidates_dfs(state, tactical.my_indices, participant_options, 0,
                                              current_actions, reserved_positions, plans, cap);
    if (plans.empty()) plans.push_back(base_plan);
    return plans;
}

static void generate_tactical_opponent_reply_plans_dfs(const SharedGameState& state,
                                                       size_t idx,
                                                       const TacticalBranchSummary& tactical,
                                                       vector<int>& current_actions,
                                                       vector<EnemyReplyPolicy>& current_policies,
                                                       vector<OpponentReplyPlan>& out,
                                                       size_t cap) {
    if (out.size() >= cap) return;
    if (idx >= state.opp_snakes.size()) {
        OpponentReplyPlan plan;
        plan.opp_actions = current_actions;
        plan.opp_policies = current_policies;
        int hot_count = 0;
        for (int opp_idx : tactical.opp_indices) {
            if (opp_idx >= 0 && opp_idx < static_cast<int>(state.opp_snakes.size())) hot_count++;
        }
        plan.hot_enemy_count = hot_count;
        out.push_back(std::move(plan));
        return;
    }

    const Snake& enemy = state.opp_snakes[idx];
    bool participant = false;
    for (int tactical_idx : tactical.opp_indices) {
        if (tactical_idx == static_cast<int>(idx)) {
            participant = true;
            break;
        }
    }

    if (!enemy.is_alive || enemy.length <= 0 || !participant) {
        current_actions[idx] = (!enemy.is_alive || enemy.length <= 0) ? infer_previous_action(enemy) : first_legal_action_shared(state, enemy);
        current_policies[idx] = EnemyReplyPolicy::DefaultContinue;
        generate_tactical_opponent_reply_plans_dfs(state, idx + 1, tactical, current_actions, current_policies, out, cap);
        return;
    }

    HotspotSummary hotspot = analyze_hotspots(state);
    vector<EnemyReplyChoice> choices = generate_enemy_reply_choices(state, enemy, hotspot);
    if (choices.size() > 4) choices.resize(4);
    for (const EnemyReplyChoice& choice : choices) {
        current_actions[idx] = choice.action;
        current_policies[idx] = choice.policy;
        generate_tactical_opponent_reply_plans_dfs(state, idx + 1, tactical, current_actions, current_policies, out, cap);
        if (out.size() >= cap) return;
    }
}

static vector<OpponentReplyPlan> generate_tactical_opponent_reply_plans(const SharedGameState& state, const HotspotSummary& hotspot, const TacticalBranchSummary& tactical, size_t cap) {
    if (!tactical.activated || tactical.opp_indices.empty()) {
        return generate_opponent_reply_plans(state, hotspot, cap);
    }

    vector<OpponentReplyPlan> plans;
    vector<int> actions = infer_default_actions(state.opp_snakes);
    vector<EnemyReplyPolicy> policies(state.opp_snakes.size(), EnemyReplyPolicy::DefaultContinue);
    generate_tactical_opponent_reply_plans_dfs(state, 0, tactical, actions, policies, plans, cap);
    if (plans.empty()) {
        OpponentReplyPlan reply;
        reply.opp_actions = infer_default_actions(state.opp_snakes);
        reply.opp_policies.assign(reply.opp_actions.size(), EnemyReplyPolicy::DefaultContinue);
        plans.push_back(reply);
    }
    return plans;
}

static JointActionPlan choose_story81_baseline_plan(const SharedGameState& state) {
    JointActionPlan plan;
    plan.my_actions = infer_default_actions(state.my_snakes);
    plan.opp_actions = infer_default_actions(state.opp_snakes);

    for (size_t i = 0; i < state.my_snakes.size(); ++i) {
        const Snake& s = state.my_snakes[i];
        plan.my_actions[i] = first_legal_action_shared(state, s);
    }
    for (size_t i = 0; i < state.opp_snakes.size(); ++i) {
        const Snake& s = state.opp_snakes[i];
        int action = infer_previous_action(s);
        if (!is_action_locally_legal(state, s, action)) action = first_legal_action_shared(state, s);
        plan.opp_actions[i] = action;
    }
    return plan;
}

static vector<int> legal_actions_for_snake(const SharedGameState& state, const Snake& s) {
    vector<int> actions;
    int preferred = infer_previous_action(s);
    if (is_action_locally_legal(state, s, preferred)) actions.push_back(preferred);
    for (int action = 0; action < 4; ++action) {
        if (action == preferred) continue;
        if (is_action_locally_legal(state, s, action)) actions.push_back(action);
    }
    if (actions.empty()) actions.push_back(preferred);
    return actions;
}

static int next_position_for_action(const Snake& s, int action) {
    int head_pos = s.body[s.head_idx];
    int hx = head_pos % max_width;
    int hy = head_pos / max_width;
    int nx = hx + (action == 2 ? -1 : action == 3 ? 1 : 0);
    int ny = hy + (action == 0 ? -1 : action == 1 ? 1 : 0);
    return ny * max_width + nx;
}

static int nearest_enemy_head_distance(const SharedGameState& state, int from_pos, int* target_pos) {
    int best = INT_MAX;
    int best_pos = -1;
    for (const Snake& s : state.opp_snakes) {
        if (!s.is_alive || s.length <= 0) continue;
        int head = s.body[s.head_idx];
        int d = manhattan_dist_pos(from_pos, head);
        if (d < best) {
            best = d;
            best_pos = head;
        }
    }
    if (target_pos != nullptr) *target_pos = best_pos;
    return best;
}

static vector<int> collect_powerup_positions(const SharedGameState& state) {
    vector<int> powerups;
    for (int pos = 0; pos < grid_size; ++pos) {
        if (state.grid[pos] == CELL_POWERUP) powerups.push_back(pos);
    }
    return powerups;
}

static vector<int> collect_powerup_positions_on_side(const SharedGameState& state, bool left_side) {
    vector<int> powerups;
    int center_x = max_width / 2;
    for (int pos = 0; pos < grid_size; ++pos) {
        if (state.grid[pos] != CELL_POWERUP) continue;
        int x = pos % max_width;
        if ((left_side && x < center_x) || (!left_side && x >= center_x)) powerups.push_back(pos);
    }
    return powerups;
}

static int choose_action_toward_target(const SharedGameState& state, const Snake& s, int target_pos) {
    int best_action = first_legal_action_shared(state, s);
    int best_score = INT_MAX;
    for (int action : legal_actions_for_snake(state, s)) {
        int n_pos = next_position_for_action(s, action);
        int score = manhattan_dist_pos(n_pos, target_pos) * 20 - local_open_neighbor_count(state, n_pos) * 3;
        if (score < best_score) {
            best_score = score;
            best_action = action;
        }
    }
    return best_action;
}

static int choose_safe_spread_action(const SharedGameState& state, const Snake& s) {
    int best_action = first_legal_action_shared(state, s);
    int best_score = INT_MIN;
    for (int action : legal_actions_for_snake(state, s)) {
        int n_pos = next_position_for_action(s, action);
        int nearest_friend = INT_MAX;
        for (const Snake& other : state.my_snakes) {
            if (!other.is_alive || other.length <= 0 || other.id == s.id) continue;
            nearest_friend = min(nearest_friend, manhattan_dist_pos(n_pos, other.body[other.head_idx]));
        }
        if (nearest_friend == INT_MAX) nearest_friend = 4;
        int enemy_dist = nearest_enemy_head_distance(state, n_pos);
        if (enemy_dist == INT_MAX) enemy_dist = 6;
        int score = local_open_neighbor_count(state, n_pos) * 20 + min(6, nearest_friend) * 6 + min(6, enemy_dist) * 4;
        if (score > best_score) {
            best_score = score;
            best_action = action;
        }
    }
    return best_action;
}

static int choose_escape_action(const SharedGameState& state, const Snake& s) {
    int best_action = first_legal_action_shared(state, s);
    int best_score = INT_MIN;
    for (int action : legal_actions_for_snake(state, s)) {
        int n_pos = next_position_for_action(s, action);
        int enemy_dist = nearest_enemy_head_distance(state, n_pos);
        if (enemy_dist == INT_MAX) enemy_dist = 8;
        int score = local_open_neighbor_count(state, n_pos) * 18 + min(8, enemy_dist) * 7;
        if (score > best_score) {
            best_score = score;
            best_action = action;
        }
    }
    return best_action;
}

static int choose_pressure_action(const SharedGameState& state, const Snake& s) {
    int target_pos = -1;
    nearest_enemy_head_distance(state, s.body[s.head_idx], &target_pos);
    if (target_pos == -1) return choose_safe_spread_action(state, s);
    return choose_action_toward_target(state, s, target_pos);
}

static int choose_center_anchor_action(const SharedGameState& state, const Snake& s) {
    int center_pos = (max_height / 2) * max_width + (max_width / 2);
    return choose_action_toward_target(state, s, center_pos);
}

static vector<int> prioritize_snakes_for_family(const SharedGameState& state, int primary_idx = -1) {
    vector<pair<int, int>> ranked;
    for (size_t i = 0; i < state.my_snakes.size(); ++i) {
        const Snake& s = state.my_snakes[i];
        int urgency = 0;
        if (!s.is_alive || s.length <= 0) urgency = -1000;
        else {
            urgency += (count_legal_actions_for_snake(state, s) <= 2) ? 100 : 0;
            urgency += (static_cast<int>(i) == primary_idx) ? 80 : 0;
            urgency += s.length;
        }
        ranked.push_back({-urgency, static_cast<int>(i)});
    }
    sort(ranked.begin(), ranked.end());
    vector<int> order;
    for (const auto& entry : ranked) order.push_back(entry.second);
    return order;
}

static vector<int> coordinate_joint_actions(const SharedGameState& state,
                                            const vector<vector<int>>& action_preferences,
                                            const vector<int>& priority_order) {
    vector<int> chosen = infer_default_actions(state.my_snakes);
    unordered_set<int> reserved_positions;
    for (int idx : priority_order) {
        if (idx < 0 || idx >= static_cast<int>(state.my_snakes.size())) continue;
        const Snake& s = state.my_snakes[idx];
        if (!s.is_alive || s.length <= 0) continue;

        int fallback = first_legal_action_shared(state, s);
        int selected = fallback;
        bool found_non_conflict = false;
        for (int action : action_preferences[idx]) {
            if (!is_action_locally_legal(state, s, action)) continue;
            int n_pos = next_position_for_action(s, action);
            if (!reserved_positions.count(n_pos)) {
                selected = action;
                found_non_conflict = true;
                break;
            }
        }
        if (!found_non_conflict) {
            for (int action : legal_actions_for_snake(state, s)) {
                int n_pos = next_position_for_action(s, action);
                if (!reserved_positions.count(n_pos)) {
                    selected = action;
                    found_non_conflict = true;
                    break;
                }
            }
        }
        chosen[idx] = selected;
        reserved_positions.insert(next_position_for_action(s, selected));
    }
    return chosen;
}

static void append_unique_root_candidate(vector<FriendlyRootCandidate>& out, const FriendlyRootCandidate& candidate,
                                         unordered_set<uint64_t>& seen, size_t cap) {
    if (out.size() >= cap) return;
    uint64_t sig = encode_actions_signature(candidate.plan.my_actions);
    if (!seen.insert(sig).second) return;
    out.push_back(candidate);
}

static FriendlyRootCandidate build_family_candidate(const SharedGameState& state, RootActionFamily::Type family,
                                                    const vector<int>& primary_targets = {}) {
    FriendlyRootCandidate candidate;
    candidate.family = family;
    candidate.plan.my_actions = infer_default_actions(state.my_snakes);
    candidate.plan.opp_actions = infer_default_actions(state.opp_snakes);
    for (size_t i = 0; i < state.opp_snakes.size(); ++i) {
        candidate.plan.opp_actions[i] = first_legal_action_shared(state, state.opp_snakes[i]);
    }

    vector<vector<int>> action_preferences(state.my_snakes.size());
    int primary_idx = primary_targets.empty() ? -1 : primary_targets[0];
    for (size_t i = 0; i < state.my_snakes.size(); ++i) {
        const Snake& s = state.my_snakes[i];
        vector<int> prefs;
        if (!s.is_alive || s.length <= 0) {
            action_preferences[i] = prefs;
            continue;
        }

        if (family == RootActionFamily::DEFENSIVE_RECOVERY) {
            prefs.push_back(choose_escape_action(state, s));
            prefs.push_back(choose_safe_spread_action(state, s));
        } else if (family == RootActionFamily::SAFE_SPREAD) {
            prefs.push_back(choose_safe_spread_action(state, s));
            prefs.push_back(choose_center_anchor_action(state, s));
        } else if (family == RootActionFamily::CENTER_PRESSURE) {
            if (static_cast<int>(i) == primary_idx) prefs.push_back(choose_pressure_action(state, s));
            prefs.push_back(choose_center_anchor_action(state, s));
            prefs.push_back(choose_safe_spread_action(state, s));
        } else if (family == RootActionFamily::ANCHOR_AND_FORAGE) {
            if (static_cast<int>(i) == primary_idx) prefs.push_back(choose_center_anchor_action(state, s));
            else if (i + 1 < primary_targets.size() && primary_targets[i + 1] >= 0) prefs.push_back(choose_action_toward_target(state, s, primary_targets[i + 1]));
            prefs.push_back(choose_safe_spread_action(state, s));
        } else if (family == RootActionFamily::SPLIT_HARVEST || family == RootActionFamily::CONTEST_LEFT_CLUSTER || family == RootActionFamily::CONTEST_RIGHT_CLUSTER) {
            if (i + 1 < primary_targets.size() && primary_targets[i + 1] >= 0) prefs.push_back(choose_action_toward_target(state, s, primary_targets[i + 1]));
            prefs.push_back(choose_safe_spread_action(state, s));
        } else {
            prefs.push_back(first_legal_action_shared(state, s));
            prefs.push_back(choose_safe_spread_action(state, s));
        }

        vector<int> legal = legal_actions_for_snake(state, s);
        for (int action : legal) prefs.push_back(action);

        vector<int> deduped;
        bool seen_action[4] = {false, false, false, false};
        for (int action : prefs) {
            if (action < 0 || action >= 4) continue;
            if (seen_action[action]) continue;
            seen_action[action] = true;
            deduped.push_back(action);
        }
        for (int action : legal) {
            if (action < 0 || action >= 4) continue;
            if (seen_action[action]) continue;
            seen_action[action] = true;
            deduped.push_back(action);
        }
        action_preferences[i] = deduped;
    }

    candidate.plan.my_actions = coordinate_joint_actions(state, action_preferences, prioritize_snakes_for_family(state, primary_idx));
    return candidate;
}

static vector<int> assign_distinct_powerup_targets(const SharedGameState& state, const vector<int>& powerups) {
    vector<int> assigned(state.my_snakes.size(), -1);
    vector<bool> used(powerups.size(), false);
    for (size_t iter = 0; iter < state.my_snakes.size(); ++iter) {
        int best_snake = -1;
        int best_power_idx = -1;
        int best_dist = INT_MAX;
        for (size_t i = 0; i < state.my_snakes.size(); ++i) {
            const Snake& s = state.my_snakes[i];
            if (!s.is_alive || s.length <= 0 || assigned[i] != -1) continue;
            int head = s.body[s.head_idx];
            for (size_t p = 0; p < powerups.size(); ++p) {
                if (used[p]) continue;
                int d = manhattan_dist_pos(head, powerups[p]);
                if (d < best_dist) {
                    best_dist = d;
                    best_snake = static_cast<int>(i);
                    best_power_idx = static_cast<int>(p);
                }
            }
        }
        if (best_snake == -1 || best_power_idx == -1) break;
        assigned[best_snake] = powerups[best_power_idx];
        used[best_power_idx] = true;
    }
    return assigned;
}

static vector<FriendlyRootCandidate> generate_team_root_candidates(const SharedGameState& state, size_t cap) {
    vector<FriendlyRootCandidate> out;
    unordered_set<uint64_t> seen;

    FriendlyRootCandidate baseline;
    baseline.family = RootActionFamily::DEFAULT_CONTINUE;
    baseline.plan = choose_story81_baseline_plan(state);
    append_unique_root_candidate(out, baseline, seen, cap);

    vector<int> all_powerups = collect_powerup_positions(state);
    vector<int> left_powerups = collect_powerup_positions_on_side(state, true);
    vector<int> right_powerups = collect_powerup_positions_on_side(state, false);

    append_unique_root_candidate(out, build_family_candidate(state, RootActionFamily::SAFE_SPREAD), seen, cap);
    append_unique_root_candidate(out, build_family_candidate(state, RootActionFamily::DEFENSIVE_RECOVERY), seen, cap);

    if (!all_powerups.empty()) {
        vector<int> assigned_harvest = assign_distinct_powerup_targets(state, all_powerups);
        append_unique_root_candidate(out, build_family_candidate(state, RootActionFamily::SPLIT_HARVEST, assigned_harvest), seen, cap);

        vector<int> anchor_targets(state.my_snakes.size() + 1, -1);
        int center_pos = (world_height / 2 + max_len) * max_width + (world_width / 2 + max_len);
        int anchor_idx = 0;
        int best_center = INT_MAX;
        for (size_t i = 0; i < state.my_snakes.size(); ++i) {
            const Snake& s = state.my_snakes[i];
            if (!s.is_alive || s.length <= 0) continue;
            int dist = manhattan_dist_pos(s.body[s.head_idx], center_pos);
            if (dist < best_center) {
                best_center = dist;
                anchor_idx = static_cast<int>(i);
            }
        }
        anchor_targets[0] = anchor_idx;
        for (size_t i = 0; i < assigned_harvest.size(); ++i) anchor_targets[i + 1] = assigned_harvest[i];
        anchor_targets[anchor_idx + 1] = -1;
        append_unique_root_candidate(out, build_family_candidate(state, RootActionFamily::ANCHOR_AND_FORAGE, anchor_targets), seen, cap);
    }

    if (!left_powerups.empty()) {
        vector<int> assigned_left = assign_distinct_powerup_targets(state, left_powerups);
        vector<int> left_targets(state.my_snakes.size() + 1, -1);
        for (size_t i = 0; i < assigned_left.size(); ++i) left_targets[i + 1] = assigned_left[i];
        append_unique_root_candidate(out, build_family_candidate(state, RootActionFamily::CONTEST_LEFT_CLUSTER, left_targets), seen, cap);
    }
    if (!right_powerups.empty()) {
        vector<int> assigned_right = assign_distinct_powerup_targets(state, right_powerups);
        vector<int> right_targets(state.my_snakes.size() + 1, -1);
        for (size_t i = 0; i < assigned_right.size(); ++i) right_targets[i + 1] = assigned_right[i];
        append_unique_root_candidate(out, build_family_candidate(state, RootActionFamily::CONTEST_RIGHT_CLUSTER, right_targets), seen, cap);
    }

    int striker_idx = -1;
    int best_enemy_dist = INT_MAX;
    for (size_t i = 0; i < state.my_snakes.size(); ++i) {
        const Snake& s = state.my_snakes[i];
        if (!s.is_alive || s.length <= 0) continue;
        int d = nearest_enemy_head_distance(state, s.body[s.head_idx]);
        if (d < best_enemy_dist) {
            best_enemy_dist = d;
            striker_idx = static_cast<int>(i);
        }
    }
    if (striker_idx != -1) {
        append_unique_root_candidate(out, build_family_candidate(state, RootActionFamily::CENTER_PRESSURE, {striker_idx}), seen, cap);
    }

    return out;
}

static vector<JointActionPlan> generate_structured_followup_candidates(const SharedGameState& state, size_t cap) {
    vector<JointActionPlan> plans;
    vector<FriendlyRootCandidate> candidates = generate_team_root_candidates(state, cap);
    for (const FriendlyRootCandidate& candidate : candidates) {
        plans.push_back(candidate.plan);
        if (plans.size() >= cap) break;
    }
    return plans;
}

static vector<JointActionPlan> generate_story82_root_candidates(const SharedGameState& state, size_t cap) {
    return generate_structured_followup_candidates(state, cap);
}

static vector<BeamNode> build_story81_root_nodes(const SharedGameState& root_state) {
    vector<BeamNode> nodes;
    JointActionPlan baseline = choose_story81_baseline_plan(root_state);
    SharedGameState next_state = root_state;
    next_state.simulate_joint_actions(baseline);

    BeamNode node;
    node.state = std::move(next_state);
    node.state_hash = node.state.full_state_hash();
    node.depth = 1;
    node.root_family = RootActionFamily::LEGALIZED_CONTINUE;
    nodes.push_back(std::move(node));
    return nodes;
}

static JointActionPlan choose_story82_best_plan(const SharedGameState& root_state, EvaluatorBreakdown& best_eval,
                                                SharedGameState& best_next_state, RootActionFamily::Type& best_family,
                                                OpponentReplyPlan& best_reply,
                                                HotspotSummary& best_hotspot) {
    HotspotSummary root_hotspot = analyze_hotspots(root_state);
    size_t candidate_cap = root_hotspot.is_hotspot ? 48 : 24;
    vector<JointActionPlan> candidates = generate_story82_root_candidates(root_state, candidate_cap);
    JointActionPlan fallback = choose_story81_baseline_plan(root_state);

    bool found = false;
    int best_score = INT_MIN;
    best_family = RootActionFamily::LEGALIZED_CONTINUE;
    best_reply.opp_actions = fallback.opp_actions;
    best_reply.opp_policies.assign(best_reply.opp_actions.size(), EnemyReplyPolicy::DefaultContinue);

    vector<OpponentReplyPlan> reply_candidates = generate_opponent_reply_plans(root_state, root_hotspot, root_hotspot.is_hotspot ? 18 : 10);
    if (reply_candidates.empty()) {
        OpponentReplyPlan reply;
        reply.opp_actions = fallback.opp_actions;
        reply.opp_policies.assign(reply.opp_actions.size(), EnemyReplyPolicy::DefaultContinue);
        reply_candidates.push_back(reply);
    }

    for (const JointActionPlan& plan : candidates) {
        int worst_reply_score = INT_MAX;
        EvaluatorBreakdown worst_eval;
        SharedGameState worst_state;
        OpponentReplyPlan worst_reply_local;

        for (const OpponentReplyPlan& reply : reply_candidates) {
            SharedGameState next_state = root_state;
            JointActionPlan combined = plan;
            combined.opp_actions = reply.opp_actions;
            next_state.simulate_joint_actions(combined);
            EvaluatorBreakdown eval = evaluate_shared_state(next_state);
            if (eval.total_score < worst_reply_score) {
                worst_reply_score = eval.total_score;
                worst_eval = eval;
                worst_state = std::move(next_state);
                worst_reply_local = reply;
            }
        }

        if (!found || worst_reply_score > best_score) {
            found = true;
            best_score = worst_reply_score;
            best_eval = worst_eval;
            best_next_state = std::move(worst_state);
            best_family = RootActionFamily::PLACEHOLDER_SHARED_ROOT;
            best_reply = worst_reply_local;
            best_hotspot = analyze_hotspots(best_next_state);
            fallback = plan;
        }
    }

    if (!found) {
        best_next_state = root_state;
        best_next_state.simulate_joint_actions(fallback);
        best_eval = evaluate_shared_state(best_next_state);
        best_hotspot = analyze_hotspots(best_next_state);
    }

    return fallback;
}

static SharedGameState read_turn_state(int power_source_count, const vector<int>& my_snakebots, const vector<int16_t>& static_walls) {
    SharedGameState state;
    state.grid = static_walls;

    for (int i = 0; i < power_source_count; ++i) {
        int x = 0;
        int y = 0;
        cin >> x >> y;
        int sx = max_len + x;
        int sy = max_len + y;
        int pos = sy * max_width + sx;
        state.grid[pos] = CELL_POWERUP;
    }

    int snakebot_count = 0;
    cin >> snakebot_count;
    for (int i = 0; i < snakebot_count; ++i) {
        int snakebot_id = -1;
        string body_str;
        cin >> snakebot_id >> body_str;

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
        s.owner = is_mine ? 0 : 1;
        if (is_mine) state.my_snakes.push_back(s);
        else state.opp_snakes.push_back(s);
    }

    return state;
}

static vector<int16_t> build_static_walls(const vector<string>& initial_rows) {
    vector<int16_t> grid(grid_size, CELL_EMPTY);
    int start_x = max_len;
    int start_y = max_len;
    for (int y = 0; y < world_height; ++y) {
        for (int x = 0; x < world_width; ++x) {
            if (initial_rows[y][x] == '#') {
                grid[(start_y + y) * max_width + (start_x + x)] = CELL_WALL;
            }
        }
    }
    return grid;
}

int main() {
    signal(SIGSEGV, crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);
    signal(SIGFPE, crash_signal_handler);
    signal(SIGILL, crash_signal_handler);
    std::set_terminate(bot_terminate_handler);

    int my_id = 0;
    cin >> my_id;
    cin >> world_width;
    cin >> world_height;

    vector<string> initial_rows(world_height);
    cin >> ws;
    for (int i = 0; i < world_height; ++i) getline(cin, initial_rows[i]);

    int snakebots_per_player = 0;
    cin >> snakebots_per_player;
    vector<int> my_snakebots;
    my_snakebots.reserve(snakebots_per_player);
    for (int i = 0; i < snakebots_per_player; ++i) {
        int id = -1;
        cin >> id;
        my_snakebots.push_back(id);
    }
    for (int i = 0; i < snakebots_per_player; ++i) {
        int ignored = -1;
        cin >> ignored;
    }

    total_powerups_count = 0;
    bool first_turn = true;
    vector<int16_t> static_walls;

    while (true) {
        turn_start_time = high_resolution_clock::now();
        ++g_turn_counter;

        int power_source_count = 0;
        if (!(cin >> power_source_count)) break;

        if (first_turn) {
            total_powerups_count = power_source_count;
            max_len = 3 + total_powerups_count;
            max_width = (2 * max_len) + world_width;
            max_height = (2 * max_len) + world_height;
            grid_size = max_width * max_height;
            static_walls = build_static_walls(initial_rows);
            first_turn = false;
        }

        SharedGameState root_state = read_turn_state(power_source_count, my_snakebots, static_walls);
        BeamSearchResult beam_result = choose_story85_beam_plan(root_state);
        EvaluatorBreakdown best_eval = beam_result.best_eval;
        SharedGameState best_next_state = beam_result.best_next_state;
        RootActionFamily::Type chosen_family = beam_result.best_family;
        OpponentReplyPlan chosen_reply = beam_result.best_reply;
        HotspotSummary chosen_hotspot = beam_result.best_hotspot;
        JointActionPlan chosen_plan = beam_result.chosen_plan;
        vector<BeamNode> root_nodes = beam_result.final_frontier;

        vector<string> commands;
        for (size_t i = 0; i < root_state.my_snakes.size(); ++i) {
            const Snake& s = root_state.my_snakes[i];
            int action = (i < chosen_plan.my_actions.size()) ? chosen_plan.my_actions[i] : infer_previous_action(s);
            commands.push_back(to_string(s.id) + " " + action_to_string(action));
        }

        string output;
        for (size_t i = 0; i < commands.size(); ++i) {
            if (i) output += ";";
            output += commands[i];
        }
        if (output.empty()) output = "WAIT";

        uint64_t root_hash = root_state.full_state_hash();
        uint64_t next_hash = root_nodes.empty() ? 0ULL : root_nodes.front().state_hash;
          mylog << "EPIC8_STORY811"
              << " turn=" << g_turn_counter
              << " my_snakes=" << root_state.my_snakes.size()
              << " opp_snakes=" << root_state.opp_snakes.size()
              << " hot_enemy_count=" << chosen_reply.hot_enemy_count
              << " hotspot=" << (chosen_hotspot.is_hotspot ? 1 : 0)
              << " hotspot_score=" << chosen_hotspot.score
              << " hotspot_reasons=" << (chosen_hotspot.reasons.empty() ? "NONE" : chosen_hotspot.reasons)
              << " my_total_len=" << root_state.total_team_length(0)
              << " opp_total_len=" << root_state.total_team_length(1)
              << " root_hash=" << root_hash
              << " next_hash=" << next_hash
              << " root_nodes=" << root_nodes.size()
              << " completed_depth=" << beam_result.completed_depth
              << " expanded_nodes=" << beam_result.expanded_nodes
              << " chosen_family=" << root_family_to_string(chosen_family)
              << " chosen_bucket=" << beam_bucket_to_string(beam_result.best_bucket)
              << " winner_retention=" << beam_result.winner_retention_reason
              << " diversity_preserved_winner=" << (beam_result.diversity_preserved_winner ? 1 : 0)
              << " winner_reason=" << beam_result.winner_over_runner_up_reason
              << " terminal_score=" << best_eval.terminal_score
              << " length_delta=" << best_eval.length_delta
              << " loss_risk_penalty=" << best_eval.loss_risk_penalty
              << " mobility_delta=" << best_eval.mobility_delta
              << " territory_delta=" << best_eval.territory_delta
              << " apple_ownership_delta=" << best_eval.apple_ownership_delta
              << " head_danger_penalty=" << best_eval.head_danger_penalty
              << " corridor_trap_penalty=" << best_eval.corridor_trap_penalty
              << " coordination_penalty=" << best_eval.coordination_penalty
              << " total_score=" << best_eval.total_score
              << " opp_reply_policies=" << build_reply_policy_string(chosen_reply)
              << " best_eval_breakdown=" << build_eval_breakdown_string(best_eval)
              << " elapsed_ms=" << elapsed_turn_ms()
              << " beam_diag=" << beam_result.diagnostics
              << " output=" << output
              << '\n';
        mylog.flush();

        cout << output << endl;
    }

    return 0;
}
