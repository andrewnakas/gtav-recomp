# Handoff: Mac and iPhone for 0.5.0 (branch `max-perf`)

Written 2026-11-23 on Linux, for the Claude session on the owner's Mac. **Read all of it first.**
The owner's words: "give handoff for my gtav mac build to do the mac and ios release for .5 so
also linux and windows can do the ios release too".

So the Mac has two jobs:

1. **Build and test the Mac app and the iPhone shell from `max-perf`**, fix what breaks, and make
   the Mac package of 0.5.0.
2. **Upload the iPhone shell (`ios-shell-v0.5.0.tar`)** as soon as it passes on the iPhone. The
   Linux and Windows packages carry that same `ios/` folder (only a Mac can build it), so they
   cannot be finished without it.

## Rules (owner's, unchanged)

- Push fixes to `max-perf` only. **Never push to `main`, never tag, never publish a release**
  until the owner says the Mac build passed and says to ship it.
- A **draft** release and its helper uploads are fine (they are how the machines exchange files).
- Hidden or automated game runs are muted (`--audio_mute=true`); warn before stopping a game the
  owner is playing.
- Commits end with the `Co-Authored-By` line of the model that made them.
- Nothing made from the game goes into a commit, a package or an upload (taint scan before any
  package leaves the Mac).

## Where things stand

- **Android 0.5.0 is published** as `v0.5.0-android` (owner's go, 11-23; tag at 0b08506): the
  standalone `GTAV.apk` that sets the game up on the phone. `main` did not move; `Latest` is
  still v0.3.0. What was checked for it: the end of `docs/RELEASE_v0.5.0.md`.
- **Linux:** `max-perf` builds and runs (developer build). The Linux package is not made yet: it
  waits for the iPhone shell.
- **Mac, iPhone, Windows: nothing after v0.3.0 was ever compiled there.** `max-perf` holds, on
  top of v0.3.0 (`main` 2a76184): the 41 commits of `issue-fixes` (test list in
  `docs/HANDOFF_MAC_WINDOWS_2026-11-11.md`), the v0.5.0 work on `perf-linux`
  (`docs/RELEASE_v0.5.0.md`), the multithreaded renderer (`docs/HANDOFF_MT_RENDERER_2026-11-22.md`),
  the Android CPU pass and the max-performance line (`docs/ANDROID_STATUS.md`, 11-23 sections).
- Versions are already set (05d6176): Mac app 0.5.0 build 5, iPhone 0.5.0, Android 0.5.0 / 5.
- Open pull requests #13 and #14 (Mac) were merged into `issue-fixes` as 63a9a1c and ed5f071 and
  are in `max-perf`; #16 (45 fps preset) is the owner's to decide.

## 1. Bring the tree up

1. `git fetch && git checkout max-perf && git pull` (at the tag `v0.5.0-android` or later).
2. Reset the SDK, FFmpeg, SDL, XenosRecomp and plume trees and apply the patches again, as for
   v0.3.0: `rexglue-sdk-gtav-linux.patch` (the one SDK patch for every platform; it matches the
   Linux SDK tree exactly), `ffmpeg-gtav.patch`, `sdl3-gtav.patch`, `xenosrecomp-gtav.patch`,
   `plume-gtav.patch`. All of them changed since v0.3.0. New untracked files come with the patch.
3. Rebuild the codegen tool and the SDK runtime (Mac and `ios-rel`), then the game and launcher.
4. Codegen check: **811/811 against `tools/release/expected.txt`**. The list changed twice since
   v0.3.0, so an old `rexglue` fails at "Checking the generated code". Linux x86-64 and Android
   arm64 (on the phone itself) both produce these 811 files byte for byte.

## 2. What is different for Apple targets

All generated code changed (this is the part most likely to need a fix):

- `gtav/gtav_manifest.toml`: `ctr_as_local`, `xer_as_local`, `cr_as_local` (v0.5.0 on
  `perf-linux`), and now `non_volatile_as_local`, `non_argument_as_local`, `skip_lr` with 66
  `share_registers` functions (4548a94). Guest registers live in host locals; functions marked
  `share_registers` and the register save/restore helpers keep them in the context. Built and
  run on Linux x86-64 and Android arm64 only. If something misbehaves on Apple arm64, compare
  with Android first: same architecture, same generated files.
- `skip_lr`: `XexGetProcedureAddress` falls back to the executable's module for its thunk.
- The register save/restore helpers are inlined in the release compile on Windows, Linux and
  Android, **not on Apple targets** (Mach-O has no overridable alias; `gtav/fs/gprlr_inline.h`).
  Nothing to do; it is a possible later gain if another way is found.

Compiled or defaulted **off** on macOS and iOS until someone tests them there (each is one switch;
they are what made Android faster, see "After it passes" below):

| switch | where | Apple default | Linux / Android |
|---|---|---|---|
| `REX_FP_SINGLE_MODE` | compile flag of the game code (`install.cpp`, `build_game_dll.py`, `gtav/CMakeLists.txt`) | off | on |
| `gtav_guest_irq_lock` | `gtav/src/guest_irq_lock.cpp` | 1 (global lock) | 0 |
| `gtav_fence_block_after_us` | `gtav/src/gpu_fence_wait.cpp` | -1 (spin) | 200 |
| `guest_poll_patience_us` | SDK | 0 | 300 on Android |
| `gtav_native_copy_resolves` | `backend.cpp` | off | on for Android |
| `GTAV_D3D_PROBES` | `gtav/CMakeLists.txt` | compiled in | compiled out |

**On** for every platform, so these need testing on the Mac and the iPhone:

- The renderer records a frame on its own thread through one draw path; extra record threads are
  a menu setting ("Record threads (experimental)", off by default).
- CPU rect lists share one expansion per frame (`gtav_native_rect_share`; the macOS / iOS /
  Android path).
- SDK waits in `threading_posix.cpp` (macOS and iOS use it): alertable waits round the remaining
  time up instead of spinning (`posix_alertable_sleep`), a semaphore release wakes one waiter
  (`posix_wake_one`), events and semaphores notify after the unlock (`posix_wake_unlocked`). Each
  is a live cvar: set it to false to rule it out.
- XMA: a kick no longer wakes the worker for nothing (`xma_kick_wakes_worker`), and FFmpeg
  computes its FFT / MDCT / VLC tables once (`ffmpeg-gtav.patch`). **Listen** to cutscene speech,
  radio and gunfire: on Linux and Android this was checked by the level meter in the log
  (`audio: 0 of 1875 frames were silence ... peak dB`), not by ear.
- Game threads are named `XThread<hardware thread>p<priority><id>`, with one lock around a
  thread's name. Without that lock Android aborted at boot (two threads naming a new thread at
  once); the code is the same on Apple.
