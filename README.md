# Chess Evaluation Bar — M5 Live Analysis

A C++20 / Qt 6 desktop application with a small, resizable, floating black-and-white
evaluation bar and a manual FEN settings window. Stockfish evaluates each position
asynchronously. The original evaluation-only CLI is also included. A separate Screen capture tab
provides region selection and a live preview using ScreenCaptureKit. A Recognition
tab provides OpenCV template calibration and manual recognition. The Live analysis
tab connects capture, recognition, legal position tracking, and Stockfish when
explicitly started. Analysis output is limited to the evaluation bar and score.

## Install prerequisites (macOS)

```bash
xcode-select --install
brew install cmake stockfish qt opencv
```

Apple Silicon and Intel macOS are supported. The subprocess module also targets
POSIX systems; Windows support is not implemented. Qt 6 Widgets and OpenCV (core, imgproc, imgcodecs) are used by the GUI/vision
build. The engine core and CLI remain free of those dependencies. There is no
Python runtime dependency.

## Build and test

Run from this repository's `Chess-analysis-bar` directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix opencv)"
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

Parser, score conversion, FEN, stream framing, fake-engine lifecycle, and Qt
controller/widget tests run without Stockfish. Qt tests use the offscreen platform
and cover monotonic/symmetric bar conversion, score formatting, animation,
debouncing, stale-result rejection, responsive cancellation, and shutdown. To enable the additional real-engine integration test:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix opencv)" \
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

## Manual recognition and calibration (M4)

Two separate profile presets are available: **Chess.com · Green / Classic** and
**Lichess · Brown / Cburnett**. They configure separate template directories; they
are not pre-trained recognizers. **No reference artwork is bundled, and real-theme
accuracy is unverified until permitted screenshots are supplied and calibrated.**
Use **Custom profile** and a separate directory for another theme. Switching
profiles restores that profile's chosen directory for the current run. Default
profile directories are under Qt's application-data location, displayed in the
Templates field. Do not mix themes, piece sets, or background palettes in one
folder. Keep the same browser zoom and board settings during calibration/use.

### Set up templates in the GUI

1. Configure the desired board theme and piece set on the site. Use a permitted
   local board editor/reference position with known piece placement. Disable move
   highlights, arrows, coordinate labels inside squares, and animations if possible.
2. In **Screen capture**, select exactly the visible 8×8 board: no surrounding
   labels, border, toolbar, or clocks. Start capture and keep the app outside it.
3. In **Recognition**, choose the matching profile and **White at bottom** or
   **Black at bottom**. The saved/recognized notation always uses canonical a8–h1
   order; the orientation control describes the screenshot.
4. Enter the board's exact **piece-placement field only** into the calibration
   input. For the initial position this is:

   ```text
   rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR
   ```

5. Click **Save labeled reference squares to this profile**. This explicitly
   snapshots the latest frame and exports normalized, labeled PNG templates in
   the background. Labels are trusted calibration input: incorrect labels will
   train the wrong class. No position is inferred for calibration.
6. Repeat with known reference arrangements until every piece of each color and
   empty squares has an example on **both light and dark backgrounds** (26 class/
   background combinations). The initial position alone is insufficient: kings
   and queens appear on only one background each. A board editor can provide the
   additional arrangements; calibration positions need not be legal game states.
7. Click **Recognize Position**. An incomplete template set produces a specific
   missing-class error. Add the missing examples, then recognize again.

The exporter saves at most one example per class/background per click and appends
variants without overwriting existing files. Files are 48×48 PNGs, for example
`wP_l_0.png`, `bK_d_0.png`, and `ee_l_0.png` (`ee` means empty). Names distinguish
colors even on case-insensitive macOS disks. The loader accepts up to 512 variants;
remove incorrect/redundant calibration files rather than repeatedly adding the
same board. Templates are reloaded on each explicit recognition request.

### Calibrate from saved permitted screenshots

The C++ tool provides the same preprocessing/export path without a GUI:

```bash
./build/chess-template-tool /path/to/board.png \
  'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR' \
  white /path/to/templates/chesscom-green-classic
```

Use `black` for Black at the bottom and a separate directory for
`lichess-brown-cburnett`. Repeat for additional correctly labeled reference boards.
Select that directory with **Browse…** in the corresponding profile. **Save
analyzed snapshot…** can export the exact immutable snapshot from a recognition
request for debugging (even if recognition failed due to missing templates).

### Read results

The debugging board uses standard FEN letters: uppercase White, lowercase Black,
`.` empty, and highlighted `?` unknown. Hover over a square for its coordinate and
best-match similarity. The board is displayed in canonical White-bottom order.
A placement string and **Copy placement** are enabled only if all squares are
known and basic structural checks pass. Validation checks one king per side,
piece/pawn totals, promotion-material limits, and pawns on promotion ranks. It
is **not full chess legality or reachability checking**.

