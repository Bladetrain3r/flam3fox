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

---

## Phase 1 — CPU optimization

### D9 — Per-thread bucket buffers instead of atomics/locks
**Decision:** Give each iteration thread its own private histogram buffer and
reduce them after join, rather than having all threads accumulate into one
shared buffer.
**Reasoning:** Profiling the dispatch revealed the real scaling bottleneck:
`flam3_render` routes ≥3 threads to an `_mt` path doing a `__sync` compare-and-swap
on *every* bucket write, and ≤2 threads to a path that serializes *all*
accumulation behind a mutex (`USE_LOCKS`). Both throttle the hot loop. Private
per-thread histograms let every write be a plain (or saturating) add with no
synchronization; a single cheap reduction pass merges them before density
estimation. Measured 4-thread efficiency rose from 40% → 81% (small scene) and
to 95% (larger scene), with 1-thread output bit-identical.
**Cost / tradeoff:** Bucket memory now scales with thread count
(`5·nthreads + 4` channels per cell vs `5 + 4`). Acceptable for typical renders
on Linux; `flam3_render_memory_required` was updated to report it. A future
guard could fall back to shared+atomic accumulation if a render's projected
memory is excessive.
**Implementation notes:** The atomic-add helpers (`*_atomic_add`) are now unused
but retained (harmless `static inline`) in case a shared fallback is added. The
`_mt` render variants still exist for the dispatch but their bump macro is now
identical to the single-threaded one. `bad-value` counts are accumulated
per-thread and summed after join to avoid a shared-counter race.
**Rejected:** *Make the ≥3-thread path non-atomic on the shared buffer* (removes
atomics but reintroduces lost-update races and false sharing — strictly worse
than private buffers, which are both faster and more correct).

### D10 — `-march=native -flto`, overridable via `OPT_FLAGS`
**Decision:** Add `-march=native -flto` to `AM_CFLAGS` through an `OPT_FLAGS`
variable that can be overridden or cleared on the make line.
**Reasoning:** ~13% single-thread speedup for free (FMA + wider vectors via
`-march=native`, cross-TU inlining via `-flto`) and the regression stays
bit-exact. `-march=native` targets the build host, which is correct for a
Linux-first optimization fork; making it an `OPT_FLAGS` variable keeps
distributors/CI able to retarget (`make OPT_FLAGS="-march=x86-64-v2"`) or
disable it (`make OPT_FLAGS=`).
**Build-system note:** Editing `AM_CFLAGS` would normally trigger an automake
regen, but the repo was generated with automake 1.15 (only 1.16 is installed),
which previously broke `make`. To avoid a noisy 1.15→1.16 mass-regeneration of
`configure`/`Makefile.in`, the change was applied surgically to both
`Makefile.am` (source of truth) and the committed `Makefile.in`, then `Makefile`
was regenerated with `./config.status` (no automake needed). `Makefile` itself is
git-ignored (regenerated per machine by `configure`).
**Rejected:** *Full `autoreconf -fi`* (clean in principle, but produces a large
generated-file diff and risks subtle 1.15→1.16 behavior changes — disproportionate
for a fork that plans to move to CMake in Phase 4); *hardcoding `-march=native`*
(not overridable for other build hosts/CI).

---

## Phase 2 — SIMD chaos game

### D11 — AVX2 foundation: masked multi-xform, opt-in, scalar fallback
**Decision:** Add an AVX2 path (`flam3_iterate_simd`) that runs 8 trajectories
per step. Each lane independently selects its xform (scalar ISAAC draw); every
xform is applied to all 8 lanes and the result blended in by a per-lane mask.
Gated behind `flam3_simd=1` with `flam3_genome_simd_ok()` fallback to scalar.
**Reasoning — why masked, not binning:** The clean alternatives for SIMD divergence
are (a) *lane-shared xform* — broken, because a contractive IFS driven by one
shared map sequence collapses all lanes to the same point; (b) *binning* points by
chosen xform into homogeneous SIMD groups — gives full width regardless of xform
count but needs gather/scatter and pool bookkeeping; (c) *masked multi-xform* —
apply every xform to every lane, keep the matching one. We chose (c): it has no
gather/scatter, keeps lanes independent and correct, and is simple enough to land
safely. Its cost is computing all xforms each step, so speedup scales ~`8/num_xforms`
(measured 1.55× at 4 xforms, ~1.9× at 2). Binning is the likely later upgrade.
**Why opt-in + fallback:** Keeps the proven scalar renderer the default (regression
stays byte-exact) while the SIMD path matures. `flam3_genome_simd_ok` only accepts
genomes whose every nonzero variation is vectorized (currently `linear`,
`spherical`) and that have no final xform / chaos; anything else runs scalar, so
correctness is never at risk.
**Why float, scalar RNG, no retry:** Lanes are `float` (8-wide, the throughput
play; flames are histograms and tolerate float — Fractorium uses it on GPU).
Output was validated statistically equivalent to the scalar double path
(mean abs diff ≈ 1.2/255, same order as multi-thread RNG variation). Xform
selection reuses scalar ISAAC draws (no new RNG to validate; the gather is scalar
anyway). The scalar path's bad-value *retry* (re-roll up to 5×) is simplified to a
single random reset per lane — a negligible statistical difference on an opt-in
path, avoiding lane-divergent control flow.
**Placement:** Implemented in `flam3.c` (guarded by `__AVX2__`, with a scalar
fallback definition otherwise) rather than a new source file, to avoid an automake
regen for `libflam3_la_SOURCES`. `-march=native` (D10) provides `__AVX2__`/FMA;
without it the code compiles to the scalar fallback.

### D12 — Broaden the SIMD variation set (arithmetic + sqrt batch)
**Decision:** Refactor the SIMD inner loop from hardcoded linear+spherical into a
per-active-variation `switch` and add the variations needing only arithmetic and
`sqrt`: `horseshoe`, `hyperbolic`, `bent`, `fisheye`, `eyefish`, `bubble`.
**Reasoning:** These broaden how many genomes qualify for the SIMD path with no new
machinery (AVX2 has `_mm256_sqrt_ps`; no transcendentals needed). The `switch`
structure also sets up cleanly for adding trig-based variations next. The loop now
skips density-0 xforms (never selected; their precalc state can be stale).
**Validation:** Each variation rendered through scalar vs SIMD on a populated scene
and checked statistically equivalent. All match. *Note on `bent`:* with deliberately
expansive test coefficients it diverged (43% of pixels), but with normal contractive
coefficients it matches (mean abs ≈ 0.16). The cause is the D11 bad-value tradeoff —
expansive maps generate many out-of-range points, and the scalar retry-up-to-5×
differs from the SIMD single-reset — not a math error (verified by hand). This is an
accepted limitation of the opt-in path, most visible on piecewise-linear maps.
