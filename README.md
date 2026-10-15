# Chess Evaluation Bar — M1 UCI CLI

A C++20 command-line application that starts an externally installed Stockfish,
accepts a FEN, searches to a configurable depth, and prints one evaluation from
White's perspective. M1 has no GUI, capture, recognition, move suggestions, or
principal-variation output.

## Install prerequisites (macOS)

```bash
xcode-select --install
brew install cmake stockfish
```

Apple Silicon and Intel macOS are supported. The subprocess module also targets
POSIX systems; Windows support is not implemented. No third-party C++ libraries
are required. Qt and OpenCV are only needed for later milestones.

## Build and test

Run from this repository's `Chess-analysis-bar` directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

Parser, score conversion, FEN, stream framing, and fake-engine lifecycle tests
run without Stockfish. To enable the additional real-engine integration test:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DSTOCKFISH_EXECUTABLE="$(command -v stockfish)"
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

The optional executable setting is cached by CMake; set
`-DSTOCKFISH_EXECUTABLE=` to disable that test again.

## Run

```bash
./build/chess-eval \
  --fen 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1' \
  --depth 15
```

Stockfish is found through `PATH`, or supplied explicitly:

```bash
./build/chess-eval --stockfish /opt/homebrew/bin/stockfish \
  --fen '7k/6Q1/6K1/8/8/8/8/8 b - - 0 1' \
  --depth 8 --timeout-ms 10000
```

`--fen` is required and must be quoted as one argument. `--depth` defaults to 15
and accepts 1–128. `--timeout-ms` defaults to 30000 and limits each handshake
and the entire search separately. `--help` prints usage.

On success, stdout contains exactly one score line:

- `cp 42`: White is ahead by 42 centipawns (0.42 pawns).
- `cp -42`: Black is ahead by 42 centipawns.
- `mate 3`: White has a forced mate in 3 moves.
- `mate -3`: Black has a forced mate in 3 moves.
- `mate 0`: Black is already checkmated; `mate -0`: White is already checkmated.

Values depend on Stockfish version and search depth. UCI scores are relative to
the side to move; the application reverses the sign for Black to move. It uses
the latest exact score at the greatest reported depth, ignoring bounds and
secondary MultiPV scores. If no exact score arrives before search completion,
it reports an error rather than presenting a bound as precise.

Errors go to stderr with exit status 1 and no evaluation on stdout. Successful
analysis exits with status 0. Missing engines, malformed input, unexpected EOF,
oversized output lines, and timeouts are handled. Engine stderr is suppressed;
launch and protocol errors are diagnosed by the application.

## Architecture and scope

- `src/engine/process_posix.cpp`: RAII child ownership, shell-free `posix_spawnp`,
  nonblocking socket I/O, deadlines, incremental reads, and process reaping.
- `src/engine/uci.cpp`: pure parsing, line framing, perspective conversion,
  output formatting, and a tested logistic White-share mapping for a future bar.
- `src/engine/engine.cpp`: `uci`/`uciok`, `isready`/`readyok`, position submission,
  search, exact-score selection, and completion coordination.
- `src/chess/fen.cpp`: six-field FEN validation, board dimensions, kings, pawn
  placement, castling-piece consistency, en passant consistency, and counters.
- `src/main.cpp`: CLI argument handling and evaluation-only output.

FEN validation checks structure and basic position consistency, not full legal
reachability or game history. Supply a legal standard-chess position; Chess960
and legal move tracking are outside M1.

Each synchronous `analyze` call owns a fresh process and discards its state on
success or failure. Shutdown sends `quit`, allows a short grace period, then
uses SIGTERM/SIGKILL if needed and reaps the child. A subsequent call starts
cleanly. The UCI completion marker is consumed only to detect search completion;
its move and any PV content are never exposed.

The core library has no UI dependency. In M2, call it from an owned worker and
pass immutable position snapshots and tagged results to Qt. Continuous engine
reuse, cancellation/resynchronization, and UI generation IDs belong to that
later integration. Capture and OpenCV remain later milestones.