Similarity is `1 - normalized pixel RMSE`, computed with OpenCV template matching.
Defaults require at least 0.92 similarity and a 0.025 advantage over the next
*distinct class*. Similarity is not a calibrated probability; even a 99% best
match can remain Unknown if two classes are too close. Light/dark backgrounds
are matched separately. Matching ignores an eight-pixel template rim and searches
within ±7 normalized pixels, with half-pixel alignment refinement and cached 97%, 100%, and 103% size variants,
to tolerate small crop offsets, size differences, and edge labels;
large offsets still require selecting the board more precisely. Existing saved
templates remain compatible. Green selection/legal-square backgrounds and
green/yellow last-move and gray-purple premove backgrounds are compensated
when the square's corner patches agree on a uniform background and its color
change is within the supported range. Background pixels and anti-aliased blends
with black/white artwork are reconstructed against the reference background,
including the fine outlines of selected queens. Pixels inconsistent with those
blends remain unchanged; piece artwork and obstructions must still pass the
same similarity and ambiguity thresholds. Small centered green legal-move and gray-purple premove
dots are removed only when the surrounding square is empty and their color and
circular shape match. Dot detection tolerates small crop offsets and samples
inside square edges when neighboring colors contaminate the corners; low-contrast
green dots on dark squares are also supported. Rings or overlays that obscure
piece artwork may still require deselecting the piece. No highlight templates are required. Thresholds are explicit `vision::Thresholds` parameters
in the core API. Do not lower them simply to make uncertain boards appear valid.

Inputs must be full, axis-aligned boards, 128–8192 pixels per side with no more
than 4% aspect distortion. Preprocessing resizes to 384×384, applies a small
Gaussian blur, and extracts 64 48×48 squares. Blurred, obscured, unusually highlighted,
misaligned, or unfamiliar artwork can yield Unknown or incorrect matches.
Matching does not support arbitrary themes. Both orientations and different
resolutions are tested synthetically, but real browser/Retina alignment and
site-specific rendering still need manual verification with labeled screenshots.

### Concurrency, tests, and measured results

Recognition runs only when explicitly requested. A single persistent worker
handles one active request and at most one replacement request. Generation IDs
reject older results after a newer request, profile/orientation changes, capture
stop, or region changes. Incoming frames only replace the latest immutable frame;
they do not start recognition. Pixel conversion, template I/O, classification,
calibration export, and snapshot encoding run off the GUI thread. Closing the
window shuts the worker down before destroying the UI. No recognition code calls
Stockfish or supplies a full FEN: side to move, rights, and counters remain unknown.

New modules are `vision/board` (Qt/OpenCV-independent representation/validation),
`vision/recognizer` (geometry, preprocessing, replaceable PieceClassifier interface,
template matching), `vision/qt_image` (stride-safe owned image conversion), and
`ui/recognition_controller` / `ui/recognition_panel` (worker and debugging UI).

Run all tests with `ctest --test-dir build --output-on-failure`. To see accuracy and
latency output, run `ctest --test-dir build -R vision_recognition -V`. Fixtures in
`tests/fixtures/vision` are explicitly synthetic OpenCV text-glyph boards, not
website reference assets. The measured Debug/OpenCV 5 run on this Apple Silicon
Mac achieved **128/128 squares and 2/2 full boards**, averaging **77.3 ms per board**
for recognition on 384×384 and noisy 768×768 inputs with 26 templates. This timing
covers preprocessing/classification/validation, excluding disk template loading,
Qt conversion, queueing, and capture. More variants increase runtime. These
synthetic figures do not estimate Chess.com or Lichess accuracy; both remain
**unmeasured**. The initial low-contrast synthetic glyph trial was rejected by the
ambiguity margin; the saved fixtures use a higher-contrast test palette.

Tests additionally cover fractional crop shifts of ±4.5 normalized pixels combined
with ±1% board-size changes and isolated pixel noise, all 13 classes on both
backgrounds, empty/invalid images,
geometry, padded image rows, ambiguous templates, FEN placement validation,
request coalescing, stale-result rejection, GUI responsiveness, profile isolation,
and the captured-frame → Recognize button → displayed placement path. Existing
M1–M3 tests remain enabled. Manual testing with your site screenshots and actual
screen permissions is still required. Live tracking is described below.

## Live analysis (M5)

1. Calibrate the desired Chess.com or Lichess profile in **Recognition**, including
   all piece/background combinations. The orientation setting there applies to
   manual recognition/calibration; live analysis detects orientation automatically.
2. Select the board region and **Start Capture** in **Screen capture**.
3. Set the Stockfish executable and depth in **Manual analysis**.
4. Open **Live analysis** and click **Start Live Analysis** before the first move.
   Keep the standard starting board visible until the position is confirmed.
   White-at-bottom and Black-at-bottom boards are detected automatically; White
   still moves first regardless of which color you play. No FEN or turn setting
   is required. Midgame initialization is not supported.
