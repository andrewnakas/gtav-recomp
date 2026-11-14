#!/bin/sh
# lib_ab_linux.sh NAME DIR_A DIR_B [ROUNDS=3] [SECS=60] [game flags...]
# Two-binary A/B of the SDK runtime on Linux: alternates the librexruntime.so / librexgpu-xenos.so
# sets found in DIR_A and DIR_B (copied into build/gtav while the game is NOT running), runs
# bench.sh for each (same input script, same length), and prints one bench_stats line per run plus
# the per-side means. Interleaved A B, B A, A B ... so drift (thermal, other jobs) hits both sides.
# BENCH_INPUT=none for standing still. The original .so files are restored at the end.
N=$1; A=$2; B=$3; ROUNDS=${4:-3}; SECS=${5:-60}; [ $# -ge 5 ] && shift 5 || shift $#
ROOT="$HOME/gtav-linux"; G="$ROOT/build/gtav"; LIBS="librexruntime.so librexgpu-xenos.so"
if pgrep -x gtav >/dev/null; then echo "game running: not starting"; exit 1; fi
for d in "$A" "$B"; do for l in $LIBS; do [ -f "$d/$l" ] || { echo "missing $d/$l"; exit 1; }; done; done
SAVE=$(mktemp -d "$ROOT/logs/lib_ab_save.XXXX"); for l in $LIBS; do cp -p "$G/$l" "$SAVE/"; done
i=0
while [ $i -lt "$ROUNDS" ]; do
  # A B, then B A: a drift that runs one way (load from other sessions, thermal) no longer favours
  # the side that always runs first.
  order="A B"; [ $((i % 2)) = 1 ] && order="B A"
  for side in $order; do
    [ $side = A ] && d=$A || d=$B
    run="${N}_${side}${i}"
    pgrep -x gtav >/dev/null && { echo "game still running before $run"; break 2; }
    for l in $LIBS; do cp -p "$d/$l" "$G/$l"; done
    "$ROOT/tools/native/bench.sh" "$run" "$SECS" "$@" >"$ROOT/logs/$run.bench" 2>&1
    python3 "$ROOT/tools/native/bench_stats.py" "$ROOT/logs/$run.log" "$ROOT/logs/${run}_cpu0.txt" "$ROOT/logs/${run}_cpu1.txt" "$SECS" --csv | sed "s/^/$side,/"
    sleep 3
  done
  i=$((i + 1))
done | tee "$ROOT/logs/${N}_ab.csv"
for l in $LIBS; do cp -p "$SAVE/$l" "$G/$l"; done; rm -rf "$SAVE"
echo "== per side (fps mean, 1% low, record ms, GPU ms, hitches)"
awk -F, '{n[$1]++; f[$1]+=$3; l[$1]+=$5; r[$1]+=$9; g[$1]+=$10; h[$1]+=$13; if($3>mx[$1])mx[$1]=$3; if(!($1 in mn)||$3<mn[$1])mn[$1]=$3}
  END{for(s in n) printf "%s: %.1f fps (min %.1f max %.1f), 1%% low %.1f, record %.2f, GPU %.2f, hitches %.1f (%d runs)\n", s, f[s]/n[s], mn[s], mx[s], l[s]/n[s], r[s]/n[s], g[s]/n[s], h[s]/n[s], n[s]}' "$ROOT/logs/${N}_ab.csv"
