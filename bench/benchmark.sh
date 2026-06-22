#!/usr/bin/env bash
# Time the renderer across thread counts to track optimization progress.
#
# Usage:
#   bench/benchmark.sh                 # sweep 1..nproc threads, default size
#   THREADS="1 2 4 8" bench/benchmark.sh
#   SS=2 QS=2 bench/benchmark.sh       # scale size x2, quality x2 (heavier)
#   REPS=3 bench/benchmark.sh          # take best of N reps per thread count
#
# SS scales image size, QS scales sample quality (passed to flam3-render).

. "$(dirname "$0")/lib.sh"

SS="${SS:-1}"
QS="${QS:-1}"
REPS="${REPS:-1}"

if [ -n "${THREADS:-}" ]; then
    thread_list="$THREADS"
else
    n="$(nproc 2>/dev/null || echo 4)"
    thread_list=""
    t=1
    while [ "$t" -le "$n" ]; do
        thread_list="$thread_list $t"
        t=$((t * 2))
    done
    # ensure the exact core count is included
    case " $thread_list " in *" $n "*) ;; *) thread_list="$thread_list $n";; esac
fi

require_build

echo "scene=$(basename "$SCENE")  ss=$SS  qs=$QS  reps=$REPS  seed=$isaac_seed"
echo "threads | best(s) | speedup | efficiency"
echo "--------+---------+---------+-----------"

out="$(mktemp --suffix=.ppm)"
trap 'rm -f "$out"' EXIT

base=""
for nt in $thread_list; do
    best=""
    for _ in $(seq 1 "$REPS"); do
        s=$(time_cmd render_ppm "$nt" "$out" ss="$SS" qs="$QS")
        if [ -z "$best" ] || awk -v a="$s" -v b="$best" 'BEGIN{exit !(a<b)}'; then
            best="$s"
        fi
    done
    [ -z "$base" ] && base="$best"
    awk -v nt="$nt" -v b="$best" -v base="$base" 'BEGIN{
        sp = base / b;
        printf "%7d | %7.3f | %6.2fx | %8.1f%%\n", nt, b, sp, 100.0*sp/nt
    }'
done
