# bench/ — measurement harness (Phase 0)

A repeatable benchmark and a correctness regression check, so optimization work
can be proven both *faster* and *still correct*.

## Prerequisites

Build the library and CLI first (see `docs/ROADMAP.md`):

```sh
apt-get install libxml2-dev libjpeg-dev   # one-time, if missing
./configure && make
```

The scripts use the freshly built `flam3-render` from `.libs/` and the repo's
`flam3-palettes.xml`, so nothing needs to be installed system-wide.

## Files

| File | Purpose |
| --- | --- |
| `scene.flam3` | Fixed 256×256, quality-1000 single-flame scene. Self-contained reference workload. |
| `golden.ppm` | Committed reference image (rendered at `REG_THREADS=1`, `isaac_seed=flam3fox`). |
| `compare.py` | Pure-stdlib PPM comparator (max/mean/RMS diff, % differing pixels). Tolerance-based. |
| `regression.sh` | Render the scene and diff against `golden.ppm`. `--update` regenerates the golden. |
| `benchmark.sh` | Time renders across thread counts; reports speedup + parallel efficiency. |
| `lib.sh` | Shared setup (paths, seed, palette, render/timing helpers). |

## Determinism model

The chaos game uses the ISAAC RNG seeded by `isaac_seed` (a string), **not** the
`seed` env var (which only seeds libc `random()` for genome operations). Output
is reproducible for a **fixed `isaac_seed` + fixed thread count**. Across thread
counts it differs, because each thread gets its own RNG stream and accumulation
into the shared bucket buffer is lock-free (benign races).

The regression therefore pins `REG_THREADS=1`. At one thread the upcoming
per-thread-bucket optimization is a no-op, so only compiler-level FP reordering
(`-march=native`, LTO, SIMD) can perturb output — and only by a few levels,
well inside the tolerances. A genuine logic regression moves the metrics far
outside them.

## Usage

```sh
# Correctness: render and compare against the golden image
bench/regression.sh

# Regenerate the golden (after an intentional, reviewed output change)
bench/regression.sh --update

# Performance: sweep 1..nproc threads at the default size
bench/benchmark.sh

# Heavier workload (2x size, 2x quality), best of 3 reps
SS=2 QS=2 REPS=3 bench/benchmark.sh

# Specific thread counts
THREADS="1 2 4" bench/benchmark.sh
```

Tolerances can be overridden via env, e.g. `MEAN_ABS=2 bench/regression.sh`.
