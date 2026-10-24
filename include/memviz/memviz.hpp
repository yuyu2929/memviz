// memviz.hpp — Memory Access Visualization library (header-only, C++17)
//
// Visualize how your C++ code walks over arrays: which cells are touched,
// in what order, and where your cursors sit. Rendered live in the terminal
// (no GUI toolkit, no display server required) and optionally recorded to a
// binary log that can be replayed later.
//
// Design mirrors open-source-ideas #360:
//   0. RAII objects.
//   1. Include this header + link (just -pthread).
//   2. A Visualizer owns a renderer living on its own thread.
//   3. Spawn array views (1D bar / 2D matrix); dimensions adjustable.
//   4. Attach cursors (magnifier markers).
//   5. Move cursors. Plus: real-time display AND binary logging + replay.
//
// SPDX-License-Identifier: MIT
#ifndef MEMVIZ_HPP
#define MEMVIZ_HPP

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Windows only: enable ANSI/VT processing so the terminal backend does not
// print raw escape codes. Opt out with -DMEMVIZ_NO_WIN32_CONSOLE if you would
// rather not pull in <windows.h>.
#if defined(_WIN32) && !defined(MEMVIZ_NO_WIN32_CONSOLE)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace memviz {

enum class Kind { Bar1D, Matrix2D };

struct Cell {
    double value = 0.0;
    bool accessed = false;
    uint64_t accessTick = 0;
};

struct Cursor {
    size_t cid = 0;
    size_t idx = 0;
    std::string label;
};

// ---------------------------------------------------------------------------
// Backend abstraction. The built-in TerminalBackend draws to stdout with ANSI
// escape codes; implement your own (Qt / SDL / ImGui) by subclassing Backend.
// ---------------------------------------------------------------------------
struct ArrayRep {
    std::string name;
    Kind kind = Kind::Bar1D;
    std::vector<size_t> dims;
    std::vector<Cell> cells;
    std::vector<Cursor> cursors;
    //: index into `cursors` of the cursor that was moved last ("active").
    //: Rendering marks it differently from the others; keeping it explicit
    //: avoids the old bug where "active" silently meant "the last element of
    //: the vector", so moving the first cursor still highlighted the second.
    size_t active = 0;
};

struct Snapshot {
    std::vector<ArrayRep> arrays;
    uint64_t tick = 0;
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual void frame(const Snapshot& s) = 0;
    virtual void clear() {}
};

class TerminalBackend : public Backend {
    bool color_;
public:
    explicit TerminalBackend(bool color = true) : color_(color) { enableAnsi(); }

    /// Turn on VT escape processing where the platform needs to be asked.
    static void enableAnsi() {
#if defined(_WIN32) && !defined(MEMVIZ_NO_WIN32_CONSOLE)
        static bool done = false;
        if (done) return;
        done = true;
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD mode = 0;
        if (!GetConsoleMode(h, &mode)) return;
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
    }

    void frame(const Snapshot& s) override {
        std::string out;
        out += "\033[2J\033[H";  // clear screen + home
        out += "memviz  tick=" + std::to_string(s.tick) + "\n\n";
        for (const auto& a : s.arrays) out += renderArray(a);
        out += "\n(press Ctrl-C to stop)\n";
        std::cout << out << std::flush;
    }

    void clear() override { std::cout << "\033[2J\033[H" << std::flush; }

    static std::string esc(bool on, const std::string& code, const std::string& s) {
        if (!on) return s;
        return code + s + "\033[0m";
    }

    // marker: 0 = plain, 1 = active (last moved) cursor, 2 = other cursor
    std::string cellStr(const Cell& c, int marker) const {
        // 3 digits plus sign, a decimal point and NUL needs more than the old
        // 8-byte buffer: values above 9999 were silently truncated.
        char buf[40];
        if (std::abs(c.value) < 1e-9)
            std::snprintf(buf, sizeof buf, " . ");
        else
            std::snprintf(buf, sizeof buf, "%3.0f", c.value);
        std::string s(buf);
        // The brackets carry the information; the colour is only decoration.
        // Wrapping the whole marker (instead of colouring the bracket) keeps
        // cursors visible on a monochrome terminal / when color_ is false.
        if (marker == 1) return esc(color_, "\033[1;33m", "[" + s + "]");
        if (marker == 2) return esc(color_, "\033[36m", "{" + s + "}");
        if (c.accessed) return " " + esc(color_, "\033[32m", s) + " ";
        return " " + esc(color_, "\033[2m", s) + " ";
    }

