# flam3fox — Assessment & Roadmap

A fork of Scott Draves' `flam3` fractal flame renderer with two goals:

1. **Optimize the renderer** as far as it can reasonably go.
2. Add a **simple interactive UI** inspired by Apophysis / Apo7x.

## Direction (decided)

| Decision | Choice |
| --- | --- |
| Optimization ceiling | **CPU first** (build flags, threading, SIMD), **then a GPU backend** |
| UI stack | **Dear ImGui** (C++), linking directly against `libflam3` |
| Target platform | **Linux first** (keep portability in mind, don't gate on Win/macOS) |

---

## Codebase Assessment

### What it is
- C library (`libflam3`) + four CLI tools: `flam3-render`, `flam3-animate`,
  `flam3-genome`, `flam3-convert`. GPL3.
- ~16k lines of C, GNU autotools build.
- Dependencies: libpng, libjpeg, libz, libxml2, pthreads.
- No GUI, no GPU. Scalar double/float math throughout.

### Render pipeline (`rect.c` → `render_rectangle`)
```
for batch:
  for temporal_sample:          interpolate genome, build colormap + xform distribution
    for sub_batch (threaded):   chaos game → scatter into shared bucket buffer
  log-scale + density-estimation (DE) filter → accumulate buffer
spatial filter + gamma / vibrancy → output pixels
```

### Hot path
- `flam3_iterate` (`flam3.c:232`) → `apply_xform` (`variations.c:2129`).
- Per iteration: affine transform, then a loop over active variations dispatched
  through a `switch` (`variations.c:2181`), each doing heavy transcendentals
  (`sin`/`cos`/`atan2`/`sqrt`/`log`/`pow`).
- Points scattered into a shared accumulator via `bump_no_overflow`
  (`rect.c:411`) — no locks, benign data races.

### Baseline measurement
1280×960, quality 500, 4-vCPU dev box:

| Threads | Time |
| --- | --- |
| 1 | 73.0 s |
| 4 | 36.7 s |

→ only **~2× scaling on 4 cores**. Prime suspect: false-sharing / cache-line
bouncing on the single shared bucket buffer.

### Strengths
- The library API is already UI-ready: parse, `flam3_render` with a progress
  callback supporting **live pause/abort** (`rect.c:189`), plus
  `flam3_random`/`mutate`/`cross`/`interpolate` and full xform manipulation.
- Clean stage separation (iterate / accumulate / filter).
- Per-thread ISAAC RNG — already correct for parallelism.

### Weaknesses / opportunities
- Poor thread scaling (shared bucket buffer false-sharing).
- Build flags leave wins on the table: `-O3 -ffast-math` but no `-march=native`,
  LTO, or PGO.
- Scalar math; the chaos game can't vectorize a single trajectory, but **many
  independent trajectories can** run in SIMD lanes (the flam4/Fractorium trick).
- Transcendental-heavy variations — candidates for fast approximations.
- No GPU path — the largest available lever (10–100×).
- Autotools is dated; CMake would ease the GUI build and cross-platform later.

---

## Roadmap

### Phase 0 — Measurement harness ✅ *(done — see `bench/`)*
- Repeatable benchmark across thread counts (`bench/benchmark.sh`).
- Reference-image regression check with tolerance (`bench/regression.sh` +
  `bench/golden.ppm`) so every optimization is proven to preserve output.
- Pure-stdlib PPM comparator (`bench/compare.py`) — no external deps.
- Determinism model documented in `bench/README.md` (fixed `isaac_seed` + fixed
  thread count → reproducible; regression pins 1 thread).

Baseline (bench scene, 256², quality 1000, 4-vCPU box):

| Threads | Time | Speedup | Efficiency |
| --- | --- | --- | --- |
| 1 | 3.27 s | 1.00× | 100% |
| 2 | 2.31 s | 1.42× | 71% |
| 4 | 2.07 s | 1.58× | **40%** |

→ Parallel efficiency collapses with cores — the headline Phase 1 target.

### Phase 1 — Cheap CPU wins *(low risk, ~2–4× expected)*
- ✅ **Per-thread private bucket buffers + reduction.** Each iteration thread
  accumulates into its own histogram, removing the per-bump atomic CAS (≥3
  threads) and the accumulation mutex (≤2 threads); buffers are summed after
  join. Cost: bucket memory scales with thread count.
  - Result (bench scene, 4-vCPU box):

    | Workload | Before (4t) | After (4t) | Efficiency |
    | --- | --- | --- | --- |
    | 256², q1000 | 1.58× | **3.25×** | 40% → 81% |
    | 512², q2000 | — | **3.79×** | → 95% |

  - 1-thread output unchanged (regression bit-exact).
- Add `-march=native` / `-mtune`, LTO, and a PGO build option.
- Function-pointer variation dispatch built in `xform_precalc` (replace the
  inner-loop `switch`).

### Phase 2 — SIMD chaos game *(bigger lift, bigger payoff)*
- Iterate N independent trajectories per SIMD register (AVX2/AVX-512).
- Vectorized / fast-approximation transcendentals for hot variations.
- Restructure accumulation to handle batched lane output.

### Phase 3 — GPU backend *(the ceiling)*
- OpenCL (portable) chaos-game kernel; histogram accumulation on device.
- Keep CPU path as the correctness reference and fallback.
- Progressive accumulation for interactive preview feeding the UI.

### Phase 4 — Apophysis-style UI (Dear ImGui)
- Link against `libflam3`; live **progressive low-quality preview** (the render
  pause/abort callback already supports this).
- Editor surfaces: affine/triangle editor, xform weights, variation params,
  palette/gradient editor, camera (zoom/rotate/center).
- Load/save `.flam3`, random/mutate/cross via existing library calls.

### Cross-cutting (as needed)
- CMake build alongside/replacing autotools to ease GUI + tooling.
- Documentation of the genome format and render parameters.

---

## Notes for contributors / future sessions
- Build on this box: `apt-get install libxml2-dev libjpeg-dev`, then
  `./configure && make` (touch generated autotools files first if it tries to
  regenerate). `flam3-render` looks for `flam3-palettes.xml` in
  `PACKAGE_DATA_DIR` (`/usr/local/share/flam3/`).
- Benchmark recipe used for the baseline:
  `nthreads=N qs=50 ss=2 ./flam3-render < test.flam3`.
