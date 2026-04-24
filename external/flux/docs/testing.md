# Testing

## Overview

| Suite | Location | Status |
|---|---|---|
| Foundation unit tests | `tests/test_foundation.c` | **Shipped** |
| Unit tests | `tests/test_*.c` | **Shipped** |
| Integration (headless/offscreen) | `tests/test_offscreen.c`, `test_gradient.c`, `test_clip.c`, `test_missing_apis.c` | **Shipped** |
| Golden-image tests | `tests/test_render_golden.c` | Planned |
| Performance benchmark | `bench/bench_ui.c` | Phase 3 |

The current automated gate is the unit suite plus headless/offscreen Vulkan
integration tests. Golden-image tests and performance benchmarks are still
planned release-hardening work.

## Running tests

```sh
meson setup build -Dtests=true
ninja -C build
meson test -C build
```

Individual suites:

```sh
meson test -C build --suite unit
meson test -C build --suite integration
```

For release checks, prefer serial execution so software Vulkan drivers do not
share device state across test processes:

```sh
meson test -C build --suite unit --suite integration --num-processes 1
```

Tests can run with `FX_ENABLE_VALIDATION=1`. A validation error at
`VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT` level causes the test
to fail regardless of the rendered output.

## Foundation unit tests (shipped)

### `test_foundation.c`

Tests the CPU-only low-level object model and recorder state.

Assertions:
- `fx_path` records verbs/points correctly and tracks bounds.
- `fx_image` applies default stride/usage rules and retains a private
  pixel copy.
- `fx_font` and `fx_glyph_run` preserve explicit metadata and glyph
  placement.
- `fx_canvas` records fill/stroke/image/glyph ops in order and resets
  cleanly between frames.

This suite does not create a Wayland surface and does not require a
working presentation path.

## Unit tests (phase 1+)

### `test_stroker.c`

Tests the CPU-side stroke-to-fill expander in isolation. Does not
require Vulkan.

Inputs: synthetic polylines (line segments, L-shapes, star).
Assertions:
- Triangle count matches the analytic expectation for the cap/join
  combination.
- All output triangles are non-degenerate (positive area).
- Bounding box of the output geometry matches stroke width + cap
  extension.

### `test_tess.c`

Tests the polygon tessellator in isolation. Does not require Vulkan.

Inputs: convex polygon, concave polygon, polygon with a hole,
degenerate (zero-area) input.
Assertions:
- Triangle count matches the expected ear-clipping count.
- Sum of triangle areas matches the analytically known polygon area
  to within floating-point tolerance.
- No degenerate triangles in output.

### `test_path.c`

Tests the path flattener (curves → polylines).

Inputs: quadratic and cubic Béziers at various tolerances.
Assertions:
- Segment count is within `[lo, hi]` for the given `device_scale`.
- End points of the polyline are exactly the stated endpoints of the
  curve (no drift).
- Maximum deviation of any midpoint from the true curve is ≤
  `tolerance` pixels.

### `test_shape_cache.c`

Tests that identical paths hashed into the glyph/shape cache return
the same tessellation buffer without re-tessellating. Asserts cache hit
rate ≥ 99% for a repeated-draw workload.

## Golden-image tests (phase 1+)

`tests/test_render_golden.c` renders a fixed set of scenes to an
offscreen `fx_surface` created with `fx_surface_create_offscreen`,
reads pixels back with `fx_surface_read_pixels`, and compares against
reference PNGs stored under `tests/golden/`.

### Pass/fail criterion

A golden test passes when, for every pixel:

    channels_above_threshold = sum(|actual[c] - expected[c]| > 4
                               for c in {R, G, B})
    if channels_above_threshold >= 3: fail

Put plainly: at most 2 colour channels may differ by more than 4
out of 255 at any single pixel, and even then it must not affect all
three simultaneously. Alpha differences are not measured (blend mode
coverage around antialiased edges may vary across driver versions).

### Updating goldens

When a rendering change is intentional (new AA algorithm, fixed
tessellation bug):

```sh
FX_UPDATE_GOLDENS=1 meson test -C build --suite golden
```

Review the diffs with an image diff tool before committing. Never
update a golden to hide a regression — golden diffs should always
be reviewed by eye.

### Current golden scenes (planned)

| Scene | Covers |
|---|---|
| `solid_rect` | axis-aligned fill, solid color |
| `stroke_line` | line stroke, butt caps |
| `stroke_round` | round caps and joins |
| `rrect` | rounded rectangle fill |
| `circle` | circle fill |
| `linear_gradient` | linear gradient, horizontal |
| `radial_gradient` | radial gradient |
| `image_blit` | image upload and draw |
| `text_run` | shaped text, 16px |
| `text_large` | SDF path, 80px |
| `clip_rect` | rect clip |
| `blend_modes` | side-by-side all 7 blend modes |
| `blur_shadow` | drop shadow and blur |

## Integration tests

The shipped integration tests create offscreen surfaces and read pixels back
without a display server. CI forces Mesa lavapipe with `VK_ICD_FILENAMES` so the
tests can run on hosted runners without a physical GPU.

Wayland example smoke tests are still useful manual checks, but they are not
part of the current automated release gate.

## Performance benchmark (phase 3+)

`bench/bench_ui.c` replays a synthetic UI scene:

- 1 panel fill (full-screen rect)
- 8 card rects with rounded corners
- 4 text labels of 60–120 glyphs
- 12 icon images (32×32 px)
- Total: ~200 draw ops per frame

The benchmark replays 10 000 frames and reports:

```
p50:  X.XX ms
p95:  X.XX ms
p99:  X.XX ms
peak: X.XX ms
```

Pass criteria (§1.3 of the design brief):

| GPU | Full-screen 4K ~200 ops | Text run 1000 glyphs |
|---|---|---|
| Intel Iris Xe (integrated) | < 4 ms GPU | < 2 ms record + submit |

The benchmark is excluded from `meson test`; run it explicitly:

```sh
./build/bench/bench_ui
```

## Validation in tests

Validation-layer runs should set `FX_ENABLE_VALIDATION=1`. Tests that install a
custom `fx_log_fn` record every log message. After the test body runs, the
harness asserts:

```c
assert(error_count == 0);
```

where `error_count` is the number of `FX_LOG_ERROR` messages. A
single validation error fails the test, even if the rendered output
matches the golden.
