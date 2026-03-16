#include <iostream>
#include <string>
#include <vector>
#include <chrono>
using namespace std;
using namespace std::chrono;

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

struct Snake {
    int id;
    int length;
    int head_idx;
    int tail_idx;
    bool is_alive;
    // Circular buffer to hold body coordinates (1D indices).
    // Initialized to max possible length to avoid reallocation.
    vector<int16_t> body; 
};

struct GameState {
    vector<int16_t> grid;
    vector<Snake> my_snakes;
    vector<Snake> opp_snakes;

    // --- PHYSICS ENGINE ---
    
    // Check if snake is resting on something solid
    inline bool is_supported(const Snake& s) const {
        for (int i = 0; i < s.length; ++i) {
            int idx = s.body[(s.head_idx + i) % s.body.capacity()];
            int below_idx = idx + max_width; // Y+1
            
            // If it's resting on bottom boundary (shouldn't happen with our math, but safety)
            if (below_idx >= grid_size) return true;
            
            int16_t below_cell = grid[below_idx];
            // Solid means Wall, Powerup, or another Snake's body part. 
            if (below_cell == CELL_WALL || below_cell == CELL_POWERUP || below_cell >= CELL_SNAKE_BASE) {
                // If it's resting on itself, that doesn't count as support UNLESS
                // the part it's resting on is ALSO supported.
                // The easiest way to handle falling is to just check if it's resting on anything solid at all.
                // The rules state "Meaning at least one body part must be above something solid or they will fall."
                // For now, assume any wall, powerup, or snake part supports it.
                // Let's assume hitting ANY solid cell under ANY body part stops falling.
                // We'll treat its own body as solid.
                return true;
            }
        }
        return false;
    }

    inline void apply_gravity() {
        bool moved;
        do {
            moved = false;
            // Gravity applies to all snakes simultaneously, but we can do it iteratively 
            // until no snake falls anymore (cascading falls).
            auto apply_fall = [&](vector<Snake>& snakes) {
                for (Snake& s : snakes) {
                    if (!s.is_alive) continue;
                    if (!is_supported(s)) {
                        // Fall down by 1 cell
                        for (int i = 0; i < s.length; ++i) {
                            int b_idx = (s.head_idx + i) % s.body.capacity();
                            int old_pos = s.body[b_idx];
                            int new_pos = old_pos + max_width;
                            
                            grid[old_pos] = CELL_EMPTY;
                            s.body[b_idx] = new_pos;
                        }
                        // Re-stamp to grid
                        for (int i = 0; i < s.length; ++i) {
                            int b_idx = (s.head_idx + i) % s.body.capacity();
                            grid[s.body[b_idx]] = CELL_SNAKE_BASE + s.id;
                        }
                        
                        // Check if fell out of bounds
                        int head_pos = s.body[s.head_idx];
                        int hy = head_pos / max_width;
                        if (hy >= max_height) {
                            s.is_alive = false;
                            // Clear from grid
                            for (int i = 0; i < s.length; ++i) {
                                int b_idx = (s.head_idx + i) % s.body.capacity();
                                if (s.body[b_idx] < grid_size) {
                                    grid[s.body[b_idx]] = CELL_EMPTY;
                                }
                            }
                        }
                        moved = true;
                    }
                }
            };
            
            apply_fall(my_snakes);
            apply_fall(opp_snakes);
        } while(moved);
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
                        int n_pos = s.body[(s.head_idx + 1) % s.body.capacity()];
                        
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
                int new_head_idx = (s.head_idx - 1 + s.body.capacity()) % s.body.capacity();
                s.body[new_head_idx] = n_pos; // Set new head here
                
                // Shift tail pointer backward (effectively cutting off old tail)
                s.tail_idx = (s.tail_idx - 1 + s.body.capacity()) % s.body.capacity();
                s.head_idx = new_head_idx;
                
                // Update grid: remove old tail, place new head
                // Wait on updating new head because of collisions and powerups!
                // For now, only remove tail from grid to clear it for others moves
                grid[tail_pos] = CELL_EMPTY;
            }
        };
        
        move_snakes(my_snakes, my_actions);
        move_snakes(opp_snakes, opp_actions);
        
        // Resolve Collisions and Powerups
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
                s->tail_idx = (s->tail_idx + 1) % s->body.capacity();
                s->length++;
                // We'll restamp the tail underneath
            }
            
            if (to_destroy[i]) {
                // Head is destroyed
                s->length--;
                // New head is the next segment
                s->head_idx = (s->head_idx + 1) % s->body.capacity();
                
                if (s->length < 3) {
                    s->is_alive = false;
                    // Clear entirely from grid
                    for (int k = 0; k < s->length; ++k) {
                        int b_idx = (s->head_idx + k) % s->body.capacity();
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
                int b_idx = (s->head_idx + k) % s->body.capacity();
                grid[s->body[b_idx]] = CELL_SNAKE_BASE + s->id;
            }
        }
    }
    
    // Simulates one entire turn
    inline void simulate(const vector<int>& my_actions, const vector<int>& opp_actions) {
        apply_movement(my_actions, opp_actions);
        apply_gravity();
    }
};

