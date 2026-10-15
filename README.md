# Chess Evaluation Bar — M3 Screen Capture

A C++20 / Qt 6 desktop application with a small, resizable, floating black-and-white
evaluation bar and a manual FEN settings window. Stockfish evaluates each position
asynchronously. The original evaluation-only CLI is also included. A separate Screen capture tab
provides region selection and a live preview using ScreenCaptureKit. Captured
frames never trigger Stockfish analysis. Computer vision, move suggestions, and
principal-variation display are not implemented.

## Install prerequisites (macOS)

```bash
xcode-select --install
brew install cmake stockfish qt
```

Apple Silicon and Intel macOS are supported. The subprocess module also targets
POSIX systems; Windows support is not implemented. Qt 6 Widgets is the only GUI dependency. The core and CLI have no third-party
C++ dependencies. OpenCV is not used.

## Build and test

Run from this repository's `Chess-analysis-bar` directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

Parser, score conversion, FEN, stream framing, fake-engine lifecycle, and Qt
controller/widget tests run without Stockfish. Qt tests use the offscreen platform
and cover monotonic/symmetric bar conversion, score formatting, animation,
debouncing, stale-result rejection, responsive cancellation, and shutdown. To enable the additional real-engine integration test:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DSTOCKFISH_EXECUTABLE="$(command -v stockfish)"
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

The optional executable setting is cached by CMake; set
`-DSTOCKFISH_EXECUTABLE=` to disable that test again.

## Run the Qt application

```bash
./build/chess-eval-bar
```

Two windows open: a narrow, always-on-top evaluation bar and a position/settings
window. Paste a standard-chess FEN, set the Stockfish executable and depth, then
press **Start**. White occupies the bottom and grows upward; Black occupies the
top. Drag the window edges to resize the bar. **Show bar** reopens it if hidden.

Scores use White's perspective: `+0.32` is 32 centipawns for White, `-1.50` is
150 centipawns for Black. `+M3` / `−M3` indicate forced mate, while `+M0` / `−M0`
mean Black / White is already checkmated. A logistic mapping
`1 / (1 + exp(-cp / 400))` centers zero at 50%, maps ±400 cp to approximately
73% / 27% White, and smoothly approaches the extremes. This is a display scale,
not a calibrated win probability. Mate maps to the corresponding endpoint.
Changes animate over 320 ms.

While started, FEN, depth, and engine-path edits trigger analysis after a 350 ms
pause. Unchanged requests are skipped. Edits and **Stop** invalidate the numeric
score (shown as `—`) and freeze the bar until a current result arrives. Stop also
disables automatic analysis until Start is pressed again. Errors appear in the
settings window. Scores update when the configured-depth search completes;
the GUI remains responsive throughout. The engine timeout is 30 seconds per
handshake/search. Closing the settings window cancels analysis and exits after
cleanup without blocking the event loop.

For a Qt-free CLI build:

```bash
cmake -S . -B build-cli -DBUILD_GUI=OFF
cmake --build build-cli -j 4
ctest --test-dir build-cli --output-on-failure
```

## Screen capture (M3)

The macOS capture backend requires ScreenCaptureKit (introduced in macOS 12.3).
The project defaults to a macOS 13 deployment target; your installed Qt version
may require a newer macOS. Build with a recent Apple SDK containing
ScreenCaptureKit. CMake enables Objective-C++ with ARC and links Apple's
ScreenCaptureKit, CoreGraphics, CoreMedia, CoreVideo, and Foundation frameworks.
No additional third-party capture library is required. Non-macOS builds retain
manual analysis but report capture as unsupported.

1. Launch `./build/chess-eval-bar` and open **Screen capture**.
2. Choose the display. **Refresh displays** reloads the monitor list.
3. Click **Select Region**, drag around the visible board, and inspect the
   highlighted rectangle. Press **Enter** to confirm or **Esc** to cancel.
4. The selected display, local point coordinates, and output pixel dimensions
   appear above the preview. Click **Start Capture**.
5. Click **Stop Capture** to end the stream. To change regions, stop first, select
   another rectangle, and start again. No application restart is needed.

On first Start Capture, macOS may ask for Screen Recording permission. In
**System Settings → Privacy & Security → Screen & System Audio Recording**
(**Screen Recording** on older versions), enable the application or the terminal
used to launch it. Quit and relaunch after granting permission if needed. The
backend requests permission at most once per application run, only after Start;
it never requests permission when opening the app or running automated tests.
Permission failures appear as an Error with instructions in the capture tab.

