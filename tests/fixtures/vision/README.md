# Recognition fixtures

These images are generated test data using OpenCV's built-in Hershey text glyphs.
They are **not Chess.com or Lichess artwork**, production templates, or evidence
of real-theme accuracy. The generator source is `tests/generate_vision_fixtures.cpp`.

- `synthetic-calibration.png` and `.labels`: all 13 classes on both square colors,
  intentionally not a legal position. Used only to extract labeled test templates.
- `synthetic-white.png`: a separate starting-position arrangement at 384×384.
- `synthetic-black-retina.png`: a Black-at-bottom arrangement at 768×768 with
  deterministic Gaussian pixel noise (seed 421). Piece glyphs stay upright.
- `placement.txt`: expected canonical piece-placement for both test boards.

Regenerate from the repository root:

```bash
cmake --build build --target generate_vision_fixtures
./build/generate_vision_fixtures tests/fixtures/vision
```

Tests also explicitly reject competing identical templates and unrelated pixels.
High-contrast synthetic glyphs exercise mechanics; calibrated website artwork
may have much lower separability and requires its own labeled evaluation set.

## User-supplied capture regression

`lichess-a2.png` and `lichess-e2.png` are normalized 48×48 pawn squares
from the user-provided `lichess_snapshot.png` (2026-10-15). They reproduce
fractional-pixel alignment differences after board normalization. The a2 square
is used as a reference; e2 must classify as a white pawn without lowering the
confidence threshold. These two crops are real Lichess Cburnett artwork, not
synthetic glyphs. They verify this specific failure, not general theme accuracy.
