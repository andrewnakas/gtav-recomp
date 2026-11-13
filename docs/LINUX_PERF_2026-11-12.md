# Linux perf pass, 2026-11-12 (branch `perf-linux`, off `issue-fixes` e0999ca)

Machine: i5-13420H (4P+4E), RTX 4050 Laptop, 32 GB, game data on the TOSHIBA USB disk.
Goal: 100+ fps uncapped (10-29 target), fewer hitches. Scope: the whole Linux build.

## Status: code in, numbers not yet
No bench reached the open world today. Other Claude sessions ran four `bfs` scans of the
whole filesystem (the exFAT game disk included) for hours, the machine sat at load 25-35 with
4 GB of swap full, and the last A/B run was killed by Claude Code's low-memory reaper. Boots
that take 4 s to frame 150 on a quiet machine took 85-330 s and then crawled at 10-30 s per
frame in the loading screens ("long wait (>15s)" loader threads). Page-cache warm-ups of the
game files (`cat` of disc2_files + update, 504 s; title storage, 429 s) were evicted again
within minutes.

**Every change below is built into `build/gtav` (game binary 18:47, runtime libs `fix2`) but
unmeasured. Run the queue in "Measurement queue" on a quiet machine before trusting any of it.**

## Findings (verified in code)
1. **Benches were refresh-capped.** `gtav_settings.ini` (`gtav_native_vsync=true`) loads after
   the command line (`renderer.cpp` LoadSettings), so `--gtav_native_vsync=false` was overridden
   and `ab.sh` flips were saved into the ini. `bench.sh`/`ab.sh` now export `GTAV_IGNORE_SETTINGS=1`
   and the log prints the effective settings. Earlier Linux fps numbers near 144 were the cap.
2. **Guest `Sleep(0)` was a 100 µs nanosleep.** `rexglue-sdk/src/system/xthread.cpp` compared the
   guest priority increment (0) with `ThreadPriority::kBelowNormal`, which is 8 on POSIX. GTA's
   update thread calls SleepEx ~1,500 times a frame from its backoff spin (Windows wait profile),
   so this was up to ~150 ms of forced sleep per second on the critical thread. Fixed (yield).
3. **`GetNativeObject` locked the global recursive mutex twice per call.** Now lock-free once the
   handle is stashed (acquire/release fences; `StashHandle` writes the handle before the signature).
4. **No thread placement on Linux.** New `--gtav_linux_pin` (default off): hot threads one P-core
   each, background Vulkan/shader work on the E-cores. Windows lost fps when everything was pinned,
   so this stays off until the A/B says otherwise.
5. Renderer per-draw waste (see commits 8a47b12, 29cb2e4): shared constants zeroed+copied+pushed
   every draw; 32-slot fetch loop with per-slot memo wiped per resolve; 60-frame walk of all
   textures and buffers; `vkGetPipelineCacheData` on the render thread.
6. The frame is game-thread bound (Main XThread + D3D thread ~85-90% busy; render thread idles
   2-4 ms, GPU idles 6-9 ms per frame at ~3,500 draws). Render-thread savings only pay in heavy
   scenes and when the command processor waits on the renderer (rare in logs: 0-2 of 600 swaps).

## Commits on `perf-linux`
| Commit | What | How to measure |
|---|---|---|
| 2412e4b | tooling: ini ignored, `bench_stats.py` (mean, 1%/5% lows, record breakdown, per-thread CPU), `lib_ab_linux.sh`, `bin_ab_linux.sh` | — |
| 7cd394a | SDK: Sleep(0) yield, gated SCHED_FIFO, lock-free GetNativeObject | `lib_ab_linux.sh sleep logs/libs_base logs/libs_fix2 3 60`; `--gtav_wait_profile=true` SleepEx share |
| 2913c9c | `--gtav_linux_pin` thread placement (off) | `BENCH_BOOT_SECS=900 tools/native/ab.sh pin0 gtav_linux_pin false true 4 15` |
| 29cb2e4 | cache byte totals, desktop buffer budget 512 MB, pipeline-cache get off the render thread | hitches ≥25 ms in `bench_stats.py` over a 5-min run-around |
| 8a47b12 | shared-constant reuse, push-constant skip, fetch-block memo | `bin_ab_linux.sh` old vs new binary; textures/consts ms vs `logs/walk2.log` (2.2 / 0.9 ms); screenshots for texture mix-ups |
| a99a0cd | `BENCH_BOOT_SECS` | — |

Runtime library sets saved for A/B: `logs/libs_base` (stock), `logs/libs_sleepfix` (Sleep fix
only), `logs/libs_fix2` (Sleep fix + lock fast path, currently in `build/gtav`).

## Measurement queue (quiet machine: no `bfs`, load < 4, swap free)
1. `BENCH_BOOT_SECS=900 tools/native/ab.sh pin0 gtav_linux_pin false true 4 15` (pinning).
2. `tools/native/bench.sh still_new 60` with `BENCH_INPUT=none`, 3 repeats; `bench.sh run_new 90` 3 repeats.
3. `tools/native/lib_ab_linux.sh sleep logs/libs_base logs/libs_fix2 3 60` (runtime fixes).
4. `tools/native/bench.sh wait_new 60 --gtav_wait_profile=true` (SleepEx share should drop from ~2.4%).
5. `tools/native/bin_ab_linux.sh draws build/gtav/gtav <binary built from 2913c9c> 3 60` (renderer per-draw).
6. `BENCH_PERF=1 tools/native/bench.sh perf_new 60` then the remaining plan items
   (`gpu_idle_spins` 500→50, runtime build flags, object table shared_mutex, snapshot-stream reuse,
   record_thread/convert_threads/slot_targets, RPF prefetch/fadvise, audio health).
Acceptance: interleaved runs, mean delta > spread of repeats, 1% low not worse; visual check
(screenshots, flicker_catch, phone squares) after every kept renderer change; owner play-test.

## Not done (deferred)
Command-processor atomic wait instead of 50 µs polls and publishing `recorded_through_` right
after `Execute()`: logs show the CP almost never waits, so these are low value until measured.
