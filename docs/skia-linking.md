# Skia Linking Guide

## Why Static Linking

Typio links Skia statically (`is_component_build = false`). The reasons:

- Skia has no stable C ABI and no versioned shared library contract. Distribution packages are rare and version-specific.
- Static linking embeds the exact built Skia version into the `typio` binary, eliminating runtime version mismatches.
- Skia is an internal rendering detail — it is not exposed in any public Typio API, so there is no benefit to a shared library.
- The tradeoffs are a larger binary (~30 MB from `libskia.a` alone) and longer link times, both acceptable for a daemon that is built once and installed.

Dynamic linking (`is_component_build = true`) is not supported. Skia does not ship a stable `.so` with versioned symbols, so any Skia `.so` would be tightly coupled to a specific build anyway — you would gain the cost (runtime dep, version management) without the benefit (ABI stability).

## Prerequisites

### System packages

Debian/Ubuntu:

```bash
sudo apt install build-essential python3 ninja-build \
    libfontconfig1-dev libfreetype-dev libharfbuzz-dev \
    libicu-dev libvulkan-dev
```

Arch Linux:

```bash
sudo pacman -S base-devel python ninja \
    fontconfig freetype2 harfbuzz icu vulkan-headers
```

Fedora:

```bash
sudo dnf install gcc-c++ python3 ninja-build \
    fontconfig-devel freetype-devel harfbuzz-devel \
    libicu-devel vulkan-headers
```

### Skia submodule

Skia lives at `external/skia` as a git submodule. Initialize it once:

```bash
git submodule update --init external/skia
```

## Building Skia

All commands run from `external/skia/` unless stated otherwise.

### 1. Sync Skia's third-party dependencies

```bash
cd external/skia
python3 tools/git-sync-deps
```

This pulls Skia's bundled third-party sources (e.g. `third_party/externals/`). It is required before the first build and after any Skia submodule update.

### 2. Generate the build directory

Skia ships its own `gn` binary. Use it directly — no separate GN installation needed.

```bash
bin/gn gen out/Release --args='
  is_official_build = true
  is_debug = false
  is_component_build = false

  skia_use_vulkan = true
  skia_use_gl = false
  skia_use_harfbuzz = true
  skia_use_icu = true
  skia_use_libjpeg_turbo = false
  skia_use_libpng = false
  skia_use_libwebp = false
  skia_use_zlib = true

  skia_enable_pdf = false
  skia_enable_skottie = false
  skia_enable_svg = false
  skia_enable_ganesh = true
  skia_enable_graphite = true

  extra_cflags = [ "-DSK_VULKAN" ]
'
```

The canonical args are also stored in `external/skia/out/Release/args.gn` as a reference.

### 3. Build only the required targets

```bash
ninja -C out/Release skia skparagraph skshaper skunicode_icu skunicode_core
```

Building the full Skia suite takes significantly longer. The five targets above are the only ones Typio needs.

Expected output after a successful build:

```
out/Release/libskia.a           (~30 MB)
out/Release/libskparagraph.a    (~800 KB)
out/Release/libskshaper.a       (~166 KB)
out/Release/libskunicode_icu.a  (~116 KB)
out/Release/libskunicode_core.a (~15 KB)
```

## Build Argument Reference

| Argument | Value | Reason |
|----------|-------|--------|
| `is_official_build` | `true` | Full optimizations; disables debug assertions and developer-only checks |
| `is_debug` | `false` | Release mode |
| `is_component_build` | `false` | Static `.a` output; required for Typio's static linking approach |
| `skia_use_vulkan` | `true` | Hardware-accelerated GPU backend for candidate popup rendering |
| `skia_use_gl` | `false` | OpenGL disabled; Vulkan is the sole GPU backend |
| `skia_use_harfbuzz` | `true` | HarfBuzz text shaping; required for correct CJK and complex-script rendering |
| `skia_use_icu` | `true` | ICU Unicode support; required by `skunicode_icu` |
| `skia_use_libjpeg_turbo` | `false` | Not needed for UI rendering |
| `skia_use_libpng` | `false` | Not needed for UI rendering |
| `skia_use_libwebp` | `false` | Not needed for UI rendering |
| `skia_use_zlib` | `true` | Used internally by some Skia paths |
| `skia_enable_pdf` | `false` | Disabled; reduces binary size |
| `skia_enable_skottie` | `false` | Animation module not needed |
| `skia_enable_svg` | `false` | SVG module not needed |
| `skia_enable_ganesh` | `true` | Legacy GPU-accelerated drawing backend |
| `skia_enable_graphite` | `true` | Modern GPU backend; required for Vulkan rendering path |
| `extra_cflags` | `["-DSK_VULKAN"]` | Compile-time flag that activates Vulkan code paths across Skia headers |

