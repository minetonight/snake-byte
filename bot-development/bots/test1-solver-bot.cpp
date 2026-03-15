#include <algorithm>
#include <thread>
#include <array>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include <random>
using namespace std;
using namespace std::chrono;

int main() {
    /**
    * Auto-generated code below aims at helping you parse
    * the standard input according to the problem statement.
    **/
    int my_id;
    cin >> my_id;
    cin.ignore();
    int width;
    cin >> width;
    cin.ignore();
    int height;
    cin >> height;
    cin.ignore();
    for (int i = 0; i < height; i++) {
        string row;
        getline(cin, row);
    }
    int snakebots_per_player;
    cin >> snakebots_per_player;
    cin.ignore();
    int my_snakebot_id;
    for (int i = 0; i < snakebots_per_player; i++) {
        cin >> my_snakebot_id;
        cin.ignore();
    }
    int opp_snakebot_id;
    for (int i = 0; i < snakebots_per_player; i++) {
        cin >> opp_snakebot_id;
        cin.ignore();
    }
    
    array<string, 12> commands = {
        "LEFT", 
        "DOWN", 
        "LEFT", 
        "UP", 
        "UP", 
        "LEFT", 
        "LEFT", 
        "UP", 
        "UP", 
        "UP", 
        "UP", 
        "UP"};
        
    int counter = 0;
    // game loop

    // simulate load

    const int MATRIX_SIZE = 64;  // 64x64 = 4096 elements, heavy but fast
    vector<vector<double>> matA(MATRIX_SIZE, vector<double>(MATRIX_SIZE));
    vector<vector<double>> matB(MATRIX_SIZE, vector<double>(MATRIX_SIZE));
    vector<vector<double>> result(MATRIX_SIZE, vector<double>(MATRIX_SIZE));
    
    // Initialize with random data (prevents compiler optimization)
    mt19937_64 rng(42);
    uniform_real_distribution<double> dist(0.0, 1.0);
    
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        for (int j = 0; j < MATRIX_SIZE; ++j) {
            matA[i][j] = dist(rng);
            matB[i][j] = dist(rng);
        }
    }
    
    auto loop_start = high_resolution_clock::now();  // Global loop start
    while (1) {
        auto iter_start = high_resolution_clock::now();  // Per-iteration start
        
        int power_source_count;
        cin >> power_source_count;
        cin.ignore();
        for (int i = 0; i < power_source_count; i++) {
            int x;
            int y;
            cin >> x >> y;
            cin.ignore();
        }
        int snakebot_count;
        cin >> snakebot_count;
        cin.ignore();
        for (int i = 0; i < snakebot_count; i++) {
            int snakebot_id;
            string body;
            cin >> snakebot_id >> body;
            cin.ignore();
        }
        
        // Write an action using cout. DON'T FORGET THE "<< endl"
        // To debug: cerr << "Debug messages..." << endl;

        auto iter_elapsed = microseconds(0);
        for (int iter = 0; iter < 40; ++iter) {
            
            // HEAVY: Triple nested matrix multiplication (64^3 = ~260k operations)
            for (int i = 0; i < MATRIX_SIZE; ++i) {
                for (int j = 0; j < MATRIX_SIZE; ++j) {
                    double sum = 0.0;
                    iter_elapsed = duration_cast<microseconds>(high_resolution_clock::now() - iter_start);
                    if(iter_elapsed.count() > 73000) { // the real threshold for codingame site
                        break;
                    }
                    for (int k = 0; k < MATRIX_SIZE; ++k) {
                        sum += matA[i][k] * matB[k][j];
                    }
                    result[i][j] = sum;
                }
            }
        }

        // Time passed in this iteration so far
        iter_elapsed = duration_cast<microseconds>(high_resolution_clock::now() - iter_start);
        cerr << iter_elapsed.count() << "μs" << endl;
        cout << my_snakebot_id << " " << commands[counter]<< endl;
        counter++;
        counter = (counter + 1) % commands.size();
    }
}