# flam3fox

An optimized fork of [`flam3`](http://flam3.com) — Scott Draves' cosmic
recursive fractal flame renderer — with a multithreaded/SIMD render core and a
small Apophysis-inspired interactive editor.

> Base `flam3` is unchanged in spirit: same algorithm, same `.flam3` files, same
> CLI tools. flam3fox makes it **faster** and adds a **GUI** on top of the
> library.

## Highlights

- **~2× faster rendering** than base flam3 at matched parameters, from:
  - **Per-thread histogram buffers** — removes the atomic/lock contention in the
    accumulator; parallel efficiency at 4 cores went from ~40% to ~90%.
  - **`-march=native` + LTO** build flags (overridable via `OPT_FLAGS`).
  - An **opt-in AVX2 SIMD chaos game** (`flam3_simd=1`) covering 12 common
    variations, with a masked/binning hybrid and automatic scalar fallback.
- **Interactive UI** (`ui/`) — Dear ImGui + GLFW/OpenGL editor on `libflam3`:
  threaded progressive preview, camera/tone/weights, random-scene generator,
  and PNG / `.flam3` export.
- **Reproducible CLI builds** via Docker.
- **Bit-exact correctness** is guarded by an image-regression harness
  (`bench/`); every optimization is verified to preserve output.

### Measured (examples)

| Workload | base flam3 | flam3fox | 
| --- | --- | --- |
| 1024², quality 1000 (24 threads) | ~6.0 s | ~2.5 s |
| 1920×1080, quality 500 (24 threads) | ~24 s | ~12.5 s |

The AVX2 SIMD path adds up to ~2.5× single-thread on trig-heavy scenes where it
is eligible (stacks on top of threading).

## Build & run — CLI

The committed autotools files predate current automake, so the easiest path is
Docker (it runs `autoreconf -fi`):

```sh
docker build -t flam3fox .
docker run --rm -v "$PWD:/workspace" flam3fox \
    bash -lc './flam3-render < test.flam3'
```

Native build (needs `autoconf automake libtool` + `libxml2-dev libpng-dev
libjpeg-dev zlib1g-dev`):

```sh
autoreconf -fi && ./configure && make -j"$(nproc)"
./flam3-render < test.flam3            # writes 00000.png
nthreads=8 flam3_simd=1 ./flam3-render < test.flam3   # threads + SIMD
```

## Build & run — UI

```sh
git submodule update --init ui/third_party/imgui
sudo apt-get install build-essential cmake libglfw3-dev libgl-dev \
                     libxml2-dev libpng-dev libjpeg-dev zlib1g-dev
cd ui && cmake -S . -B build && cmake --build build -j
cd build && export flam3_palettes=../../flam3-palettes.xml
./flam3fox_ui ../../bench/scene.flam3
```

See [`ui/README.md`](ui/README.md) for details.

## Repository layout

| Path | What |
| --- | --- |
| `*.c`, `*.h` | the `libflam3` library + CLI tools (`flam3-render`, `-animate`, `-genome`, `-convert`) |
| `bench/` | benchmark + golden-image regression harness |
| `ui/` | the Dear ImGui editor (`RenderEngine` + GLFW/OpenGL app) |
| `docs/ROADMAP.md` | phased plan and status |
| `docs/REVIEW.md` | decision log — *why* each change was made (D1–D16) |
| `Dockerfile` | reproducible CLI build environment |

## Benchmark / regression

```sh
bench/benchmark.sh            # speedup across thread counts
bench/regression.sh          # render and diff against the golden image
```

## License

GPLv3, inherited from `flam3` (© 1992–2009 Scott Draves / Spotworks LLC). See
[`COPYING`](COPYING). flam3fox's additions are released under the same license.
The original project documentation is preserved in [`README.txt`](README.txt).