- Two new hooks (`sub_83829CC0`, `sub_83829CD8`): the shell and the game library of a package
  must come from the same commit, as always.

iPhone only:

- **New settings menu layout** for phones (`GTAV_MENU_PHONE` in `gtav/src/native/menu.cpp`): one
  sheet under the overlay's Menu button, two columns, large rows, a Close button, "Touch button
  size" slider, "Reflections every other frame" under Advanced. Checked on a 2340x1080 Android
  phone only. It replaces the centred iPhone window: look at it on the 13 mini (notch, rounded
  corners, does it fit without scrolling while Advanced is closed) and fix what does not fit.
- The on-screen Back / Menu / Start buttons are 20% larger and further apart
  (`touch_controls.cpp`); the iPhone's scale (1.3) and inset (0.16) are unchanged.
- `gtav_native_cube_every=2` (the menu's reflections option) skips the reflection cube's draws on
  every other frame. Measured on Android (+2-5% fps); on the iPhone check that reflections look
  right with it on before leaving it in the menu.

## 3. Tests

Developer build first (fast to fix), then the release package. One line per test: pass / fail +
a sentence. Fix on `max-perf`, push, write it under "Results".

| # | test | pass |
|---|---|---|
| 1 | Build + codegen | compiles; 811/811 |
| 2 | Boot (new game folder and an existing save) | world reached, lit, no black world |
| 3 | 20 minutes of play: on foot, driving, a shoot-out, a cutscene | no crash, no hang, picture right |
| 4 | Sound by ear | speech to the end of a cutscene, radio, effects; no dropouts or bursts |
| 5 | WASTED (#21) above 30 fps | fades, respawn at a hospital |
| 6 | Keyboard + mouse (#11), F1 settings menu (#6), fullscreen (#8), Controls remap | as in the 11-11 handoff |
| 7 | Retina window (#14), input-attachment resolves off (#13) | fills the window; no grey world |
| 8 | "Record threads" 1-3 in the menu | no picture change; fps noted |
| 9 | Bug report from the menu | file written |
| 10 | **iPhone shell compiles** (`build/release-ios-shell`) | b2276fa fixed the same gap for Android; never compiled for iOS |
| 11 | iPhone 13 mini: ipa from the package's `--iphone` | world loads, touch plays, WASTED respawns, sound |
| 12 | iPhone settings menu and touch buttons | fits, every row can be hit, Close works |
| 13 | Release package, unattended install from the unzipped zip | 811/811, world reached |
| 14 | `scan_release_macos.sh` on the package | PASS |

Numbers worth writing down (same scene as for v0.3.0): Mac fps, iPhone fps cool and hot.

## 4. The iPhone shell, for everyone

When test 11 passes:

1. `tools/ios/make_shell.sh OUT` from the release commit (as for v0.3.0: `build/release-ios-shell`,
   SDK `rexglue-sdk/out/install/ios-rel`, `-mcpu=apple-a14 -ffile-prefix-map`; it refuses prebuilt
   native shaders and checks for build paths).
2. Tar it without AppleDouble files (`COPYFILE_DISABLE=1 tar ...`) as `ios-shell-v0.5.0.tar`.
3. Upload it to the **draft** release `v0.5.0` and tell the owner the commit it was built from:
   `gh release upload v0.5.0 ios-shell-v0.5.0.tar --clobber`. Linux created that draft on 11-23
   and put `android-v0.5.0.tar` there (the published `GTAV.apk` + `LICENSES/`). If the draft is
   gone, create it: `gh release create v0.5.0 --draft --target max-perf --title "v0.5.0 (draft)"`.

Linux (and Windows, when the owner boots it) then download it and finish their packages; their
`--iphone` builds the ipa from the player's own game with that shell. If the shell changes after
the upload, upload again and say so: all packages must carry the identical `ios/` folder.

## 5. The Mac package

1. `gh release download v0.5.0 -p android-v0.5.0.tar` and unpack it: the package's `android/`
   folder is just `GTAV.apk` + `LICENSES/` now (the app builds the game on the phone; the
   launcher's "Build for Android" only says to install the APK). Its `GTAV.apk` must have the
   SHA-256 printed in the `v0.5.0-android` release notes.
2. `tools/release/build_release_macos.sh OUT LLVM LIC --ios-shell <ios> --android <android> --zip`.
3. Tests 13 and 14 from the unzipped package, plus `GTAV --iphone` (811/811, 0 failed shaders)
   and the ipa on the iPhone.
4. Upload `gtav-recomp-macos-arm64.zip` to the draft `v0.5.0` and **ask the owner**. On their go
   the Mac release follows the pattern of `v0.2.0-macos` / `v0.5.0-android`: its own tag on the
   commit it was built from (`v0.5.0-macos`), the zip, and the complete corresponding source
   (`tools/release/make_source_archive.sh gtav-recomp-src-v0.5.0-macos.tar.xz v0.5.0-macos
   rexglue-sdk thirdparty/XenosRecomp thirdparty/plume`), notes with what was tested and the
   SHA-256 of each asset. Whether the desktop packages later also go out together as one `v0.5.0`
   marked Latest is the owner's call when Linux and Windows are ready.

## After it passes: the switches that made Android faster

Not needed for the release, and each wants its own A/B on the Mac and the iPhone
(`--gtav_native_ab='cvar=a|b'` flips a live cvar every 60 frames inside one run):

- `gtav_guest_irq_lock=0` (the game's interlocked operations without the global lock): Linux job
  threads 56-59% -> 35-38% of a core, Android +2-9% fps.
- `gtav_fence_block_after_us=200` (the game's GPU fence wait sleeps instead of spinning): a full
  core less heat on Linux and Android, same fps. On the iPhone heat is the frame rate.
- `REX_FP_SINGLE_MODE` for the game code (no denormal-mode switch around FP instructions).
- `gtav_native_copy_resolves=true` on the iPhone's tile GPU (Android: +1-3%).
- On the iPhone the record thread's core matters (Android: recording 23-28% faster on the prime
  core with the main thread moved off it, `gtav/src/android_threads.cpp`): iOS has no affinity
  call, QoS classes are the lever.

## Results (fill in)

| machine | commit | result | notes |
|---|---|---|---|
| Linux (developer build) | 51d0fea | runs | hidden run-arounds clean; package waits for the iPhone shell |
| Android (S23 FE) | 51d0fea | PASS | release APK, game built on the phone, see `docs/RELEASE_v0.5.0.md` |
| Mac (M1 Air, 8 GB) | 75fbe60 | unattended tests pass; owner's hands-on tests open | package on the draft; see "Mac and iPhone, 11-24" |
| iPhone (13 mini) | 75fbe60 | ipa built by the Mac package and installed; **not played yet** | tests 11 and 12 open; shell tar not uploaded |
| Windows | | | |

### Mac and iPhone, 11-24

MacBook Air M1, 8 GB, macOS 27.0.1, Xcode 27.1 (Apple clang 21). Written by the Claude session
on the Mac. Every run was hidden and muted (`--gtav_native_hidden=true --audio_mute=true`), so
nothing here was seen in a window or heard: tests 3, 4, 6, 11, 12 and the by-eye half of 7 are
the owner's.

Fixes pushed to `max-perf`:

- `95f7f79` two link errors: `sub_83829CC0` / `sub_83829CD8` were still in `d3d_probe_list.inc`
  (duplicate symbols where the probes are compiled in: macOS, Windows), and `bzero` was missing
  from `libSystem.tbd` / `libSystem_ios.tbd` (Apple clang turns two zeroing loops into it).
- `dce0198` `bug_report.cpp` called `system()`, which the iOS SDK refuses: the iPhone shell had
  not compiled since v0.4.0.
- `75fbe60` **xxHash read its input through a type-punned load** (its default for GCC/clang) and,
  with `XXH_INLINE_ALL`, Apple clang moved the stores of a key's `RenderFormat` fields after the
  hash. Clear pipelines of different colour formats shared one cache slot: Metal's validation
  layer counted about 400 "pipeline's pixelFormat does not match the framebuffer's" a second in
  play, and the frames after WASTED (only a clear) showed blue noise. Now
  `XXH_FORCE_MEMORY_ACCESS=0` in the four files that include `xxhash.h`; 0 mismatches after.
  **For the other machines:** the same `Hash(&local, sizeof)` pattern keys the pipeline,
  framebuffer, stream and in-pass resolve caches, so every platform should build from this
  commit or later and look again at anything that depended on those caches. Not tested: whether
  PR #13's grey world with in-pass resolves on the Mac was this bug (the Mac launcher still sets
  `GTAV_NATIVE_FETCH_RESOLVE=0`).
- `9aba5e8` `make_release_macos.py` still packaged `libTracyClient.dylib`, which the SDK has not
  built since f7707d5. `make_release_linux.py` still lists `libTracyClient.so` (not touched).

| # | result | what was seen |
|---|---|---|
| 1 | pass | after `95f7f79`; codegen 811/811 in the developer build, the package install and `--iphone` |
| 2 | pass | package: new game folder -> Display Calibration -> Story Mode -> prologue vault, lit; existing save -> Franklin's street, lit. A new game folder waits at Display Calibration for A / Enter: unattended runs need `--input_script` |
| 3 | open | owner |
| 4 | open | owner, by ear. Log meter only (muted runs): 181 of 187 ten-second reports had 0 silent frames, the rest 1-3 |
| 5 | pass | after `75fbe60`: fade to black, respawn at the hospital, 46-49 fps, no noise in 111 captures. Before: blue noise for the ~10 s after WASTED |
| 6 | open | owner (keyboard, mouse, F1 menu, fullscreen, Controls) |
| 7 | half | #13: no grey world in any capture with the launcher's default. #14: the log shows a 2560x1440 swapchain at contents scale 2.0; "fills the window" needs eyes |
| 8 | pass, no gain | no crash; captures with 1 and with 3 threads look right; table below |
| 9 | pass | report zip written (developer build, `report` debug command; the menu row itself not clicked) |
| 10 | pass | after `dce0198`; also built by `IOS_SHELL=1 build_release_macos.sh` |
| 11 | open | `GTAV --iphone` from the package: 811/811, 3,617 shaders, 0 failed, `GTAV.ipa` 169 MB. Installed with `tools/ios/install_ipa_dev.sh` as 0.5.0 (202611232352); the phone's game data and save are in place. The phone was locked, so it was not started |
| 12 | open | owner |
| 13 | pass, copy stage not run | below |
| 14 | PASS | `scan_release_macos.sh`: 1,326 files, 558 MB of sources, 0 failing |

Test 13: `GTAV --unattended` from the unzipped zip, `GTAV_JOBS=2`. Codegen matched 811/811, the
406 files compiled in 483 s, `gtav_game.dylib` linked, "Done. Press Play."; the launcher then
started the game for both cases of test 2. **The copy stage did not run:** the internal disk has
no room for the 15 GB, so the game folder's data files were links to an earlier install on the
external drive, each hashed equal to `expected.txt` first and listed in `data/install_state.txt`
(the installer's own list of files verified by an earlier run). Opening the discs, the
verification review, codegen, compile, link and launch ran as for a player. Extracting the
15 GB from the ISOs was **not** exercised with this build on the Mac.

Test 8, "Record threads" against off inside one run (`--gtav_native_ab`, package build, existing
save, Franklin's street, 6 to 10 reports of 1,200 frames each):

| record threads | frame rate against off | render thread CPU per frame |
|---|---|---|
| 1 | -4.1% | 13.2 -> 11.4 ms |
| 2 | -2.3% | 9.5 -> 8.1 ms |
| 3 | -0.3% | 7.1 -> 5.1 ms |

The render thread does less, the frame rate does not rise: leave it off on this Mac. Draws and
passes per frame were equal on both sides (within the scene's own movement).

Frame rates (hidden window, uncapped, other jobs running on the Mac): prologue vault about 55;
Franklin's street 45-57 with 2,000-2,800 draws, 31-40 while another job held a core. Numbers in
a visible window and the iPhone's are the owner's to take.

Uploads to the draft `v0.5.0`:

- (Replaced on 11-26 by the `34fc528` zip, see the last section.) `gtav-recomp-macos-arm64.zip`, 195,560,842 bytes, SHA-256
  `e1d5e6f27f90401954e39b6533e1f41ff47f6ef030707f4d958cea27d1bf94e8`. Binaries built from
  `75fbe60` with a clean tree; packaged by `9aba5e8`'s `make_release_macos.py`. Its `android/` is
  the published `GTAV.apk` (SHA-256 `0856e2fc...e615`).
- `ios-shell-v0.5.0.tar` is **not uploaded**: it waits for test 11. It is ready on the Mac
  (`~/gtav05/out2`, the `ios/` folder from `build/release-mac/ios-shell-app` at `75fbe60`,
  29,650,432 bytes, SHA-256 `8da291884bec40f501aa17951bb2a0048d16751e7b31950890311ea04eb4d957`).
  The Mac zip's `ios/` holds the same binaries with the ad-hoc signatures `make_release_macos.py`
  gives every Mach-O in the app, so they differ byte for byte from the tar's.

Two of the "After it passes" switches, measured on the Mac only (package build, existing save,
hidden, uncapped; defaults **not** changed):

| switch | frame rate | CPU |
|---|---|---|
| `gtav_guest_irq_lock` 1 -> 0 | +0.3% (9 A/B reports, 48-58 fps both sides) | not measured |
| `gtav_fence_block_after_us` -1 -> 200 | +0.3% (9 A/B reports) | 4.6 -> 4.1 cores over 3 minutes in the world (one run each) |

Neither crashed or changed the picture's draw counts in 4 minutes. On the Mac the lock mode
makes no difference to the frame rate; blocking in the fence wait costs nothing and saves about
half a core, as on Linux and Android. Not done: `REX_FP_SINGLE_MODE`, anything on the iPhone,
PR #16.

### iPhone played, PR #49, taint scan (11-24 evening)

**The owner played the 0.5.0 ipa on the 13 mini: "runs slow, a lot of graphical glitches".** From
the phone's log: the first launch after the update compiled every pipeline again (all shaders
changed since 0.3.0). The frame's own compiles (six threads) took 1.5-3.5 s each, against
0.3-0.5 s on the M1 Air, while the four warm-up threads were busy with the 2,644 pipelines of
earlier runs. Draws were skipped for 95 s after the world appeared, up to 1,446 of about 2,600 a
frame. A phone holds the previous frame for only 0.5 s, so the player saw missing geometry. The
frame rate was 15-18 during that window and 26-30 (the cap) after. On the second launch, draws
were missing for about 25 s. Steady state matches 0.3.0 (23 fps in the city in November; the GPU
frame is now 23 ms against 33). On 11-01, the first iPhone day, the same storm skipped draws in
13,322 frames. No errors, failed shaders or memory trouble in either launch. The settings menu
works (the owner changed the frame cap, vsync and penumbra between launches).

Proposed fix, **local, not pushed until it is measured on the phone**: the iPhone's warm-up
threads drop to `QOS_CLASS_UTILITY` and start no compile while one of the frame's is queued or
running. Other platforms get only the counter. The test is an A/B on the phone from the
pre-update caches (pipeline files restored, Metal cache emptied); it waits for the phone to be
unlocked.

**PR #49** (a tester's M4: fullscreen hotkeys toggled twice; packager needed Tracy): the
fullscreen fix is real (Option+Enter / F11 never changed fullscreen on macOS or Linux), but as
sent it does not compile for iPhone or Android (`use of undeclared identifier 'sdk_window_id_'`:
the window ids are desktop-only). The owner: if it breaks, leave it out. A version guarded for
the phones builds for macOS and passes an iPhone `-fsyntax-only` check. It sits on a local
branch and goes into the Mac package only if that package boots. The packager half is
`9aba5e8` already. The PR is not merged, commented on or closed.

**Taint scan:** the WD drive dropped out during a scan of a repackaged Mac zip, `update.rpf` was
not read ("496 MB scanned" instead of 558) and the scan still printed PASS. `taint_scan.py` now
fails on a source that is missing, unreadable or yields nothing. **Linux:** `scan_release.sh`'s
default GENERATED_DIR (`~/gtav-e2e-linux-cg/generated/default`) does not exist on the 4050, so
pass it explicitly or the scan now stops there. That repackaged zip was not uploaded; the draft
still holds the `75fbe60` zip.

### iPhone fix measured, PR #49 in, new Mac zip on the draft (11-26)

**iPhone A/B** (13 mini, both launches from the pre-update pipeline files with the Metal cache
emptied, muted, one launch each; the owner was playing during the test launch, so the scenes
differ):

| | 0.5.0 as shipped | warm-up gives way (`e15a470`) |
|---|---|---|
| frames with 100+ draws missing | +34 s to +128 s | +34 s to +88 s |
| most draws missing in a frame | 1,794 | 1,569 |
| the frame's compiles, average per 50 | 1.3-3.4 s | 1.0-1.9 s |
| first 250 of the frame's compiles done | 172 s after launch | 96 s after launch |
| frame rate while compiling | 17-20 | 15-22 |

The change helps but does not remove the storm: the frame still needs about 300 pipelines on the
first launch after an update, and the phone's Metal compiler takes about a second each. `e15a470`
is pushed. The test launch ran 11 minutes (thermal "serious" from minute 9) and its log then
stops with no "background" line; several 0.3.0 launches on 11-01 ended the same way, and no
crash report could be pulled over the network connection. The phone now runs that test build
(0.5.0's `ios/` with only `gtav` rebuilt at `e15a470`); its iPhone code is the same as
`34fc528`'s.

**PR #49:** the fullscreen fix, guarded for the phones, is `34fc528` on max-perf (author kept).
The Mac package with it booted hidden from the existing save for 420 s, city at ~50 fps, no
errors. The first boot attempt crashed ~15 s into loading (guest read of 0): the WD drive came
back as `/Volumes/Untitled` and nine links of the test install still pointed at
`/Volumes/Untitled 1` (the disc-2 `part*.rpf`), not the change. The PR itself is still not
merged, commented on or closed.

**New Mac zip on the draft** `v0.5.0`: built from `34fc528` (clean tree) by rebuilding the release
trees `tools/release/build_release_macos.sh` had configured (only `renderer.cpp` and `backend.cpp`
recompiled, for the Mac and the iPhone shell; launcher identical apart from its signature;
iPhone interface and exports identical), `make_shell.sh`, then `make_release_macos.py`.
`gtav-recomp-macos-arm64.zip`, 195,560,880 bytes, SHA-256
`0c16164e6da853d1b45eeeb616ffcc61efb638e03cd3e24f5199a1dd61cdd979` (GitHub's digest matches).
Unzipped: test 13 again (hidden, muted, existing save, 420 s, no errors; ~50 fps until the
Mac's load average went past 100 from other jobs). Taint scan (strict) PASS: 1,326 files,
558 MB of sources read, 0 failing.

`ios-shell-v0.5.0.tar` rebuilt from `34fc528` (29,650,432 bytes, SHA-256
`0fb279d901d11ac708e6aaae7ce9247e96c9f37210b77bf2997ce5f3b10c5193`, owner names root/wheel),
same file list as the `75fbe60` tar. **Still not uploaded**: it waits for test 11 on the phone.

### iPhone speed and flicker (night of 11-27)

Owner's goal: about 40 fps in game with no flickering on the 13 mini; picture quality may go down
if a Quality option stays; uncapped. Eight commits on `max-perf`, `73b16a0` to `23a8dfe`. Every
number below is from the game's own log on the phone (muted, the owner's settings file ignored
unless said), default 960x540 internal resolution, uncapped.

**Result.** One launch each; a launch brings the character in with a different switch scene every
time, which is most of the spread between launches:

| | before (11-25/26, 30 fps cap) | now |
|---|---|---|
| standing, light start scenes (1,830-2,140 draws) | 25-33 | 44.7-45.5 fps |
| standing, the heaviest start scene (2,460-2,960 draws) | | 27.3-30.0 fps |
| scripted walk (60-70 s windows, three launches) | 7-19 moving | 37.1-39.1, 38.8-40.8 |
| frames shown with draws missing, warm Metal cache | minutes of them after installs | 0 in seven launches, incl. 19.5 min of walking |

40 fps holds where the scene is light and for the first minutes; the phone reaches thermal state
"serious" about 3 minutes into a run and the same scene then costs about a third more GPU time.

**What made the difference** (commit, measured effect):
- `adcda53` the game code built with `-DREX_FP_SINGLE_MODE` (as on Android) and the shell without
  the D3D probes: recording 7.6 -> 5.6 ms per 1,000 draws, all threads 169-173 -> 140-150 CPU-ms
  a frame at 3,200-3,600 draws.
- `d45998f` interlocked operations without the global lock (+3.3% standing, 2 of 2 report lines)
  and fence waits that sleep (+3.7%, 2 of 2).
- `c66f004` the flicker: warm-up ordered by recent use, a start-up gate, the last complete frame
  held up to 5 s while pipelines compile, and a counter in the log for every frame that reached
  the screen incomplete. `6fef3f8` sampler key (a way to fail removed; not seen failing).
- `23a8dfe` uncapped launch arguments; both presets keep the per-pixel sun shadows and GPU lag 6
  on the iPhone; `gtav_settings.ini` gets `settings_version=2` and an older file is corrected once
  (the dev phone's had the shadows off and lag 4 from the "Max performance" button: checked, the
  file on the phone now reads version 2, shadows on, lag 6).

**Tried and left off, with the numbers:**
- Anonymous guest memory (`869b6d0`, `--ios_guest_memory=file|anon|phys|auto`): page-ins while
  walking 114-624 a second -> 2-16, but 280 MB more footprint (1,993 MB against 1,713; headroom
  down to 354 MB after 3.5 minutes) and no frame rate for it (35.3 / 31.6 / 26.5 fps in that
  launch). Default stays `file`; `auto` picks anon only from 3,000 MB of headroom (not a 4 GB
  phone; that line is not measured).
- A lower internal resolution. Same-run A/B with a flat pixel shader on every draw: 19.9 -> 19.3
  ms of GPU a frame. The GPU's time is per render pass and per draw (162-190 passes a frame), not
  in the pixels, so no resolution option went into the menu. One start at scale 0.5: 9.4-9.6 GPU
  ms per 1,000 draws against 8.3-12 at 0.75.
- Settings A/Bs that changed nothing or lost: copy resolves (-3.3%), guest poll patience,
  hash threads 3|0 (+2.5% standing, -4.2% hot), record workers, draw clears, load clears
  (-2.8%), keep targets, cube map every 2nd frame (-3.6%), sun shadows on|off 24.3|24.5, GPU lag
  6|4 21.8|22.3.

**Soak** (final build, 06:08, a scripted walk that turns the camera: beach, a shop, a respawn,
downtown, a rainy freeway; screenshot every 20 s, all correct):

| minutes | fps | draws | GPU ms | headroom min | thermal |
|---|---|---|---|---|---|
| 2-7 | 39.8 | 2,674 | 21.5 | 623 MB | fair -> serious |
| 7-12 | 35.1 | 2,310 | 24.9 | 626 MB | serious |
| 12-17 | 25.6 | 2,537 | 33.4 | 587 MB | serious |
| 17-19.5 | 21.3 | 1,750 | 45.5 | 594 MB | serious |

0 frames shown with draws missing; 13 holds, the longest 3.1 s (a new area). The frame rate
halves with heat: GPU time per 1,000 draws went 8 -> 26 ms. **The game then ended, 19.5 minutes
in, with no crash report and no jetsam report**, 653 MB of headroom in the last memory line, a
burst of 60-160 ms frames in its last seconds. Two earlier long runs ended the same way (11-27
03:28 after 12.5 minutes; the 11-26 test launch after 11 minutes), all hot. Nothing in the game
or the SDK exits without logging first. Not solved; a build that logs iOS's thermal changes and
memory warnings as they happen is being tested.

**Soaks 2 and 3** (08:14 and 08:55, same walk, builds that log iOS's signals and, while hot or
short of memory, a once-a-second memory line split into Metal / malloc / compressed / file):

- Soak 2 ended 22 minutes in the same silent way, and the log now shows how: CPU memory
  (malloc) rose by 420 MB within 9 s while Metal stayed flat, iOS sent two memory warnings
  (464 MB left), and the log stopped 4 s later. Earlier in that run the GPU frame went from
  ~21 ms to 200-265 ms for 12 minutes (4 fps) with the character facing the sun at sunset: one
  extra effect (+3 clears, +4 resolves, +1 target), not heat. Not seen again since; the renderer
  now times its passes by itself when the GPU frame averages over 80 ms (`5258304`).
- Soak 3 ran **62 minutes** and was still running when stopped by hand. fps by 5-minute window:
  36.7, 34.1, 27.5, then 30.6 over the last 47 minutes (thermal serious from minute 2, charging,
  battery 60 -> 50%). Lowest headroom 598 MB. Two transient malloc spikes: +256 MB for about a
  second at minute 35 (memory warnings at 464 MB left; back to normal a second later) and +66 MB
  at minute 60. Same signature as soak 2's end, only shorter. Both came during runs of 60-365 ms
  frames. Which code allocates is not known yet: a build that logs every allocation of 24 MB or
  more with its call stack (`--gtav_native_big_alloc_mb`, off by default) is in soak 4.
- Flicker in soak 3: 0 frames with draws missing for 45 minutes, then 273 in one burst
  (minute 45): one pipeline took about 16 s to compile (others average 83 ms), the hold ran its
  5 s and then that one draw (1,896 indices) was missing for 11 s. Holds otherwise 0.02-2.7 s,
  about one every 2-3 minutes once hot. Compiles of 2 s or more are now logged with their shaders.

**Soaks 4-7: the malloc spikes were fence copies** (10:18-13:00, same walk, the big-allocation
log on at 24 MB):

- The logger (`6f1b29b`) showed what allocates: fence-copy arenas growing to 32, 64 and 128 MB in
  `FenceFlush`. A fence copy keeps a draw's vertex data for the frame the game is still building
  when it waits on a fence mid-frame; normally 0-2 draws per 300 frames, in bursts 160-310 MB per
  300 frames (up to 11,684 draws). The fence arenas shared the packet arenas' size memory (64 MB),
  so every new one started at 64 MB, and big ones were kept as spares.
- `dc59509`: fence arenas start at 1 MB with no shared peak, spares over 32 MB are freed, and a
  budget for fence copies (`gtav_native_fence_copy_budget_mb`, 96 on the iPhone, 0 = none
  elsewhere). Its first version (soak 6) still started each arena at the old peak, so frames with
  copies allocated and freed 64 MB up to 270 times a minute; fixed in the same commit.
- Soak 7 (`dc59509`, 36.6 minutes): iOS sent two memory warnings, with our malloc flat (690 and
  677 MB) and 944 / 878 MB available: the system's pressure, not a spike of ours (soaks 2 and 3:
  +256-420 MB within seconds before each). Lowest available 734 MB. 64 MB arenas 29 times in 37
  minutes (soak 6: up to 270 a minute). fps by 5-minute window: 38.7, 30.7, 30.9, 26.3, 20.5,
  23.2, 31.1, 28.9 (thermal serious throughout, charging at 40%).
- The budget never triggered in soaks 6 and 7 (it counted bytes used), yet one burst still grew an
  arena to 128 MB (64 + 128 MB held during the copy). It now counts memory held, with the
  arena's next doubling once it is past half full, so no fence arena passes 64 MB. Soak 8 (40
  minutes, the full run): no 128 MB allocation, 64 MB 9 times, 0 memory warnings, lowest
  available 879 MB; the budget held back copies in 5 of 237 report windows (11,843 draws), all in
  the first 2.5 minutes (loading and the first switch scene), none while walking. fps by
  5-minute window: 34.5, 29.7, 31.7, 26.8, 24.4, 26.2, 26.8, 31.8.
- Soaks 6 and 7 ended with "app in the background" (12:00:41 and 12:56:08) while the owner was
  using the phone; not a crash. No JetsamEvent reports exist on the phone after 11-11, so the
  silent ends of soak 1, soak 2 and the 11-26 test launch are put down to these spikes by their
  log signature, not by a report.

**Cold Metal cache** (`GTAV_IOS_CLEAR_METAL_CACHE=1`, one launch, with a 3 s hold): a compile takes
0.9-1.0 s on average; the 409 pipelines of the last three runs were ready after 177 s; the boot
screens were held 15.8, 1.1 and 3.4 s; walking into a new area gave holds of 1.5, 3.0 and 2.2 s
and one burst of 8 frames with draws missing behind a 3.4 s compile (hence 5 s now, not measured
again). **Open:** while the rest of the list recompiles (2,300 pipelines, four threads at the
utility class, about 18 minutes of running) the game runs late: 26 fps walking against 39-41, and
21 fps standing in the launch after. An app update keeps Metal's cache (three installs that
night: the whole list ready in 1-9 s); a release that changes the shaders does not.

**Next for the frame rate** (not done): fewer render passes. The in-pass (framebuffer fetch)
resolves are off on the iPhone because they strobed white (`dbc731d`); they are about 45 passes a
frame, plus 14 writeback compute passes; the G-buffer and the lighting target are each entered 8
times a frame. Skipping every resolve (which breaks the picture) is worth 21.8 -> 19.5 ms. Then
heat: the phone throttles within minutes, and a quality step-down by thermal state does not
exist yet.

**How it was measured:** `--gtav_native_ab=name=a|b` flips a live setting every 60 frames and
prints both sides (set while running through `Documents/logs/menu_set`); `--input_script` walks
the character (the file is reloaded when its modification time changes: `devicectl copy to` keeps
the source's time, so push a freshly dated copy and look for a second "input script: N steps"
line); `GTAV_NATIVE_SHOT_DIR` + `GTAV_SHOT_SECS` for screenshots; the log's memory line now has
the battery and a line with CPU by thread (`c435956`). `--gtav_native_pass_gpu_times` halves the
frame rate while it is on: proportions only. Battery limits a night on a 5 W charger: the game
drains about 1% a minute while charging.

### 11-27 evening: Linux's waits ported to the Mac and the iPhone

The owner asked whether the Mac and iPhone builds have every fix the Linux build has. Every
issue fix does: no Linux branch, tag or local commit holds a commit that `max-perf` lacks, and no
issue fix is behind a Linux-only `#if`. Three Linux/Android performance paths were not on Apple;
two of them are now:

- **Waits on several handles wake at once** (`posix_multi_wait_wake`, SDK `threading_posix.cpp`,
  on by default, live). Linux and Android sleep on a futex that every signal of a watched handle
  bumps; Apple polled every 1 ms, so a game thread waiting on a few events woke up to 1 ms late,
  every frame. Apple now sleeps in `os_sync_wait_on_address` (macOS 14.4 / iOS 17.4; older
  systems keep the poll; the iPhone build targets iOS 17.0 and links the calls weakly).
  - M1 Air, package arguments, existing save, hidden, 16 same-run A/B report pairs in the world,
    off | on: 58.1 | 58.6 fps (+1.0%, on ahead in 10 of 16), p90 20.1 | 19.4 ms, renderer CPU
    6.84 | 6.28 ms a frame (lower in 16 of 16). The Mac is GPU-bound (16 ms a frame).
  - iPhone 13 mini (`GTAV_mw_1127.ipa` = the 0.5.0 package's ipa with the new shell and runtime):
    the first launch after the install recompiled the pipeline list for 190 s, and the owner
    needed the phone 80 s after that, so only two clean pairs, both walking: 24.8 | 26.7 and
    29.7 | 33.9 fps, p90 55.8 | 53.0 and 47.0 | 41.1 ms. 8.5 minutes, no crash; it ended with
    "app in the background". To be measured again with a warm cache.
- **Sleeping fence waits and no global lock on interlocked operations** are now on for the Mac too
  (`gtav_fence_block_after_us=200`, `gtav_guest_irq_lock=0`; measured 11-24, table above:
  +0.3% fps each, 4.6 -> 4.1 cores). Windows keeps the spin and the lock until measured.
- Not ported: the write watch (`write_watch.cpp`: userfaultfd on Linux, page protection on Windows
  and Android; Apple spot-checks textures instead), thread placement on hybrid Intel CPUs, and the
  Linux crash backtrace.