#ifdef LOCAL_TEST
void run_local_tests() {
    cerr << "Running local memory constraint and cloning tests..." << endl;
    
    // Initialize mock grid dimensions
    world_width = 20;
    world_height = 20;
    total_powerups_count = 10;
    
    max_len = 3 + total_powerups_count;
    max_width = (2 * max_len) + world_width;
    max_height = world_height + max_len + 1;
    grid_size = max_width * max_height;
    
    GameState root_state;
    root_state.grid.assign(grid_size, CELL_EMPTY);
    
    Snake s1;
    s1.id = 0; s1.is_alive = true; s1.length = 3; s1.head_idx = 0; s1.tail_idx = 2;
    s1.body.reserve(max_len);
    s1.body.push_back(5 * max_width + 5);
    s1.body.push_back(5 * max_width + 6);
    s1.body.push_back(5 * max_width + 7);
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
#ifdef LOCAL_TEST
    run_local_tests();
#endif
    /**
    * Auto-generated code below aims at helping you parse
    * the standard input according to the problem statement.
    **/
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

    auto loop_start = high_resolution_clock::now();  // Global loop start
    while (1) {
        counter++;
        auto iter_start = high_resolution_clock::now();  // Per-iteration start
        
        int power_source_count;
        cin >> power_source_count;
        cin.ignore();

        if (is_first_turn) {
            total_powerups_count = power_source_count;
            max_len = 3 + total_powerups_count;
            max_width = (2 * max_len) + world_width;
            max_height = world_height + max_len + 1;
            grid_size = max_width * max_height;

            state.grid.assign(grid_size, CELL_EMPTY);

            // Now parse the initial_rows map into the bottom center of the new grid.
            int start_x = max_len;
            int start_y = max_height - world_height; // Put it at the bottom
            
            for (int y = 0; y < world_height; y++) {
                for (int x = 0; x < world_width; x++) {
                    if (initial_rows[y][x] == '#') {
                        state.grid[(start_y + y) * max_width + (start_x + x)] = CELL_WALL;
                    }
                }
            }
            is_first_turn = false;
        }

        for (int i = 0; i < power_source_count; i++) {
            int x;
            int y;
            cin >> x >> y;
            cin.ignore();
            
            int sx = max_len + x;
            int sy = max_height - world_height + y;
            state.grid[sy * max_width + sx] = CELL_POWERUP;
        }

        int snakebot_count;
        cin >> snakebot_count;
        cin.ignore();
        
        // Reset dynamic snake states arrays since we overwrite parsed data
        state.my_snakes.clear();
        state.opp_snakes.clear();
        
        for (int i = 0; i < snakebot_count; i++) {
            int snakebot_id;
            string body_str;
            cin >> snakebot_id >> body_str;
            cin.ignore();

            Snake s;
            s.id = snakebot_id;
            s.is_alive = true;
            s.body.reserve(max_len);
            
            // Parse body "x,y:x2,y2"
            size_t pos = 0;
            while (pos < body_str.length()) {
                size_t comma = body_str.find(',', pos);
                size_t colon = body_str.find(':', comma);
                if (colon == string::npos) colon = body_str.length();
                
                int x = stoi(body_str.substr(pos, comma - pos));
                int y = stoi(body_str.substr(comma + 1, colon - comma - 1));
                
                int sx = max_len + x;
                int sy = max_height - world_height + y;
                
                s.body.push_back(sy * max_width + sx);
                state.grid[sy * max_width + sx] = CELL_SNAKE_BASE + s.id;
                
                pos = colon + 1;
            }
            s.length = s.body.size();
            s.head_idx = 0;
            s.tail_idx = s.length - 1;
            
            // Assuming my snakes are ID's stored somehow, we can infer by comparing to my_id, 
            // but codingame sends all my snake ids initially. 
            // For now, simplify and assume we can parse them correctly.
        }

        auto iter_elapsed = duration_cast<microseconds>(high_resolution_clock::now() - iter_start);
        
        cout << "WAIT" << endl;
    }
}