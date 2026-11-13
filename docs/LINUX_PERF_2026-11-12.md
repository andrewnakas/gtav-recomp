# Linux perf pass, 2026-11-12 (branch `perf-linux`, off `issue-fixes` e0999ca)

Machine: i5-13420H (4P+4E), RTX 4050 Laptop, 32 GB, game data on the TOSHIBA USB disk.
Goal: 100+ fps uncapped (10-29 target), fewer hitches. Scope: the whole Linux build.

## Status (11-13 12:30): boot bug found and fixed, in-world numbers still pending
The loading-screen crawl that blocked every bench (0.2-1 fps, 1-5 s guest frames, "long wait
(>15s)" loader threads, CPU and disk mostly idle) was a runtime bug, not only the busy machine:
`HostPathDevice::ResolvePath` listed the whole host directory (opendir + readdir + stat per
child) on every probe for a file that does not exist, under the VFS global lock. GTA V probes
`game:\xbox360` ~10x/s while loading; on the exFAT USB disk each listing is 20-200 ms inside
`exfat_iterate` (gdb + `/proc/<tid>/wchan` sampling: Main XThread 80% there, every other guest
thread queued on the global mutex). Fixed in 90484b8 (listing cached per directory while its
write time is unchanged). First hidden boot with the fix reached the open world in 154 s on a
loaded machine (load 29); 11-08 logs on a quiet machine show 12 s, so the rest is load and the
disk. Other sessions' GPU renders (Blender, all night) and `bfs` scans of the game disk made it
worse but were not the cause.

Also seen: the hidden SDK window is still painted once per guest swap (presenter →
`WindowSDL::RequestPaint`, Vulkan present through the NVIDIA driver, ~500/s in the 400-fps logo
phase, 18% of a core on the main host thread); harmless in the world at 30-60 fps, so left alone.

**Everything below is built (`build/gtav`: pre-change game binary from e0999ca, runtime libs
`logs/libs_dircache` = Sleep fix + lock fast path + listing cache) but the perf commits are still
unmeasured: the machine has not been quiet (load 9-30, other sessions' rustc/rm/Blender).**

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
| e0855db | fix: `push_set` reset after the non-draw push-constant writes (self-review of 8a47b12) | — |
| c857388, 0bbd86d, cd11af6 | `quiet_gate.sh` (load, GPU, disk), `gpu_watch.sh` | — |
| 90484b8 | SDK: host directory listing cached for missing-file probes (boot crawl); SDK build options `REX_LARGE_CODE_MODEL`/`REX_SYMBOLIC_RUNTIME` (defaults unchanged) | boot-to-world time: `grep -m1 -E 'frame [0-9]+: [0-9]{3,} draws'` vs log start; 11-08 = 12 s |

Runtime library sets saved for A/B: `logs/libs_base` (stock), `logs/libs_sleepfix` (Sleep fix
only), `logs/libs_fix2` (Sleep fix + lock fast path), `logs/libs_dircache` (fix2 + listing cache, currently
in `build/gtav`; the boot fix is needed to reach the world at all, so A/B the Sleep fix as
`libs_base`+listing cache vs `libs_dircache` if a separate build is wanted).

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
