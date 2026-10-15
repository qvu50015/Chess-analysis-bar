# Synthetic pipeline fixtures

These images are generated test data using OpenCV's built-in Hershey text glyphs.
They are **not Chess.com or Lichess artwork**, production templates, or evidence
of real-theme accuracy. No website screenshots or third-party piece assets are
bundled. The generator source is `tests/generate_vision_fixtures.cpp`.

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
