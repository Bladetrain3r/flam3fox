# flam3fox UI

An Apophysis-inspired interactive viewer/editor built on `libflam3` with
Dear ImGui. Linux-first.

## Architecture

- **`render_engine.{h,cpp}`** — `RenderEngine`, a threaded, progressive wrapper
  around `libflam3`. The GUI edits the master genome and calls
  `requestRerender()`; a background worker renders at geometrically increasing
  quality into an RGBA8 buffer, aborting in-flight renders the moment a newer
  edit arrives (via the flam3 progress callback). No GUI dependency, so it is
  unit-testable headless.
- **`app.cpp`** — the GLFW + OpenGL + Dear ImGui glue: a controls panel
  (load, target quality, camera, tone, per-xform weights) and a live preview.
- **`render_engine_test.cpp`** — headless smoke test (no display needed).

The render core (`flam3_iterate`/`flam3_render`, including the multithreaded
per-thread-bucket path and the AVX2 SIMD fast path) is reused as-is; the engine
just orchestrates it. On a many-core machine the worker's `flam3_render` call
uses all cores, so previews refine quickly.

## Build

Dependencies: a C++17 compiler, CMake ≥ 3.16, GLFW3, OpenGL, and the libflam3
deps (libxml2, libpng, libjpeg, zlib). Dear ImGui is vendored as a git
submodule.

```sh
# from the repo root, once:
git submodule update --init ui/third_party/imgui

# Debian/Ubuntu deps:
sudo apt-get install build-essential cmake libglfw3-dev libgl-dev \
                     libxml2-dev libpng-dev libjpeg-dev zlib1g-dev

cd ui && cmake -S . -B build && cmake --build build -j
```

This compiles the flam3 C sources directly (with `-O3 -ffast-math
-march=native`, so the SIMD path is available) — no autotools step required.

## Run

```sh
cd ui/build
# palette file location (defaults to ../../flam3-palettes.xml if unset):
export flam3_palettes=../../flam3-palettes.xml
./flam3fox_ui ../../bench/scene.flam3     # GUI (needs a display)
./render_engine_test ../../bench/scene.flam3 out.ppm 200   # headless check
```

## Status / next

Implemented: load `.flam3`, progressive threaded preview, click-to-type camera
(center/zoom/rotate/scale), tone (brightness/gamma/vibrancy), per-xform weight
edits, **Save PNG**, **Save .flam3** (parameter export, round-trips through the
parser), and a **Random scene** panel (tunable xform-count range, symmetry,
framing/zoom range, size, and an option to restrict to the AVX2-supported
variations), auto-framed to fit.

Planned: interactive triangle/affine xform editor, variation parameter editing,
palette/gradient editor, a real file-open dialog, save `.flam3`, mutate/cross,
and promoting the AVX2 SIMD path to the preview (`flam3_simd`).

## CLI builds (Docker)

The repo's committed autotools files were generated with automake 1.15, so a
plain `make` on a newer host may try to regenerate them and fail. The
`Dockerfile` at the repo root builds the CLI reproducibly (it runs
`autoreconf -fi`):

```sh
docker build -t flam3fox .
docker run --rm -v "$PWD:/workspace" flam3fox \
    bash -lc 'autoreconf -fi && ./configure && make -j"$(nproc)" && ./flam3-render < test.flam3'
```