    int markerAt(const ArrayRep& a, size_t idx) const {
        if (a.cursors.empty()) return 0;
        size_t act = a.active < a.cursors.size() ? a.active : a.cursors.size() - 1;
        if (a.cursors[act].idx == idx) return 1;
        for (size_t k = 0; k < a.cursors.size(); ++k)
            if (k != act && a.cursors[k].idx == idx) return 2;
        return 0;
    }

    std::string magnifier1D(const ArrayRep& a, const Cursor& cu) const {
        if (a.cells.empty()) return "";
        long lo = static_cast<long>(cu.idx) - 3;
        long hi = static_cast<long>(cu.idx) + 3;
        if (lo < 0) lo = 0;
        if (hi >= static_cast<long>(a.cells.size()))
            hi = static_cast<long>(a.cells.size()) - 1;
        std::string o = "   magnifier @ " + std::to_string(cu.idx);
        if (!cu.label.empty()) o += " (" + cu.label + ")";
        o += ": ";
        for (long i = lo; i <= hi; ++i)
            o += cellStr(a.cells[i], markerAt(a, static_cast<size_t>(i)));
        o += "\n";
        return o;
    }

    std::string magnifier2D(const ArrayRep& a, const Cursor& cu) const {
        if (a.cells.empty() || a.dims.size() < 2) return "";
        size_t r = a.dims[0], c = a.dims[1];
        size_t ci = cu.idx / c, cj = cu.idx % c;
        long r0 = static_cast<long>(ci) - 1, r1 = static_cast<long>(ci) + 1;
        long c0 = static_cast<long>(cj) - 2, c1 = static_cast<long>(cj) + 2;
        if (r0 < 0) r0 = 0;
        if (r1 >= static_cast<long>(r)) r1 = static_cast<long>(r) - 1;
        if (c0 < 0) c0 = 0;
        if (c1 >= static_cast<long>(c)) c1 = static_cast<long>(c) - 1;
        std::string o = "   magnifier @ (" + std::to_string(ci) + "," +
                        std::to_string(cj) + ")";
        if (!cu.label.empty()) o += " " + cu.label;
        o += ":\n";
        for (long i = r0; i <= r1; ++i) {
            o += "     ";
            for (long j = c0; j <= c1; ++j)
                o += cellStr(a.cells[static_cast<size_t>(i) * c + static_cast<size_t>(j)],
                             markerAt(a, static_cast<size_t>(i) * c + static_cast<size_t>(j)));
            o += "\n";
        }
        return o;
    }

    std::string renderArray(const ArrayRep& a) const {
        if (a.cells.empty()) return "[" + a.name + "] (empty)\n";
        // A 2D array resized to 1D (ArrayHandle::resize(n)) leaves only one
        // dimension; indexing dims[1] there was undefined behaviour.
        const bool is2d = (a.kind == Kind::Matrix2D && a.dims.size() >= 2);
        std::string o = "[" + a.name + "] ";
        if (!is2d)
            o += "(1D  n=" + std::string(std::to_string(a.cells.size())) + ")\n";
        else
            o += "(2D  " + std::to_string(a.dims[0]) + "x" +
                 std::to_string(a.dims[1]) + ")\n";

        if (!is2d) {
            for (size_t i = 0; i < a.cells.size(); ++i)
                o += cellStr(a.cells[i], markerAt(a, i));
            o += "\n";
        } else {
            size_t r = a.dims[0], c = a.dims[1];
            for (size_t i = 0; i < r; ++i) {
                for (size_t j = 0; j < c; ++j) {
                    size_t idx = i * c + j;
                    if (idx >= a.cells.size()) break;
                    o += cellStr(a.cells[idx], markerAt(a, idx));
                }
                o += "\n";
            }
        }
        for (const auto& cu : a.cursors)
            o += is2d ? magnifier2D(a, cu) : magnifier1D(a, cu);
        return o;
    }
};

// ---------------------------------------------------------------------------
// Little-endian helpers for the binary event log.
// ---------------------------------------------------------------------------
namespace detail {
inline void putU16(std::ostream& o, uint16_t v) {
    unsigned char b[2] = {static_cast<unsigned char>(v & 0xff),
                          static_cast<unsigned char>((v >> 8) & 0xff)};
    o.write(reinterpret_cast<char*>(b), 2);
}
inline void putU32(std::ostream& o, uint32_t v) {
    unsigned char b[4];
    for (int i = 0; i < 4; ++i) b[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xff);
    o.write(reinterpret_cast<char*>(b), 4);
}
inline void putU64(std::ostream& o, uint64_t v) {
    unsigned char b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xff);
    o.write(reinterpret_cast<char*>(b), 8);
}
inline uint16_t getU16(std::istream& in) {
    unsigned char b[2];
    in.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}
