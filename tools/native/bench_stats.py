#!/usr/bin/env python3
"""bench_stats.py LOG [CPU0 CPU1 SECS] [--csv] [--window N]

One-line summary of a bench.sh log over the measured window (the fps lines after the input script
started, or the last N fps lines with --window): mean fps, 1% / 5% lows (from the per-second p99/p95
intervals), worst frame, draws, render-thread record ms, GPU ms, hitches >= 25 ms, and per-thread CPU
from bench.sh's cpu0/cpu1 snapshots. --csv prints a comma-separated line for A/B tables.
"""
import re, sys, statistics

args = [a for a in sys.argv[1:] if not a.startswith('--')]
csv = '--csv' in sys.argv
window = 0
for i, a in enumerate(sys.argv):
    if a == '--window': window = int(sys.argv[i + 1]); args = [x for x in args if x != sys.argv[i + 1]]
log = args[0]
cpu0, cpu1, secs = (args[1], args[2], float(args[3])) if len(args) >= 4 else (None, None, 0)

fps_re = re.compile(r'\[native\] fps=([\d.]+) avg=([\d.]+)ms max=([\d.]+)ms(?: p99=([\d.]+)ms p95=([\d.]+)ms)? .*?drawidx=(\d+)')
rec_re = re.compile(r'record ms per frame: setup ([\d.]+) consts ([\d.]+) textures ([\d.]+) streams ([\d.]+) pipeline ([\d.]+) bind\+draw ([\d.]+)')
pres_re = re.compile(r'presented \d+ frames .*record avg ([\d.]+) max ([\d.]+) ms')
gpu_re = re.compile(r'per frame (\d+) render passes .*GPU frame ([\d.]+) ms, idle before ([\d.]+) ms')
hitch_re = re.compile(r'hitch ([\d.]+) ms at guest frame \d+: record ([\d.]+) ms, wait ([\d.]+) ms, (\d+) new pipelines, (\d+) textures, (\d+) draws')
start_re = re.compile(r'input script|\[input\].*(loaded|reload)')

lines = open(log, errors='replace').read().splitlines()
# Window: from the first fps line after the input script (re)load, if any; else all.
start = 0
for i, l in enumerate(lines):
    if 'input_script' in l and ('reload' in l or 'loaded' in l): start = i
fps, p99, p95, mx, draws, rec, pres, gpu, idle, passes, hitches = [], [], [], [], [], [], [], [], [], [], []
for l in lines[start:]:
    m = fps_re.search(l)
    if m:
        fps.append(float(m.group(1))); mx.append(float(m.group(3))); draws.append(int(m.group(6)))
        if m.group(4): p99.append(float(m.group(4))); p95.append(float(m.group(5)))
        continue
    m = rec_re.search(l)
    if m: rec.append([float(x) for x in m.groups()]); continue
    m = pres_re.search(l)
    if m: pres.append(float(m.group(1))); continue
    m = gpu_re.search(l)
    if m: passes.append(int(m.group(1))); gpu.append(float(m.group(2))); idle.append(float(m.group(3))); continue
    m = hitch_re.search(l)
    if m and int(m.group(6)) > 100: hitches.append(float(m.group(1)))
if window and len(fps) > window:
    fps, mx, draws = fps[-window:], mx[-window:], draws[-window:]
    p99, p95 = p99[-window:], p95[-window:]
if not fps:
    print(f"{log}: no fps lines (need --gtav_native_fps=true)"); sys.exit(1)

def mean(v): return statistics.fmean(v) if v else 0.0
def sd(v): return statistics.pstdev(v) if len(v) > 1 else 0.0
low1 = 1000.0 / mean(p99) if p99 and mean(p99) > 0 else 0
low5 = 1000.0 / mean(p95) if p95 and mean(p95) > 0 else 0
r = [mean([x[i] for x in rec]) for i in range(6)] if rec else [0] * 6
threads = []
if cpu0 and cpu1 and secs > 0:
    hz = 100
    a = {l.split()[0]: l.split() for l in open(cpu0)}
    for l in open(cpu1):
        f = l.split()
        if f[0] in a and len(f) >= 3:
            pct = (int(f[2]) - int(a[f[0]][2])) * 100.0 / hz / secs
            threads.append((pct, f[1], f[3] if len(f) > 3 else '?'))
    threads.sort(reverse=True)
hot = ' '.join(f"{n}={p:.0f}%@{c}" for p, n, c in threads[:5])
name = log.rsplit('/', 1)[-1]
if csv:
    print(f"{name},{mean(fps):.1f},{sd(fps):.1f},{low1:.1f},{low5:.1f},{max(mx):.0f},{mean(draws):.0f},"
          f"{mean(pres):.2f},{mean(gpu):.2f},{mean(idle):.2f},{mean(passes):.0f},{len(hitches)},{len(fps)}," + ','.join(f"{x:.2f}" for x in r)
          + ',' + hot)
else:
    print(f"{name}: {mean(fps):.1f} fps (sd {sd(fps):.1f}, {len(fps)} s), 1% low {low1:.1f}, 5% low {low5:.1f}, worst {max(mx):.0f} ms, "
          f"{mean(draws):.0f} draws")
    print(f"  record {mean(pres):.2f} ms (setup {r[0]:.2f} consts {r[1]:.2f} textures {r[2]:.2f} streams {r[3]:.2f} pipeline {r[4]:.2f} bind+draw {r[5]:.2f}); "
          f"GPU {mean(gpu):.2f} ms, idle before {mean(idle):.2f} ms, {mean(passes):.0f} passes; hitches >=25 ms: {len(hitches)}"
          + (f" (mean {mean(hitches):.0f} ms)" if hitches else ''))
    if threads: print("  threads: " + ' '.join(f"{n} {p:.0f}% cpu{c}" for p, n, c in threads[:8]))