5. **Stop Live Analysis** cancels analysis and freezes the bar. Start resumes the
   tracked game when capture and recognition settings are unchanged, retaining
   the detected orientation and full game state.
6. **Reset** cancels outdated work, clears tracked history, and waits for a new
   standard starting board. It detects the orientation again for the new game.

Each new placement must remain consistent across at least two recognition
observations spanning 300 ms and match an unambiguous legal continuation from the
tracked position. The tracker first searches two plies (one move and one reply), then automatically
widens recovery to four plies when needed, so fast moves between captured
observations can be recovered without pressing Reset.
All matching paths must agree on the complete FEN, including hidden state.
Moves that expose the moving side's king are rejected. Castling,
en passant, captures, and all four promotion choices update the full FEN
internally, including rights and counters. This supports standard chess;
Chess960 is not supported.

Animations, unknown squares, and unmatched placements suspend the current
score once observed. The bar stays frozen and the score shows `—`, but live
recognition keeps watching without a three-second shutdown deadline. The status
identifies unreadable squares. A stable current position or a unique continuation
of at most four plies recovers automatically, including after longer obstructions. Unmatched boards
never overwrite the tracked state. Extended recovery is cancellable and limited
to 100,000 search nodes; an incomplete search never publishes a partial match. If more than four plies were missed, or legal
paths disagree about the full FEN, tracking can remain frozen. Use Reset before the first move of the next game.
The app never silently resets history or guesses which side should move.

Pixels cannot establish castling rights, en passant, or move counters. Live
analysis starts with standard-game rights and counters, then updates these fields
from legal moves. A FEN does not encode repetition history, so the app does not adjudicate
repetition draws. Standard chess is supported; Chess960 is not supported.

Changing capture session/region discards the old tracking session and stops live
analysis. Changing recognition settings stops live analysis; changing the template
path or orientation initializes a new tracking session on Start. Manual
recognition/calibration also stops live tracking before modifying its inputs.
Starting manual analysis switches out of live mode. Both modes share one engine
controller, so searches cannot overlap. Template errors pause live analysis with
an actionable message. Engine errors appear in the Live analysis tab; correct the
settings and Stop/Start to retry the retained game. **Reset** establishes a new
standard starting baseline for a new game; Stop/Start alone does not reset history.

Recognition and bounded legal recovery run on a persistent worker, at most four
requests per second, with one in-flight request and one latest captured frame. Tracking uses a worker-owned
copy and only publishes it if the request generation is still current. Recovery
is cancellable, enumerates legal successors without repeated move validation,
and caches failed placements until the candidate or authoritative state changes.
Templates are cached for the session and reloaded on Start; identical images reuse the last recognition result.
ScreenCaptureKit may not deliver new frames for a static screen, so the latest
unchanged image is retained for stability checks until a new frame or capture-stop
notification arrives. No image processing or engine I/O runs on the Qt thread.
Confirmed live positions launch the cancellable engine controller immediately,
skipping the 350 ms debounce reserved for manual FEN edits. The live bar shows an
early white-relative exact score from depth 8 (or a mate score), then refines it
while Stockfish continues to the configured depth. Early scores may change as
search deepens; the status remains **Analyzing…** until completion. Intermediate
updates are limited to ten per second. Generation IDs discard both intermediate
and final results from stopped or superseded work, and engine errors invalidate
any preliminary score. Unchanged tracked positions do not trigger additional
searches. Board stability and legal-move checks are unchanged. The recognition debug board remains
an explicit M4 troubleshooting tool; live mode displays no moves or engine lines.

Modules:

- `chess/game`: standard-chess legal moves and complete FEN state.
- `chess/tracker`: stable observations, unique legal transitions, and safe recovery.
- `ui/live_controller`: bounded asynchronous recognition, template caching, capture
  lifecycle handling, and confirmed-position/suspension signals.
- `ui/window`: live controls and integration with the shared evaluation controller.

`position_tracking` tests standard move-tree counts (including castling and en
passant positions), checks/pins, promotions, complete FEN transitions, animations,
resets, two- through four-ply recovery (including castling, en passant and
promotion), search cancellation, and ambiguous histories that must stay frozen. `live_analysis` runs synthetic captured boards through
real OpenCV recognition, tracking, the UCI fake engine, and the evaluation widget
in both orientations. It also covers both profile selectors, slow recognition,
rapid frame replacement, skipped intermediate boards followed by automatic
evaluation, automatic starting-board orientation for both colors,
Stop/Start orientation retention, Reset with a flipped board, highlighted move squares,
stale-result cancellation, capture loss, template errors,
and responsive shutdown. Existing M1–M4 tests and optional real-Stockfish tests
remain enabled. These automated checks do not establish live-site recognition
accuracy; real screen permissions, animations, and theme calibration still require
manual verification on the user's display.

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