inline uint32_t getU32(std::istream& in) {
    unsigned char b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(b[i]) << (8 * i);
    return v;
}
inline uint64_t getU64(std::istream& in) {
    unsigned char b[8];
    in.read(reinterpret_cast<char*>(b), 8);
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(b[i]) << (8 * i);
    return v;
}
}  // namespace detail

// ---------------------------------------------------------------------------
// Visualizer: the RAII root object. Owns a renderer thread + optional log.
// ---------------------------------------------------------------------------
template <class T>
class ArrayHandle;  // fwd: declared fully below

class Visualizer {
    std::mutex mtx_;
    std::vector<ArrayRep> arrays_;
    std::atomic<bool> running_{false};
    std::thread thr_;
    Backend* backend_;
    bool ownBackend_;
    std::ofstream logf_;
    std::atomic<bool> logging_{false};
    //: guarded by mtx_ like the array state, so the event log and the state it
    //: describes are updated under the same lock and cannot disagree.
    std::atomic<uint64_t> tick_{0};
    std::atomic<bool> dirty_{false};

    void renderLoop() {
        while (running_) {
            if (dirty_.exchange(false)) {
                Snapshot s;
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    s.arrays = arrays_;
                    s.tick = tick_.load();
                }
                backend_->frame(s);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(70));
        }
    }

    void bump() {
        tick_.fetch_add(1);
        dirty_ = true;
    }

    // --- log writers -------------------------------------------------------
    // All callers hold mtx_, which is what keeps the event stream consistent
    // with the state it describes.
    void logCreate(size_t id, const std::string& name, Kind k,
                   const std::vector<size_t>& dims) {
        if (!logging_) return;
        logf_.put('N');
        detail::putU32(logf_, static_cast<uint32_t>(id));
        logf_.put(k == Kind::Bar1D ? '1' : '2');
        detail::putU32(logf_, static_cast<uint32_t>(dims.size()));
        for (size_t d : dims) detail::putU32(logf_, static_cast<uint32_t>(d));
        detail::putU16(logf_, static_cast<uint16_t>(name.size()));
        logf_.write(name.data(), static_cast<std::streamsize>(name.size()));
    }
    void logSet(size_t id, size_t idx, double v) {
        if (!logging_) return;
        logf_.put('S');
        detail::putU32(logf_, static_cast<uint32_t>(id));
        detail::putU32(logf_, static_cast<uint32_t>(idx));
        uint64_t u;
        double d = v;
        std::memcpy(&u, &d, 8);
        detail::putU64(logf_, u);
    }
    void logAccess(size_t id, size_t idx) {
        if (!logging_) return;
        logf_.put('A');
        detail::putU32(logf_, static_cast<uint32_t>(id));
        detail::putU32(logf_, static_cast<uint32_t>(idx));
    }
    void logCursor(size_t id, size_t cid, size_t idx, const std::string& label) {
        if (!logging_) return;
        logf_.put('K');
        detail::putU32(logf_, static_cast<uint32_t>(id));
        detail::putU32(logf_, static_cast<uint32_t>(cid));
        detail::putU32(logf_, static_cast<uint32_t>(idx));
        detail::putU16(logf_, static_cast<uint16_t>(label.size()));
        logf_.write(label.data(), static_cast<std::streamsize>(label.size()));
    }
    void logResize(size_t id, const std::vector<size_t>& dims) {
        if (!logging_) return;
        logf_.put('R');
        detail::putU32(logf_, static_cast<uint32_t>(id));
        detail::putU32(logf_, static_cast<uint32_t>(dims.size()));
        for (size_t d : dims) detail::putU32(logf_, static_cast<uint32_t>(d));
    }