## How CMake Picks Up Skia

`cmake/skia.cmake` is included from `src/apps/typio/CMakeLists.txt` after the `typio` target is defined. It does three things:

**1. Header search paths** (with `SYSTEM` to suppress warnings from Skia's own headers):

```cmake
target_include_directories(typio SYSTEM PRIVATE
    ${SKIA_DIR}                           # root (for includes like "include/core/SkCanvas.h")
    ${SKIA_DIR}/include
    ${SKIA_DIR}/include/core
    ${SKIA_DIR}/include/gpu
    ${SKIA_DIR}/modules/skparagraph/include
    ${SKIA_DIR}/modules/skunicode/include
)
```

**2. Static library link order** (order matters — dependents before dependencies):

```cmake
target_link_libraries(typio PRIVATE
    libskparagraph.a    # text layout (depends on skshaper and skia)
    libskshaper.a       # text shaping (depends on skia)
    libskunicode_icu.a  # ICU Unicode (depends on skunicode_core)
    libskunicode_core.a # core Unicode
    libskia.a           # main library (must come last)
    dl pthread fontconfig freetype harfbuzz icuuc
)
```

The system libraries (`fontconfig`, `freetype`, `harfbuzz`, `icuuc`) satisfy the transitive runtime dependencies that Skia's static archives expect to find at link time.

**3. C++ standard enforcement:**

```cmake
set_target_properties(typio PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
```

Skia requires C++17. This property is set on the `typio` target rather than globally to avoid forcing C++17 on pure-C components.

Both `SKIA_DIR` and `SKIA_OUT_DIR` are derived from `CMAKE_SOURCE_DIR` and are not configurable via CMake options. If your Skia build lives elsewhere, edit `cmake/skia.cmake` directly.

## Troubleshooting

**`depot_tools` not found / `gn` fails to run**

Skia bundles `bin/gn` and `bin/ninja` for Linux. These are the recommended tools. Do not use a distribution-packaged GN; version mismatches cause silent misconfiguration.

**`python3 tools/git-sync-deps` fails on a network-restricted machine**

Mirror the required third-party archives and set `DEPOT_TOOLS_UPDATE=0` plus custom `SKIP_*` variables as documented in `tools/git-sync-deps`. Alternatively, copy a pre-synced `external/skia` checkout from a machine with network access.

**Link error: `undefined reference to sk_*` or `SkCanvas::*`**

The static library link order is significant. `libskia.a` must come last in the list. Check that `cmake/skia.cmake` has not been reordered.

**Link error: `undefined reference to hb_*` or `u_*`**

The system `harfbuzz` or `icu` development packages are missing. Install `libharfbuzz-dev` / `harfbuzz-devel` and `libicu-dev` / `libicu-devel` for your distribution.

**`ninja` fails with `FAILED: out/Release/libskia.a` and an ICU or HarfBuzz error**

Skia's bundled third-party ICU or HarfBuzz may conflict with the system version. Ensure `python3 tools/git-sync-deps` has been run so the bundled sources are present. Skia will prefer its bundled copies when they are present in `third_party/externals/`.

**Binary size is unexpectedly large**

`libskia.a` is ~30 MB before LTO. With `is_official_build = true`, the Skia build already applies `-O2`/`-O3`. If final binary size is critical, enable LTO at the CMake level (`-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`) to allow the linker to strip unused Skia symbols. Note that LTO significantly increases link time.

**Rebuilding after a Skia submodule bump**

```bash
git submodule update external/skia
cd external/skia
python3 tools/git-sync-deps
ninja -C out/Release skia skparagraph skshaper skunicode_icu skunicode_core
```

The `out/Release/` directory can be reused across minor Skia updates; GN handles incremental rebuilds correctly. For major version bumps, it is safer to remove `out/Release/` and regenerate from scratch.
