// memviz tests — dependency-free, runs as a plain executable (CTest).
//
//   g++ -std=c++17 -pthread -Iinclude tests/memviz_test.cpp -o memviz_tests
//   ./memviz_tests
//
// Every test uses a silent recording backend instead of the terminal, so the
// suite produces no escape codes and can assert on the exact snapshot a
// backend would have received.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <memviz/memviz.hpp>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                     \
    do {                                                                   \
        ++g_checks;                                                        \
        auto _va = (a);                                                    \
        auto _vb = (b);                                                    \
        if (!(_va == _vb)) {                                               \
            std::printf("FAIL %s:%d  %s == %s\n", __FILE__, __LINE__, #a,  \
                        #b);                                               \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

// Records the last snapshot a backend was asked to render.
struct Recorder : memviz::Backend {
    memviz::Snapshot last;
    int frames = 0;
    void frame(const memviz::Snapshot& s) override {
        last = s;
        ++frames;
    }
};

std::string tmpPath(const char* name) {
    const char* dir = std::getenv("TMPDIR");
    if (!dir) dir = std::getenv("TEMP");
    if (!dir) dir = "/tmp";
    return std::string(dir) + "/memviz_test_" + name;
}

// ---------------------------------------------------------------------------

void test_1d_basic() {
    Recorder rec;
    memviz::Visualizer viz(&rec, false);
    auto a = viz.array1d<int>("signal", 10);
    a.set(0, 42);
    a.set(9, -7);
    CHECK_EQ(a.get(0), 42);
    CHECK_EQ(a.get(9), -7);
    CHECK_EQ(a.get(100), 0);           // out of range reads as 0, no crash
    a.set(100, 5);                     // out of range write is ignored
    auto snap = viz.snapshot();
    CHECK_EQ(snap.arrays.size(), size_t(1));
    CHECK_EQ(snap.arrays[0].cells.size(), size_t(10));
    CHECK_EQ(snap.arrays[0].cells[0].value, 42.0);
}

void test_access_marks_cell() {
    Recorder rec;
    memviz::Visualizer viz(&rec, false);
    auto a = viz.array1d<int>("x", 4);
    CHECK(!viz.snapshot().arrays[0].cells[2].accessed);
    a.access(2);
    auto snap = viz.snapshot();
    CHECK(snap.arrays[0].cells[2].accessed);
    CHECK(snap.arrays[0].cells[2].accessTick > 0);
    CHECK(!snap.arrays[0].cells[1].accessed);
}

void test_cursor_active_is_last_moved() {
    Recorder rec;
    memviz::Visualizer viz(&rec, false);
    auto a = viz.array1d<int>("x", 8);
    a.set(1, 11);
    a.set(5, 55);

    a.cursor(0, 1, "first");
    a.cursor(1, 5, "second");
    auto snap = viz.snapshot();
    CHECK_EQ(snap.arrays[0].cursors.size(), size_t(2));
    CHECK_EQ(snap.arrays[0].active, size_t(1));      // second was moved last

    // Moving the FIRST cursor again must make it the active one. The old
    // implementation always highlighted the last vector element.
    a.cursor(0, 1, "first");
    snap = viz.snapshot();
    CHECK_EQ(snap.arrays[0].active, size_t(0));

    memviz::TerminalBackend tb(false);
    std::string out = tb.renderArray(snap.arrays[0]);
    // active cursor renders as [v], the other as {v}
    CHECK(out.find("[ 11 ]") != std::string::npos || out.find("[ 11]") != std::string::npos ||
          out.find("[11]") != std::string::npos);
    CHECK(out.find("{") != std::string::npos);
}

void test_resize_1d() {
    Recorder rec;
    memviz::Visualizer viz(&rec, false);
    auto a = viz.array1d<int>("x", 4);
    a.set(0, 7);
    a.resize(10);
    CHECK_EQ(viz.snapshot().arrays[0].cells.size(), size_t(10));
    CHECK_EQ(a.get(0), 7);              // values survive growth
    a.resize(2);
    CHECK_EQ(viz.snapshot().arrays[0].cells.size(), size_t(2));
}

void test_2d_basic() {
    Recorder rec;
    memviz::Visualizer viz(&rec, false);
    auto m = viz.array2d<float>("heat", 3, 4);
    m.set(2 * 4 + 1, 1.5f);             // row 2, col 1
    CHECK_EQ(m.get(9), 1.5f);
    auto snap = viz.snapshot();
    CHECK_EQ(snap.arrays[0].kind, memviz::Kind::Matrix2D);
    CHECK_EQ(snap.arrays[0].cells.size(), size_t(12));
    CHECK_EQ(snap.arrays[0].dims.size(), size_t(2));
}

void test_2d_resize_to_1d_is_safe() {
    // Regression: resizing a 2D array to one dimension left kind == Matrix2D
    // with a single dim, and the renderer indexed dims[1] out of range.
    Recorder rec;
    memviz::Visualizer viz(&rec, false);
    auto m = viz.array2d<float>("heat", 3, 4);
    m.resize(6);                        // 1D now
    auto snap = viz.snapshot();
    CHECK_EQ(snap.arrays[0].kind, memviz::Kind::Bar1D);
    CHECK_EQ(snap.arrays[0].cells.size(), size_t(6));
    memviz::TerminalBackend tb(false);
    std::string out = tb.renderArray(snap.arrays[0]);   // must not read dims[1]
    CHECK(out.find("(1D") != std::string::npos);
}

void test_render_1d_2d() {
    Recorder rec;
    memviz::Visualizer viz(&rec, false);
    auto a = viz.array1d<int>("bar", 5);
    a.set(2, 33);
    memviz::TerminalBackend tb(false);
    auto snap = viz.snapshot();
    std::string s1 = tb.renderArray(snap.arrays[0]);
    CHECK(s1.find("[bar]") != std::string::npos);
    CHECK(s1.find("33") != std::string::npos);

    auto m = viz.array2d<int>("mat", 2, 3);
    m.set(0, 1);
    auto snap2 = viz.snapshot();
    std::string s2 = tb.renderArray(snap2.arrays[1]);
    CHECK(s2.find("(2D  2x3)") != std::string::npos);
}

void test_log_replay_roundtrip() {
    const std::string path = tmpPath("roundtrip.bin");
    std::remove(path.c_str());

    {
        Recorder rec;
        memviz::Visualizer viz(&rec, false);
        viz.log_to(path);               // first, so everything is recorded
        auto a = viz.array1d<int>("sig", 6);
        a.set(1, 10);
        a.set(3, 30);
        a.access(3);
        a.cursor(0, 3, "scan");
        a.resize(8);
        a.set(7, 70);
    }

    Recorder rec;
    CHECK(memviz::Visualizer::replay(path, &rec, 0));
    CHECK(rec.frames >= 1);
    CHECK_EQ(rec.last.arrays.size(), size_t(1));
    CHECK_EQ(rec.last.arrays[0].cells.size(), size_t(8));
    CHECK_EQ(rec.last.arrays[0].cells[1].value, 10.0);
    CHECK_EQ(rec.last.arrays[0].cells[7].value, 70.0);
    CHECK(rec.last.arrays[0].cells[3].accessed);
    CHECK_EQ(rec.last.arrays[0].cursors.size(), size_t(1));
    CHECK_EQ(rec.last.arrays[0].cursors[0].label, std::string("scan"));
    std::remove(path.c_str());
}

void test_log_started_late_is_replayable() {
    // The README example calls log_to() after creating arrays; that used to
    // produce a log with no create events (unreplayable, and empty).
    const std::string path = tmpPath("late.bin");
    std::remove(path.c_str());
    {
        Recorder rec;
        memviz::Visualizer viz(&rec, false);
        auto a = viz.array1d<int>("sig", 4);
        a.set(2, 22);
        viz.log_to(path);               // preamble must describe the array
        a.set(3, 44);
    }
    Recorder rec;
    CHECK(memviz::Visualizer::replay(path, &rec, 0));
    CHECK_EQ(rec.last.arrays.size(), size_t(1));
    CHECK_EQ(rec.last.arrays[0].cells.size(), size_t(4));
    CHECK_EQ(rec.last.arrays[0].cells[2].value, 22.0);
    CHECK_EQ(rec.last.arrays[0].cells[3].value, 44.0);
    std::remove(path.c_str());
}

void test_replay_rejects_bad_input() {
    Recorder rec;
    CHECK(!memviz::Visualizer::replay(tmpPath("does_not_exist.bin"), &rec, 0));

    const std::string bad = tmpPath("bad.bin");
    {
        std::ofstream f(bad, std::ios::binary);
        f.write("NOTMEMVIZ", 9);
    }
    CHECK(!memviz::Visualizer::replay(bad, &rec, 0));

    const std::string trunc = tmpPath("trunc.bin");
    {
        std::ofstream f(trunc, std::ios::binary);
        f.write("MEMVIZ01", 8);
        f.put('N');                     // create event, then nothing
        f.put('\x01');
    }
    CHECK(!memviz::Visualizer::replay(trunc, &rec, 0));
    std::remove(bad.c_str());
    std::remove(trunc.c_str());
}

void test_concurrent_mutation() {
    // Exercises the shared lock: state and event log must stay consistent.
    Recorder rec;
    memviz::Visualizer viz(&rec, false);
    auto a = viz.array1d<int>("hot", 64);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&a, t]() {
            for (int i = 0; i < 500; ++i) {
                size_t idx = static_cast<size_t>((i * 7 + t) % 64);
                a.set(idx, i);
                a.access(idx);
                a.cursor(static_cast<size_t>(t), idx, "t");
                (void)a.get(idx);
            }
        });
    }
    for (auto& th : threads) th.join();
    auto snap = viz.snapshot();
    CHECK_EQ(snap.arrays[0].cells.size(), size_t(64));
    CHECK_EQ(snap.arrays[0].cursors.size(), size_t(4));
}

}  // namespace

int main() {
    test_1d_basic();
    test_access_marks_cell();
    test_cursor_active_is_last_moved();
    test_resize_1d();
    test_2d_basic();
    test_2d_resize_to_1d_is_safe();
    test_render_1d_2d();
    test_log_replay_roundtrip();
    test_log_started_late_is_replayable();
    test_replay_rejects_bad_input();
    test_concurrent_mutation();

    std::printf("%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
