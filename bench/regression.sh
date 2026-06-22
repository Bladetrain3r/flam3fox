#!/usr/bin/env bash
# Render the benchmark scene with a pinned seed + thread count and compare
# against the committed golden image within tolerance. This guards against
# correctness regressions while allowing performance work to change
# floating-point accumulation order slightly.
#
# Usage:
#   bench/regression.sh            # check current render against golden
#   bench/regression.sh --update   # (re)generate the golden image
#
# Tolerances can be overridden via env: MAX_ABS, MEAN_ABS, MAX_PCT, PCT_THRESH.

. "$(dirname "$0")/lib.sh"

# Pin thread count: output is reproducible for a fixed seed + thread count.
REG_THREADS="${REG_THREADS:-1}"
GOLDEN="$BENCH_DIR/golden.ppm"

# Defaults leave headroom for floating-point reordering from legitimate
# optimizations (-march=native, LTO, SIMD). At REG_THREADS=1 the current code
# reproduces bit-exact, so a real regression stands out clearly.
MAX_ABS="${MAX_ABS:-24}"
MEAN_ABS="${MEAN_ABS:-1.5}"
MAX_PCT="${MAX_PCT:-3.0}"
PCT_THRESH="${PCT_THRESH:-8}"

require_build

if [ "${1:-}" = "--update" ]; then
    echo "Generating golden image -> $GOLDEN (threads=$REG_THREADS, seed=$isaac_seed)"
    render_ppm "$REG_THREADS" "$GOLDEN"
    echo "done. Commit bench/golden.ppm to lock the reference."
    exit 0
fi

[ -f "$GOLDEN" ] || die "no golden image. Run: bench/regression.sh --update"

tmp="$(mktemp --suffix=.ppm)"
trap 'rm -f "$tmp"' EXIT

render_ppm "$REG_THREADS" "$tmp"

python3 "$BENCH_DIR/compare.py" "$GOLDEN" "$tmp" \
    --max-abs "$MAX_ABS" --mean-abs "$MEAN_ABS" \
    --max-pct "$MAX_PCT" --pct-thresh "$PCT_THRESH"