public:
    explicit Visualizer(Backend* backend = new TerminalBackend(), bool ownBackend = true)
        : backend_(backend), ownBackend_(ownBackend) {
        running_ = true;
        thr_ = std::thread(&Visualizer::renderLoop, this);
    }

    ~Visualizer() {
        running_ = false;
        if (thr_.joinable()) thr_.join();
        if (logging_) logf_.close();
        backend_->clear();
        if (ownBackend_) delete backend_;
    }

    /// Start recording to `path`.
    ///
    /// Arrays that already exist are written first as a preamble (creation +
    /// current values + cursors), so a log started mid-run is still
    /// replayable. Anything that happened *before* this call is not recorded.
    /// Consistent copy of the current state (what a backend would render).
    Snapshot snapshot() {
        std::lock_guard<std::mutex> lk(mtx_);
        Snapshot s;
        s.arrays = arrays_;
        s.tick = tick_.load();
        return s;
    }

    void log_to(const std::string& path) {
        std::lock_guard<std::mutex> lk(mtx_);
        logf_.open(path, std::ios::binary | std::ios::trunc);
        bool ok = logf_.good();
        const char magic[8] = {'M', 'E', 'M', 'V', 'I', 'Z', '0', '1'};
        if (ok) logf_.write(magic, 8);
        logging_ = ok;
        if (!ok) return;
        for (size_t id = 0; id < arrays_.size(); ++id) {
            const ArrayRep& a = arrays_[id];
            logCreate(id, a.name, a.kind, a.dims);
            for (size_t i = 0; i < a.cells.size(); ++i)
                if (a.cells[i].value != 0.0) logSet(id, i, a.cells[i].value);
            for (size_t i = 0; i < a.cells.size(); ++i)
                if (a.cells[i].accessed) logAccess(id, i);
            for (const Cursor& cu : a.cursors) logCursor(id, cu.cid, cu.idx, cu.label);
        }
    }

    // --- array creation (private; public API returns typed handles) --------
    template <class T>
    size_t _create1d(const std::string& name, size_t n) {
        std::lock_guard<std::mutex> lk(mtx_);
        ArrayRep a;
        a.name = name;
        a.kind = Kind::Bar1D;
        a.dims = {n};
        a.cells.resize(n);
        arrays_.push_back(std::move(a));
        size_t id = arrays_.size() - 1;
        logCreate(id, name, Kind::Bar1D, {n});
        bump();
        return id;
    }
    template <class T>
    size_t _create2d(const std::string& name, size_t r, size_t c) {
        std::lock_guard<std::mutex> lk(mtx_);
        ArrayRep a;
        a.name = name;
        a.kind = Kind::Matrix2D;
        a.dims = {r, c};
        a.cells.resize(r * c);
        arrays_.push_back(std::move(a));
        size_t id = arrays_.size() - 1;
        logCreate(id, name, Kind::Matrix2D, {r, c});
        bump();
        return id;
    }

