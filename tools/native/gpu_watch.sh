#!/bin/bash
# Samples the GPU every 5 s while the game runs: logs/NAME_gpu.txt gets "time util% other-apps".
# A bench whose file shows another compute process (e.g. a Blender render) is not trustworthy:
# the guest stalls on GPU fences. usage: gpu_watch.sh NAME (exits when no gtav process is left)
L="$HOME/gtav-linux/logs/$1_gpu.txt"; : >"$L"
# The bench may sit in memgate's slot/RAM queue for a long time before the game starts: wait for
# the process (up to 2 h) instead of exiting with an empty file.
w=0; until pgrep -x gtav >/dev/null; do sleep 5; w=$((w+5)); [ $w -ge 7200 ] && { echo "gpu watch: no game within 2 h"; exit 1; }; done
sleep 5
while pgrep -x gtav >/dev/null; do
  u=$(nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null)
  o=$(nvidia-smi --query-compute-apps=pid,process_name --format=csv,noheader 2>/dev/null | grep -v '/gtav' | awk -F', ' '{print $2}' | xargs -n1 basename 2>/dev/null | sort -u | tr '\n' ',')
  echo "$(date +%H:%M:%S) ${u}% ${o:-none}" >>"$L"
  sleep 5
done
n=$(grep -vc 'none$' "$L"); echo "gpu watch: $(wc -l <"$L") samples, $n with another GPU process"
