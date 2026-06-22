# flam3fox — Decision Review

A companion to `ROADMAP.md`. The roadmap says *what* and *when*; this log says
*why*. Newest entries at the bottom of each section. Each entry records the
decision, the reasoning, and the alternatives that were weighed and rejected.

---

## Project direction

### D1 — Optimize CPU first, then GPU
**Decision:** Bank the cheap CPU wins (build flags, threading) and SIMD before
building a GPU backend.
**Reasoning:** The cheap CPU wins are low-risk and pay off immediately, and they
sharpen the CPU reference path that the GPU port will be validated against. A GPU
port is the highest ceiling but also the most work and adds a hardware/driver
dependency before any speedup lands.
**Rejected:** *GPU-first* (highest ceiling but slowest to first win, and risky to
validate without a fast, trusted CPU baseline); *CPU-only forever* (caps the
ceiling at single-digit× — contrary to the "see how far we can go" goal).

### D2 — UI on Dear ImGui
**Decision:** Build the Apophysis-style editor with Dear ImGui (C++), linking
directly against `libflam3`.
**Reasoning:** Fastest path to a working interactive editor with minimal
dependencies; matches the "relatively simple UI" brief. The library already
exposes everything a UI needs (parse, render-with-pause/abort callback, mutate,
cross, interpolate, xform manipulation).
**Rejected:** *Qt* (more polished/Apophysis-like, but heavier setup and a large
dependency — overkill for "simple"); *Web + WASM* (most shareable, but adds a
WASM toolchain and perf constraints, premature for now).

### D3 — Linux first
**Decision:** Develop and optimize on Linux; keep portability in mind but do not
gate work on Windows/macOS.
**Reasoning:** Fastest iteration. Cross-platform CI/build complexity can be added
later once the architecture settles.
**Rejected:** *Cross-platform from day one* (more build/CI overhead early; would
push toward CMake + Qt before that's warranted).

---

## Phase 0 — Measurement harness

### D4 — Tolerance-based image comparison, not bit-exact
**Decision:** The regression compares against a golden image within tolerances
(max/mean/RMS abs diff, % differing pixels) rather than requiring an exact match.
**Reasoning:** The chaos game is stochastic and floating-point accumulation order
is not stable across legitimate optimizations (per-thread buckets, `-march=native`,
LTO, SIMD). A bit-exact check would flag valid speedups as failures. A converged,
high-quality reference + tolerances catches real logic regressions (which move
the metrics far out of range) while tolerating numerical reordering.
**Rejected:** *Exact hash/`cmp`* (brittle — breaks on every valid optimization);
*perceptual metric like SSIM* (needs numpy/external deps; overkill here).

### D5 — Determinism via `isaac_seed` + pinned thread count
**Decision:** Fix `isaac_seed` and pin `REG_THREADS=1` for the regression.
**Reasoning:** Investigation showed the chaos game RNG is ISAAC seeded by the
`isaac_seed` string; the `seed` env var only seeds libc `random()` (genome ops)
and does **not** make a render reproducible. Output is reproducible only for a
fixed `isaac_seed` **and** a fixed thread count (threads get independent RNG
streams and accumulate into a shared buffer lock-free). Pinning 1 thread also
means the upcoming per-thread-bucket change is a no-op there, so the golden stays
valid across that optimization — only compiler FP reordering can perturb it, well
inside tolerances.
**Rejected:** *Multi-thread golden* (cross-thread output differs run-to-run in
structure; would force loose tolerances that mask real bugs).

### D6 — PPM format + pure-stdlib comparator
**Decision:** Render the reference to binary PPM (P6) and compare with a
dependency-free Python script.
**Reasoning:** The box has no PIL/numpy/ImageMagick. PPM is trivial to parse with
the standard library; PNG would require decoding filters. Keeps the harness
runnable anywhere with just python3.
**Rejected:** *PNG golden* (smaller on disk, but needs a real decoder);
*ImageMagick `compare`* (not installed, extra dependency).

### D7 — Compact committed golden (256², quality 1000)
**Decision:** The reference scene is a single flame at 256², quality 1000; the
golden PPM (~192 KB raw, well-compressed in the packfile) is committed.
**Reasoning:** Small enough to commit comfortably, high enough quality to be
converged (so order-independent renders agree), and fast enough (~3 s at 1
thread) for a quick regression run. `benchmark.sh` scales size/quality up via
`SS`/`QS` for heavier timing without bloating the committed reference.
**Rejected:** *Large multi-flame scene* (slow regression, bulky golden);
*external palette dependency* — the scene references a palette index but the
scripts point `flam3_palettes` at the repo's `flam3-palettes.xml`, so the harness
stays self-contained without `make install`.

### D8 — Build notes (dev environment)
**Decision:** Document that the build needs `libxml2-dev` + `libjpeg-dev`, and
that stale autotools timestamps require touching the generated files before
`make` (rather than regenerating with a possibly-mismatched automake).
**Reasoning:** Reproducibility for future sessions on a fresh, ephemeral
container. Regenerating autotools risked version mismatches (`aclocal-1.15` not
present); touching the committed generated files is the lower-risk path.