public:
    /// Create a 1D array view ("bar with cells").
    template <class T>
    ArrayHandle<T> array1d(const std::string& name, size_t n) {
        return ArrayHandle<T>(this, _create1d<T>(name, n));
    }
    /// Create a 2D array view (matrix). Higher dims render as 2D sections.
    template <class T>
    ArrayHandle<T> array2d(const std::string& name, size_t rows, size_t cols) {
        return ArrayHandle<T>(this, _create2d<T>(name, rows, cols));
    }

    void setDouble(size_t id, size_t idx, double v) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (id < arrays_.size() && idx < arrays_[id].cells.size()) {
            arrays_[id].cells[idx].value = v;
        }
        // Logging happens under the same lock as the mutation: otherwise two
        // threads could interleave and the recorded history would disagree
        // with the final state (and the log stream itself would race).
        logSet(id, idx, v);
        bump();
    }
    double getDouble(size_t id, size_t idx) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (id < arrays_.size() && idx < arrays_[id].cells.size())
            return arrays_[id].cells[idx].value;
        return 0.0;
    }
    void access(size_t id, size_t idx) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (id < arrays_.size() && idx < arrays_[id].cells.size()) {
            arrays_[id].cells[idx].accessed = true;
            arrays_[id].cells[idx].accessTick = tick_.load();
        }
        logAccess(id, idx);
        bump();
    }
    void cursorAt(size_t id, size_t cid, size_t idx, const std::string& label) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (id < arrays_.size()) {
            auto& cs = arrays_[id].cursors;
            bool found = false;
            for (size_t k = 0; k < cs.size(); ++k) {
                if (cs[k].cid == cid) {
                    cs[k].idx = idx;
                    cs[k].label = label;
                    arrays_[id].active = k;   // the cursor you just moved is active
                    found = true;
                    break;
                }
            }
            if (!found) {
                cs.push_back({cid, idx, label});
                arrays_[id].active = cs.size() - 1;
            }
        }
        logCursor(id, cid, idx, label);
        bump();
    }

    /// Alter dimensions after creation ("Dimensions can be altered further").
    void resize(size_t id, const std::vector<size_t>& dims) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (id >= arrays_.size() || dims.empty()) return;
        size_t total = 1;
        for (size_t d : dims) total *= d;
        arrays_[id].dims = dims;
        // The renderer picks its layout from `kind`; leaving it at Matrix2D
        // after resizing to one dimension made it index dims[1] out of range.
        arrays_[id].kind = (dims.size() >= 2) ? Kind::Matrix2D : Kind::Bar1D;
        arrays_[id].cells.resize(total);
        logResize(id, dims);
        bump();
    }

    // --- replay: rebuild + animate from a binary log -----------------------
    /// Replay a recorded log. `backend == nullptr` uses a terminal backend
    /// that replay owns; a caller-supplied backend stays the caller's.
    static bool replay(const std::string& path, Backend* backend = nullptr,
                       int stepMs = 25) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        char magic[8] = {0};
        if (!in.read(magic, 8) || std::memcmp(magic, "MEMVIZ01", 8) != 0)
            return false;

        // The old signature defaulted to `new TerminalBackend()` at the call
        // site and always took ownership, so every early return above leaked
        // it.
        std::unique_ptr<Backend> owned;
        if (backend == nullptr) {
            owned.reset(new TerminalBackend());
            backend = owned.get();
        }

        Visualizer viz(backend, /*ownBackend=*/false);
        using namespace detail;
        char type;
        while (in.get(type)) {
            if (type == 'N') {
                size_t id = getU32(in);
                int k = in.get();
                if (k != '1' && k != '2') return false;
                size_t nd = getU32(in);
                if (nd == 0 || nd > 8) return false;   // corrupt / absurd
                std::vector<size_t> dims(nd);
                for (size_t i = 0; i < nd; ++i) dims[i] = getU32(in);
                if (dims[0] == 0 || (nd >= 2 && dims[1] == 0)) return false;
                if (dims[0] > (1u << 20) || (nd >= 2 && dims[1] > (1u << 20)))
                    return false;
                uint16_t nl = getU16(in);
                std::string name(nl, ' ');
                if (nl && !in.read(&name[0], nl)) return false;
                size_t newId = (k == '1')
                                   ? viz._create1d<int>(name, dims[0])
                                   : viz._create2d<int>(name, dims[0], dims[1]);
                if (newId != id) return false;  // corrupt log: non-sequential id
            } else if (type == 'S') {
                size_t id = getU32(in), idx = getU32(in);
                uint64_t u = getU64(in);
                if (!in) return false;
                double d;
                std::memcpy(&d, &u, 8);
                viz.setDouble(id, idx, d);
            } else if (type == 'A') {
                size_t id = getU32(in), idx = getU32(in);
                if (!in) return false;
                viz.access(id, idx);
            } else if (type == 'K') {
                size_t id = getU32(in), cid = getU32(in), idx = getU32(in);
                uint16_t ll = getU16(in);
                std::string label(ll, ' ');
                if (ll && !in.read(&label[0], ll)) return false;
                viz.cursorAt(id, cid, idx, label);
            } else if (type == 'R') {
                size_t id = getU32(in);
                size_t nd = getU32(in);
                if (nd == 0 || nd > 8) return false;
                std::vector<size_t> dims(nd);
                for (size_t i = 0; i < nd; ++i) dims[i] = getU32(in);
                viz.resize(id, dims);
            } else {
                break;  // unknown / 'E'
            }
            if (!in) return false;   // truncated record
            if (stepMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
        }
        if (stepMs > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
        // Draw the final state explicitly: the render thread sleeps between
        // frames, so the last events could otherwise never be shown.
        backend->frame(viz.snapshot());
        return true;
    }
};

// ---------------------------------------------------------------------------
// Typed handle: the ergonomic front-end a developer actually uses.
// ---------------------------------------------------------------------------
template <class T>
class ArrayHandle {
    Visualizer* viz_;
    size_t id_;
public:
    ArrayHandle(Visualizer* v, size_t id) : viz_(v), id_(id) {}
    void set(size_t idx, T v) { viz_->setDouble(id_, idx, static_cast<double>(v)); }
    T get(size_t idx) { return static_cast<T>(viz_->getDouble(id_, idx)); }
    void access(size_t idx) { viz_->access(id_, idx); }
    void cursor(size_t cid, size_t idx, const std::string& label = "") {
        viz_->cursorAt(id_, cid, idx, label);
    }
    /// Alter dimensions after creation.
    void resize(size_t n) { viz_->resize(id_, {n}); }
    void resize(size_t rows, size_t cols) { viz_->resize(id_, {rows, cols}); }
};

}  // namespace memviz

#endif  // MEMVIZ_HPP
