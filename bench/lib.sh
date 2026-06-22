#!/usr/bin/env bash
# Shared setup for flam3fox benchmark / regression scripts.
# Source this from other scripts: . "$(dirname "$0")/lib.sh"

set -euo pipefail

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$BENCH_DIR/.." && pwd)"

# Use the freshly built library and CLI from the repo, and the repo's palette
# file, so the harness is self-contained regardless of `make install`.
export LD_LIBRARY_PATH="$REPO_DIR/.libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export flam3_palettes="$REPO_DIR/flam3-palettes.xml"

RENDER="$REPO_DIR/flam3-render"
SCENE="${SCENE:-$BENCH_DIR/scene.flam3}"

# Fixed ISAAC seed -> reproducible chaos game for a given thread count.
export isaac_seed="${isaac_seed:-flam3fox}"

die() { echo "error: $*" >&2; exit 1; }

require_build() {
    [ -x "$RENDER" ] || die "flam3-render not built. Run: make (see docs/ROADMAP.md)"
    [ -f "$flam3_palettes" ] || die "palette file not found: $flam3_palettes"
    [ -f "$SCENE" ] || die "scene not found: $SCENE"
}

# render <nthreads> <out.ppm> [extra env assignments...]
# Renders $SCENE to a single PPM. Extra args are passed as VAR=VAL env.
render_ppm() {
    local nthreads="$1" out="$2"; shift 2
    env "$@" \
        nthreads="$nthreads" format=ppm out="$out" \
        "$RENDER" < "$SCENE" >/dev/null 2>&1
}

# Wall-clock seconds (float) for a command.
time_cmd() {
    local t0 t1
    t0=$(date +%s.%N)
    "$@"
    t1=$(date +%s.%N)
    awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.3f", b - a }'
}
