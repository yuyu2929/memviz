# Changelog

All notable changes to memviz are documented here. The event-log format has its
own magic (`MEMVIZ01`); a breaking change to it bumps the major version.

## [1.0.1] - 2026-10-25

First release with tests and CI. The API is unchanged apart from two behaviour
fixes that match what the documentation already claimed.

### Fixed

- **Resizing a 2D array to one dimension was undefined behaviour.** `resize()`
  replaced `dims` but left `kind = Matrix2D`, so the renderer indexed `dims[1]`
  on a one-element vector. `resize()` now updates `kind`, and the renderer
  derives its layout defensively.
- **The "active" cursor was whichever cursor had been added last**, not the one
  you moved last: `markerAt` compared against `cursors.size() - 1`. With two
  cursors, moving the first still highlighted the second. `ArrayRep` now carries
  an explicit `active` index, updated by `cursorAt()`.
- **Cursor brackets disappeared when colour was disabled.** `cellStr` wrapped
  only the bracket in the colour escape, so `TerminalBackend(false)` printed
  plain values with no indication of where a cursor was. The whole marker is now
  colourised, and the brackets are always drawn.
- **`log_to()` produced an unreplayable file if called after arrays existed.**
  Replay requires the create events, so the log had no `N` records and
  `replay()` rejected it. `log_to()` now writes a preamble describing the arrays
  it finds (creation, non-zero values, access flags, cursors).
- **`replay()` leaked a backend on every early return.** The default argument
  was `new TerminalBackend()`, evaluated at the call site, and replay took
  ownership — so a missing or malformed file leaked it. The parameter is now
  nullable, and replay owns only what it creates.
- **`replay()` could skip the final frame.** The render thread sleeps between
  frames, so the last events might never be drawn before the `Visualizer` was
  destroyed. Replay now renders the final snapshot explicitly.
- **Data race on `tick_`.** It was a plain `uint64_t` incremented outside the
  mutex and read inside it. It is now `std::atomic`.
- **The event log was written outside the state lock.** Two threads mutating
  concurrently could interleave records and record a history that disagreed
  with the final state. Logging now happens inside the same critical section as
  the mutation.
- **Values above 9999 were truncated** in the terminal renderer (`char buf[8]`
  with `%3.0f`).
- **Raw escape codes on Windows.** The terminal backend now enables virtual
  terminal processing (`MEMVIZ_NO_WIN32_CONSOLE` opts out).
- **Missing `<cstdio>`** — `std::snprintf` was used without including it.
- **`find_package(memviz CONFIG)` failed on an installed copy**: the targets
  file was exported but no `memvizConfig.cmake` / version file was generated.
- Corrupt or truncated logs were read past-end into garbage; replay now rejects
  them (bad magic, unknown record type, absurd dimensions, short read).

### Added

- `Visualizer::snapshot()` — a consistent copy of the state a backend would see.
- `tests/memviz_test.cpp` — 47 assertions, no framework; wired into CTest.
- CMake options `MEMVIZ_BUILD_EXAMPLES` / `MEMVIZ_BUILD_TESTS`, built only when
  memviz is the top-level project (so FetchContent does not inject targets).
- CI: GCC and Clang on Linux, AppleClang on macOS, MSVC on Windows, plus
  AddressSanitizer/UBSan and ThreadSanitizer jobs and an installed-package
  consumer test.
- `COMMERCIAL.md` and this changelog.

## [1.0.0] - 2024

Initial release: header-only visualizer, 1D/2D views, cursors with magnifiers,
binary event log and replay, pluggable backend.

[1.0.1]: https://github.com/yuyu2929/memviz/releases/tag/v1.0.1
[1.0.0]: https://github.com/yuyu2929/memviz/releases/tag/v1.0.0
