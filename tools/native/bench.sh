#!/bin/sh
# Unattended performance run: hidden native window, muted, uncapped. Boots into the world, plays
# tools/native/input_run_around.txt, then measures SECS seconds: per-thread CPU use (which thread
# is the bottleneck), the [native] timing lines, and (BENCH_PERF=1, needs
# kernel.perf_event_paranoid <= 1) a perf profile in logs/NAME.perf.
# usage: tools/native/bench.sh NAME [SECS] [game flags...]
NAME=$1; SECS=${2:-30}; [ $# -ge 2 ] && shift 2 || shift $#
ROOT="$HOME/gtav-linux"
L="$ROOT/logs/$NAME.log"
# Command-line settings only: gtav_settings.ini is loaded after the command line and would put
# vsync (refresh cap) and the menu's other choices back.
export GTAV_IGNORE_SETTINGS=1
if pgrep -x gtav >/dev/null; then echo "game already running: not starting"; exit 1; fi
GO="$ROOT/logs/${NAME}_input.txt"
rm -f "$L" "$GO"
"$ROOT/play.sh" "$NAME" --gtav_native_fps --gtav_native_stats=true --gtav_native_hidden=true --audio_mute=true \
  --input_script="$GO" "$@" >"$ROOT/logs/$NAME.stdout" 2>&1 &
PID=$!
S=$(date +%s)
until grep -qE '\[native\] frame [0-9]+: [0-9]{3,} draws' "$L" 2>/dev/null; do
  sleep 5
  if ! kill -0 $PID 2>/dev/null; then echo "game exited before reaching the world"; exit 3; fi
  if [ $(( $(date +%s) - S )) -gt "${BENCH_BOOT_SECS:-420}" ]; then echo "no in-game frame after ${BENCH_BOOT_SECS:-420} s (boot hang?)"; kill -9 $PID; exit 2; fi
done
echo "in game after $(( $(date +%s) - S )) s"
sleep 10
# BENCH_INPUT=none: stand still at the spawn point (steadier scenes for A/B runs).
[ "${BENCH_INPUT:-run}" = none ] || cp "$ROOT/tools/native/input_run_around.txt" "$GO"
sleep 5
# Per-thread CPU (utime + stime ticks) over the window, and the CPU the thread last ran on
# (P-cores 0-7 in hyperthread pairs, E-cores 8-11 on this laptop).
snap() { for t in /proc/$PID/task/*; do
  # comm may hold spaces and parentheses ("Main XThread (F..."): count fields after the last ')'.
  printf '%s %s ' "${t##*/}" "$(tr ' ' '_' <"$t/comm")"; sed 's/.*) //' "$t/stat" | awk '{print $12 + $13, $37}'; done; }
snap >"$ROOT/logs/${NAME}_cpu0.txt"
LINE0=$(wc -l <"$L")
if [ "${BENCH_PERF:-0}" = 1 ]; then
  perf record -F 499 ${BENCH_PERF_ARGS:--g} -p $PID -o "$ROOT/logs/$NAME.perf" -- sleep "$SECS" >/dev/null 2>&1
else
  sleep "$SECS"
fi
snap >"$ROOT/logs/${NAME}_cpu1.txt"
kill -9 $PID 2>/dev/null
sleep 2
HZ=$(getconf CLK_TCK)
echo "== CPU per thread over $SECS s (% of one core, top 12)"
awk -v hz=$HZ -v s=$SECS 'NR == FNR { a[$1] = $3; next } ($1 in a) { printf "%6.1f%%  %s (%s) cpu%s\n", ($3 - a[$1]) * 100 / hz / s, $2, $1, $4 }' \
  "$ROOT/logs/${NAME}_cpu0.txt" "$ROOT/logs/${NAME}_cpu1.txt" | sort -rn | head -12
echo "== timing lines in the window"
tail -n +"$LINE0" "$L" | grep -E '\[native\] (fps=|record ms|presented|  avg acquire|  textures per|command processor waited|fence copies)' | tail -14
echo "== summary"
python3 "$ROOT/tools/native/bench_stats.py" "$L" "$ROOT/logs/${NAME}_cpu0.txt" "$ROOT/logs/${NAME}_cpu1.txt" "$SECS"
