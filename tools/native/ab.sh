#!/bin/sh
# A/B a runtime setting in one unattended run (scenes differ between runs, so separate runs
# compare badly): standing still at the spawn point, the setting flips between A and B every
# PERIOD seconds (logs/menu_set test hook) and the per-second fps lines are averaged per value.
# usage: tools/native/ab.sh NAME SETTING A B [ROUNDS] [PERIOD] [game flags...]
NAME=$1; SETTING=$2; A=$3; B=$4; ROUNDS=${5:-4}; PERIOD=${6:-10}; shift 6 2>/dev/null
ROOT="$HOME/gtav-linux"
L="$ROOT/logs/$NAME.log"
# Command-line settings only, and the menu_set flips must not be saved into gtav_settings.ini.
export GTAV_IGNORE_SETTINGS=1
if pgrep -x gtav >/dev/null; then echo "game already running: not starting"; exit 1; fi
rm -f "$L" "$ROOT/build/gtav/logs/menu_set"
"$ROOT/play.sh" "$NAME" --gtav_native_fps --gtav_native_stats=true --gtav_native_hidden=true --audio_mute=true "$@" \
  >"$ROOT/logs/$NAME.stdout" 2>&1 &
PID=$!
S=$(date +%s)
until grep -qE '\[native\] frame [0-9]+: [0-9]{3,} draws' "$L" 2>/dev/null; do
  sleep 5
  if ! kill -0 $PID 2>/dev/null; then echo "game exited before reaching the world"; exit 3; fi
  if [ $(( $(date +%s) - S )) -gt 420 ]; then echo "no in-game frame after 420 s"; kill -9 $PID; exit 2; fi
done
sleep 15
MARKS="$ROOT/logs/${NAME}_marks.txt"; : >"$MARKS"
i=0
while [ $i -lt "$ROUNDS" ]; do
  for v in "$A" "$B"; do
    echo "$SETTING=$v" >"$ROOT/build/gtav/logs/menu_set"
    echo "$(date '+%H:%M:%S') $v" >>"$MARKS"
    sleep "$PERIOD"
  done
  i=$((i + 1))
done
echo "$(date '+%H:%M:%S') end" >>"$MARKS"
kill -9 $PID 2>/dev/null
sleep 2
# Average fps and draws per value, skipping 2 s after each flip.
grep -E '\[native\] fps=' "$L" | sed -E 's/^\[[0-9-]+ ([0-9:]+)\.[0-9]+\].*fps=([0-9.]+).*drawidx=([0-9]+).*/\1 \2 \3/' |
  awk -v marks="$MARKS" '
    function secs(t,  a) { split(t, a, ":"); return a[1] * 3600 + a[2] * 60 + a[3] }
    BEGIN { n = 0; while ((getline line < marks) > 0) { split(line, f, " "); mt[n] = secs(f[1]); mv[n] = f[2]; n++ } }
    { t = secs($1); for (k = n - 2; k >= 0; k--) if (t >= mt[k] + 2 && t < mt[k + 1]) { s[mv[k]] += $2; d[mv[k]] += $3; c[mv[k]]++; break } }
    END { for (v in c) printf "%s=%s: %.1f fps (draws %.0f, %d samples)\n", "'"$SETTING"'", v, s[v] / c[v], d[v] / c[v], c[v] }'
