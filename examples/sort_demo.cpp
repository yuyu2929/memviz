// sort_demo.cpp — minimal memviz usage: watch bubble sort touch cells.
//
// Build (from the memviz package root):
//   g++ -std=c++17 -pthread -Iinclude examples/sort_demo.cpp -o sort_demo
// Run:
//   ./sort_demo
#include <chrono>
#include <thread>

#include <memviz/memviz.hpp>

int main() {
    using namespace memviz;
    using namespace std::chrono_literals;

    Visualizer viz;                       // RAII: renderer lives on its own thread
    auto a = viz.array1d<int>("bubble", 16);

    for (int i = 0; i < 16; ++i) a.set(i, (i * 11) % 37);

    for (int i = 0; i < 15; ++i) {
        for (int j = 0; j < 15 - i; ++j) {
            a.access(j);                  // cells touched by this compare
            a.access(j + 1);
            a.cursor(1, j, "cmp");        // magnifier follows the comparison
            if (a.get(j) > a.get(j + 1)) {
                int t = a.get(j);
                a.set(j, a.get(j + 1));
                a.set(j + 1, t);
            }
            std::this_thread::sleep_for(40ms);
        }
    }
    return 0;  // ~Visualizer stops the renderer thread and clears the screen
}
