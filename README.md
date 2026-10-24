# memviz

**See how your code walks over memory.** One header, no dependencies, live in
the terminal.

```cpp
#include <memviz/memviz.hpp>

memviz::Visualizer viz;
auto a = viz.array1d<int>("signal", 16);
a.set(3, 42);
a.access(3);          // cell turns green
a.cursor(0, 3, "i");  // magnifier follows it
```

```
[signal] (1D  n=16)
   .     .     .  [ 42]    .     .     .     .     .     .     .     .     .     .     .     .
   magnifier @ 3 (i):    .     .     .  [ 42]    .     .     .
```

Watch a sort compare-and-swap its way through an array, watch a matrix fill up,
watch an index come back into range — without adding `printf`s to the algorithm
and without a GUI toolkit, a display server, or an X11/Wayland session.

[![CI](https://github.com/yuyu2929/memviz/actions/workflows/ci.yml/badge.svg)](https://github.com/yuyu2929/memviz/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-yellow.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](#requirements)
[![header-only](https://img.shields.io/badge/header--only-1%20file-34d399)](#install)
[![deps](https://img.shields.io/badge/deps-pthread%20only-lightgrey)](#requirements)

## Try it in 30 seconds

```bash
git clone https://github.com/yuyu2929/memviz.git
cd memviz
c++ -std=c++17 -pthread -Iinclude examples/sort_demo.cpp -o sort_demo
./sort_demo          # 16 cells bubble-sorting themselves, live
```

On Windows (MSVC): `cl /std:c++17 /EHsc /Iinclude examples\sort_demo.cpp`.

## What you get

| | |
|---|---|
| **1D "bar with cells"** | a row of values you can address and colour as accessed |
| **2D matrix** | grid view for images, tables, kernels |
| **Cursors + magnifier** | each cursor gets a zoomed neighbourhood block, so you can read the value even when the bar is wider than the window |
| **Access tracking** | green = touched, dim = untouched, so you can see the *order* an algorithm walks |
| **Binary event log + replay** | `log_to("events.bin")`, then `Visualizer::replay("events.bin")` — record now, look later |
| **Pluggable backend** | subclass `memviz::Backend` for Qt / SDL / ImGui; the core never touches I/O |

## Install

### FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(memviz
  GIT_REPOSITORY https://github.com/yuyu2929/memviz.git
  GIT_TAG v1.0.1)
FetchContent_MakeAvailable(memviz)

target_link_libraries(myapp PRIVATE memviz::memviz)
```

### Installed package

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build && cmake --install build
```

```cmake
find_package(memviz 1 CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE memviz::memviz)
```

### No CMake at all

Copy `include/memviz/memviz.hpp` next to your sources, `#include "memviz.hpp"`,
compile with `-std=c++17 -pthread`.

## API

```cpp
Visualizer viz;                          // RAII; renderer on its own thread
auto a = viz.array1d<int>("a", 24);      // 1D bar
auto m = viz.array2d<float>("m", 8, 12); // 2D matrix
a.set(0, 42);   a.get(0);
a.access(0);                             // mark touched
a.cursor(1, 0, "scan");                  // cursor id 1 at index 0
a.resize(32);                            // dimensions are adjustable
viz.log_to("events.bin");                // record from here on
Visualizer::replay("events.bin");        // ... and animate it back
```

Two behaviours worth knowing:

- **The cursor you moved last is the "active" one** — it renders as `[v]`, the
  others as `{v}`. Brackets are drawn even with colour disabled, so cursors stay
  visible on a monochrome terminal or when stdout is a pipe.
- **`log_to()` writes a preamble** for arrays that already exist, so starting a
  recording mid-run still produces a replayable file. Events before the call are
  not recorded.

Thread safety: every mutator takes one mutex, and the event log is written under
the same lock, so a recorded file always agrees with the state it describes.
Rendering happens on a separate thread and reads a consistent snapshot.

## Requirements

- C++17 (`std::thread`, `std::atomic`, `std::string`)
- `pthread` on POSIX; MSVC needs nothing extra
- A terminal with ANSI escape support. On Windows the library enables virtual
  terminal processing itself; define `MEMVIZ_NO_WIN32_CONSOLE` to opt out and
  keep `<windows.h>` out of your translation units.

## Tests

```bash
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

No test framework: `tests/memviz_test.cpp` is a plain executable with 47
assertions covering the API, the 2D→1D resize path, log/replay round-trips,
corrupt-log rejection and concurrent mutation.

## Status

v1.0.1. The event-log format (`MEMVIZ01`) is stable within 1.x.

## License

MIT — see [LICENSE](LICENSE). Commercial support, custom backends and priority
fixes are available: see [COMMERCIAL.md](COMMERCIAL.md).

Changes: [CHANGELOG.md](CHANGELOG.md).
