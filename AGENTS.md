# AGENTS.md — Chess Evaluation Bar

## Project goal
Build a macOS desktop application in **C++20** that displays **only a live, vertical chess evaluation bar** for live games that I am playing

The eventual application watches a user-selected chessboard region, recognizes the position, uses Stockfish to evaluate it, and updates an unobtrusive floating bar. **Do not add best-move suggestions, arrows, engine lines, move recommendations, opening advice, or coaching features.**

## Environment and stack
- Target OS: **macOS** first (Apple Silicon compatible).
- Language: **C++20**; Objective-C++ (`.mm`) only for macOS-specific frameworks if needed.
- Build: **CMake** (out-of-source build).
- GUI: **Qt 6 Widgets** for the evaluation bar and configuration window.
- Chess engine: externally installed **Stockfish**, controlled via the **UCI protocol** using a subprocess (never reimplement engine evaluation).
- Image processing: **OpenCV**, introduced only when screen recognition work starts.
- Capture: **ScreenCaptureKit** (macOS) behind an abstract capture interface, introduced after the manual-FEN MVP.
- Testing: **CTest** and lightweight C++ unit tests; choose a test framework only when it provides concrete value.

## Product boundaries
The only analysis output is a black/white vertical evaluation bar, optionally displaying a numeric evaluation value. No principal variation, recommended moves, arrows, or move list should appear in the main UI. Settings and troubleshooting status are allowed.

## Milestones (build in this order)
1. **M1 — UCI CLI:** Start Stockfish as a child process; perform `uci`/`uciok`, `isready`/`readyok`, send `position fen <FEN>` and `go depth <N>`, read `info ... score cp ...` or `info ... score mate ...`, and print a white-relative score. Handle engine errors, timeouts, shutdown, and EOF safely.
2. **M2 — Qt evaluation bar:** Manual FEN entry, Start/Stop analysis, vertical black/white bar, numeric evaluation. Never block the UI thread. Debounce new evaluations and discard stale results using position generation IDs.
3. **M3 — User-selected screen region:** Add ScreenCaptureKit capture with permission handling and configurable region. Do not process full-screen frames unnecessarily.
4. **M4 — Recognition on a controlled board theme:** Detect 8x8 squares, classify piece occupancy/identity, board orientation, and recognize moves. Begin with one offline board/theme; expand later.
5. **M5 — Position tracking:** Maintain the authoritative game state from a known start position. Validate observed changes against legal moves; handle captures, promotion, castling, and en passant. Do not infer castling rights, en passant rights, or move counters from pixels alone. When uncertain, freeze the bar and request resynchronization rather than showing a misleading evaluation.
6. **M6 — Performance and reliability:** Measure capture-to-score latency, frame processing time, memory usage, and CPU overhead. Analyze only after confirmed position changes; rate-limit engine searches.

## Suggested repository structure
```text
chess-eval-bar/
├── AGENTS.md
├── CMakeLists.txt
├── README.md
├── .gitignore
├── src/
│   ├── main.cpp
│   ├── engine/        # Stockfish subprocess, UCI protocol parsing
│   ├── chess/         # FEN, legal position tracking (later)
│   ├── ui/            # Qt widgets and evaluation model
│   ├── capture/       # screen capture interface; macOS implementation later
│   └── vision/        # board recognition; introduced later
├── include/
│   └── chess_eval/
├── tests/
└── assets/
```
Do not create empty placeholder modules merely to match this structure; add directories as functionality arrives.

## Architecture rules
- Isolate engine I/O, score normalization, UI, capture, recognition, and game-state logic behind small interfaces.
- The UI must never block on engine startup, pipe reads, or analysis.
- Own Stockfish as one managed child process. Avoid shell interpolation of FEN or user input. Close file descriptors and cleanly reap the child process.
- Use RAII for process handles and resource lifetime; prefer standard library facilities when suitable.
- Avoid data races: pass immutable position snapshots and tagged results between workers and UI. Never update Qt widgets from worker threads.
- Restart or resynchronize cleanly if Stockfish crashes, returns malformed data, or hangs.
- Reject invalid FEN with clear diagnostics. Don't guess at illegible positions.
- Limit capture frequency and avoid sending unchanged positions for reanalysis.
- Keep platform-specific code outside core engine and parsing layers.

## UCI and evaluation requirements
- Handle incremental and partial pipe output, multiple `info` lines, and mate scores.
- UCI `score cp` and `score mate` are expressed relative to the **side to move**; convert to **White's perspective** before display.
- Recognize `upperbound` and `lowerbound`; do not present bound scores as precise final evaluations.
- For an evaluation bar, map centipawns to a bounded 0–1 White share using a smooth monotonic function (e.g., logistic), with 0 cp centered at 0.5. Mate scores require special handling near the corresponding extreme.
- Do not assume the engine supports all optional UCI commands or options. Wait for `uciok`/`readyok` where appropriate.
- When a position changes, send `stop`, coordinate completion of the old search, and start analysis for the latest position. Tag updates so stale engine output cannot overwrite the current display.
- Do not parse `pv` or use it for recommendations.

## Coding conventions
- Prefer straightforward, readable C++20 over abstractions without a clear payoff.
- Use `std::unique_ptr`, RAII, `const`, scoped enums, and strong types where they improve correctness.
- No global mutable engine state. No detached threads.
- Use explicit error handling, actionable error messages, and deterministic shutdown.
- Keep functions small; place declarations in headers and implementations in `.cpp` files where appropriate.
- Format with `clang-format`; enable useful compiler warnings (`-Wall -Wextra -Wpedantic` with Apple Clang as applicable).
- Comment on protocol invariants, concurrency ownership, and non-obvious choices rather than narrating simple code.

## Testing expectations
- Unit test UCI parser against centipawn scores, mate scores, bounds, malformed lines, missing fields, and streaming/partial lines.
- Unit test score perspective conversion for both sides to move and the symmetric zero position.
- Unit test score-to-bar mapping for zero, moderate, extreme, and mate scores.
- Add integration tests that run only when a Stockfish executable is configured; keep parser/unit tests independent of Stockfish.
- After each implementation change: build, run tests, and report exact commands plus results.
- Do not claim tests passed unless actually executed. If a dependency is missing, identify it and give a reproducible command.

## Workflow for Codex
1. Inspect the repository before editing. Read this file and the README.
2. Implement the **smallest usable vertical slice** for the current milestone; avoid sweeping rewrites.
3. State the intended changes and which files will be edited.
4. Keep third-party dependencies minimal and explain additions.
5. Add tests for parsing, score conversions, and state transitions.
6. Build and run tests where possible; summarize changes, limitations, and next concrete step.
7. Ask before expanding beyond the requested milestone or adding features outside the product boundary.
