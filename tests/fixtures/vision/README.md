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

Highlight regressions recolor the uniform background of all synthetic classes
and the real e2 pawn crop, leaving artwork intact. The live integration test
recolors both the empty source and occupied destination squares after moves,
in both orientations. These are simulated green/yellow highlights; an additional
central occlusion must still be rejected.

`legal-dots.png` is the user-provided October 15 screenshot of green legal-move
indicators on light and dark squares, used to test empty-square recognition.

`lichess-selected-pawn.png` is the user-supplied full-board capture with a
selected black d7 pawn and legal dots on d6/d5, with Black at the bottom.
`lichess-user-profile/` contains the existing saved Lichess calibration used
to verify this capture independently (not templates exported from the capture).

`lichess-premove-dots.png` is the user-provided White-bottom capture with
gray-purple premove dots on d3/d4 and a selected white d2 pawn. Tests use the
existing saved Lichess profile, including crop-offset and occlusion cases.

`lichess-premove-backgrounds.png` and `lichess-selected-queen.png` are the
user-provided full-board snapshots of queued premove source/destination
backgrounds and a selected queen on light g4, respectively. Both must recognize
the same placement with the independently saved Lichess profile. The queen test
also verifies that an unrelated opaque obstruction remains unknown.

`lichess-selected-dark-knight.png` is the user-provided selected g1 knight
capture. The saved knight reference has artwork in its corner patches, so this
regression verifies the template-side inner background fallback and preserves
rejection of an obscured knight.
