#!/bin/bash
# Waits until the machine can give a trustworthy game benchmark: 1-minute load below MAXLOAD
# (default 5) twice in a row AND nothing else on the GPU (no other compute process in nvidia-smi,
# GPU utilization below 20%). A Blender render on the same GPU made the game crawl at 7-second
# frames on 11-13 while the disk and CPU looked idle; another session's filesystem scan of the USB
# game disk made every loader read take 60-120 ms (10-second loading frames). The disk (DISK,
# default sdb) must also be under 15% busy. usage: quiet_gate.sh [MAXLOAD] [MAXSECS] [DISK]
MAXLOAD=${1:-5}; MAXSECS=${2:-21600}; DISK=${3:-sdb}
S0=$(date +%s); ok=0
while :; do
  l=$(cut -d' ' -f1 /proc/loadavg | cut -d. -f1)
  gpu=$(nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null | head -1)
  apps=$(nvidia-smi --query-compute-apps=pid --format=csv,noheader 2>/dev/null | grep -c .)
  a=$(awk -v d="$DISK" '$3==d{print $13}' /proc/diskstats); sleep 10; b=$(awk -v d="$DISK" '$3==d{print $13}' /proc/diskstats)
  disk=$(( (${b:-0} - ${a:-0}) / 100 ))  # percent of the 10 s the disk had a request in flight
  if [ "$l" -lt "$MAXLOAD" ] && [ "${gpu:-0}" -lt 20 ] && [ "$apps" = 0 ] && [ "$disk" -lt 15 ]; then ok=$((ok+1)); else ok=0; fi
  [ $ok -ge 2 ] && { echo "quiet at $(date +%H:%M): load $l, gpu ${gpu}%, $DISK ${disk}% busy"; exit 0; }
  if [ $(( $(date +%s) - S0 )) -gt "$MAXSECS" ]; then echo "gave up after $MAXSECS s (load $l, gpu ${gpu}%, gpu apps $apps, $DISK ${disk}% busy)"; exit 2; fi
  sleep 50
done