Capture targets 12 FPS, hides the cursor, and requests only the selected source
rectangle. The preview preserves aspect ratio with empty margins. Static content
may deliver fewer complete frames; incomplete/idle frames leave the last valid
preview visible. Stop clears the preview and ends capture. Keep the application
windows outside the selected area to avoid capturing the preview itself. Screen
content covered by another window is captured as visible; protected content may
be blank. Selection is rectangular, confined to one display, and stored only for
the current run. Display layout/scaling changes can require selecting again.

Capture architecture:

- `include/chess_eval/capture/types.hpp`, `src/capture/types.cpp`: display-local
  point geometry, global-to-local conversion, Retina pixel dimensions, frame
  validation, aspect fitting, and the capture session state machine.
- `src/capture/mac_backend.mm`: asynchronous ScreenCaptureKit operations on a
  serial dispatch queue; native permission checks, display enumeration, and one
  BGRA pixel copy into owned Qt image storage per complete frame.
- `src/capture/backend.cpp`, `src/capture/manager.cpp`: a thread-safe, latest-frame
  mailbox and GUI-thread delivery. A stopped or superseded session cannot update
  the preview. Native frame queues are limited to three surfaces; the mailbox
  retains only the latest frame, so a slow GUI cannot accumulate frame history.
- `src/ui/region_selector.cpp`, `src/ui/capture_panel.cpp`: transparent selection
  overlay, capture controls/status, and an aspect-preserving preview.

Future frame consumers can connect to `capture::Manager::frameReady`. A `Frame`
contains an owned `QImage` (width, height, stride, BGRA/ARGB32 layout), monotonic
media timestamp in nanoseconds, session ID, source region, display ID, global
logical display bounds, and display scale. Images are implicitly shared and must
be treated as immutable after publication. A heavy future consumer should use a
bounded mailbox instead of queueing every frame. No recognition or FEN generation
is included in M3.

### Verification

`ctest --test-dir build --output-on-failure` includes the existing M1/M2 suites
and `capture_logic`. On macOS, `native_capture_lifetime` also exercises deferred
ScreenCaptureKit enumeration and stop callbacks with mocked native responses,
verifying session ownership after the initiating function returns without screen
access. The capture suite uses mock frames without screen permission
and tests invalid/out-of-bounds rectangles, negative monitor origins, Retina and
fractional scales, frame metadata, aspect ratios, session transitions, stale
frames/errors, bounded latest-frame delivery, preview rendering, and confirming
a region followed by pressing Start Capture.

Manual macOS checks remain necessary: grant/deny actual screen permission; select
boards on Retina and external displays (including displays left of the primary);
compare the visible crop with the selection; resize the preview; stop/restart
rapidly; unplug a display; and close the application during capture. Verify manual
FEN analysis still works independently. Automated tests do not verify real screen
pixels, native permission dialogs, or physical monitor alignment.

## Run the CLI

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
  output formatting, and the tested logistic White-share mapping.
- `src/engine/engine.cpp`: `uci`/`uciok`, `isready`/`readyok`, position submission,
  search, exact-score selection, and completion coordination.
- `src/chess/fen.cpp`: six-field FEN validation, board dimensions, kings, pawn
  placement, castling-piece consistency, en passant consistency, and counters.
- `src/main.cpp`: CLI argument handling and evaluation-only output.
- `src/ui/controller.cpp`: debounced immutable requests, one owned worker at a
  time, cancellation, and generation-tagged results delivered on the GUI thread.
- `src/ui/evaluation_bar.cpp`: black/white painting and smooth ratio animation.
- `src/ui/window.cpp`: manual FEN/settings and asynchronous window shutdown.

FEN validation checks structure and basic position consistency, not full legal
reachability or game history. Supply a legal standard-chess position; Chess960
and legal move tracking are outside M1.

Each synchronous `analyze` call owns a fresh process and discards its state on
success or failure. Shutdown sends `quit`, allows a short grace period, then
uses SIGTERM/SIGKILL if needed and reaps the child. A subsequent call starts
cleanly. The UCI completion marker is consumed only to detect search completion;
its move and any PV content are never exposed.

The core library has no Qt dependency. Its optional cancellation callback is
checked during I/O at intervals of at most 50 ms. Cancellation sends UCI `stop`
and drains completion for up to 300 ms, then the existing RAII shutdown reaps the
process. The controller starts the next process only after the old worker has
finished; stale generations cannot publish scores or errors. No threads detach.
This isolated-search design favors predictable cleanup over persistent-engine
reuse. Future capture/recognition modules can submit positions to the controller
without changing the bar or UCI parser.
