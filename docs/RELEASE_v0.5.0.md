# v0.5.0 build sheet

v0.5.0 replaces the unpublished v0.4.0 (everything staged for 0.4.0 is in it). Same shape as
v0.3.0/v0.4.0 (`docs/RELEASE_v0.3.0.md`, `docs/RELEASE_v0.4.0.md`); the differences are below.
Branch `perf-linux` (it contains all of `issue-fixes`). Version strings set in 05d6176: Mac app
0.5.0 / 5, iPhone 0.5.0, Android 0.5.0 / versionCode 5.

**Nothing is published until the owner says Mac and Windows passed and says to ship** (no tag, no
push to `main`, no public release). The draft and its helper uploads are fine once the owner says go.

## Android alone first: `v0.5.0-android` (owner, 2026-11-23)

The owner asked on 11-23 to publish the standalone APK (no game code) as the 0.5 release without
waiting for Mac and Windows. That release is the tag `v0.5.0-android` on branch `max-perf`
(perf-linux + the multithreaded renderer + the Android CPU pass + the max-performance line), with
`GTAV.apk` and `gtav-recomp-src-v0.5.0-android.tar.xz`. `main` does not move and `Latest` stays on
v0.3.0 until the desktop packages ship; text: `docs/RELEASE_NOTES_v0.5.0-android.md`.

Checked for it on Linux and the S23 FE (results at the end of this file): the release APK
installed over the test build, the game built on the phone by the app, world reached, sound, the
phone settings menu, taint scan of the unpacked APK.

**The desktop and iPhone packages still wait for Mac and Windows**, and `max-perf` was never
compiled there. What it changes for them, on top of the list below:

1. Re-apply all patches: `rexglue-sdk-gtav-linux.patch` grew ~640 lines since a27ae72 (waits,
   thread names, the name lock, codegen for non-argument locals, the pch template),
   `plume-gtav.patch` ~300, `ffmpeg-gtav.patch` ~320 (XMA tables computed once). Rebuild the codegen
   tool and the runtime.
2. **All generated code changed again.** The manifest now also has `non_volatile_as_local`,
   `non_argument_as_local` and `skip_lr` with 66 `share_registers` functions, and
   `tools/release/expected.txt` has the gen lines for it (4548a94). Linux x86-64 and Android arm64
   build and run it; Windows x86-64 and Apple arm64 have not compiled it. Check 811/811, a boot and
   a long run-around before anything else.
3. Off on Windows, macOS and iOS by build or by default, until tested there (each is a switch):
   `REX_FP_SINGLE_MODE` (compile flag of the game code), `gtav_guest_irq_lock` (1 there),
   `gtav_fence_block_after_us` (-1 there), `guest_poll_patience_us` (Android only),
   `gtav_native_copy_resolves` and `gtav_android_record_cores` (Android only); the D3D call-count
   probes stay compiled in there (`GTAV_D3D_PROBES`).
4. On for everyone, so test it: several record threads and the single draw path
   (`docs/HANDOFF_MT_RENDERER_2026-11-22.md`), the SDK's wait changes (`posix_alertable_sleep`,
   `posix_wake_one`, `posix_wake_unlocked`: macOS and iOS use that file too),
   `xma_kick_wakes_worker` and the shared XMA tables (listen to cutscene speech and radio),
   game threads named by role, the thread-name lock.
5. iPhone: the settings menu has a new phone layout (`GTAV_MENU_PHONE` in `menu.cpp`, uses
   `touch::MenuButtonBottom`), checked on Android only. Build the shell and look at it.

## What changed for every platform

1. **Generated code changed.** `gtav/gtav_manifest.toml` now has `ctr_as_local`, `xer_as_local`,
   `cr_as_local` in `[entrypoint]`, and the SDK codegen (`rexglue-sdk-gtav-linux.patch`, as always the
   only SDK patch for all platforms) has the matching code. `tools/release/expected.txt` gen lines
   were regenerated (811 files, 405 changed). **Windows and Mac must rebuild `rexglue`/`rexgluerd`
   from the current patch**, or the install stops at "Checking the generated code". Determinism was
   checked: Linux x86-64 and Android arm64 codegen give the same 811 files byte for byte.
2. **Release compile inlines `__savegprlr/__restgprlr`** (`gtav/fs/gprlr_inline.h`, force-included
   into generated sources by `install.cpp Compile()` and `build_game_dll.py`) for Windows, Linux and
   Android targets; not Apple targets (Mach-O has no overridable alias). All three packagers copy
   `tools/game_dll/fs/gprlr_inline.h`. Windows: check a full install compiles (COFF uses the same
   `__attribute__((alias))` path the release pch already uses) and the game boots.
3. **Defaults uncapped:** `gtav_fps_limit` 0 and `gtav_native_vsync` false in code (the launchers
   already passed these). Vehicles uncapped too (`gtav_vehicle_30fps` false; menu option).
4. **Android builds the game on the phone.** The APK carries rexglue, clang, lld (LLVM 19.1.5),
   XenosRecomp and dxc (416fab6b) for arm64 Android plus `assets/kit.zip`
   (`tools/android/build_llvm.sh`, `build_dxc.sh`, `build_xr.sh`, `make_kit.sh`, `build_apk.sh`
   RELEASE=1). The desktop packages' `android/` folder is now just `GTAV.apk` + `LICENSES/`
   (`make_shell.sh`); the launchers' "Build for Android" / `--android` only says to install the APK.
   No code change needed on Windows/Mac for this: copy `android-v0.5.0.tar` from the draft.

## Order on the owner's go

1. **Linux** uploads `android-v0.5.0.tar` (dist/v0.5.0/android) to the draft and the source archive.
2. **Mac** reapplies the patch, rebuilds the SDK (codegen tool!), the Mac app and the iPhone shell,
   uploads `ios-shell-v0.5.0.tar`, builds `gtav-recomp-macos-arm64.zip` with both folders.
3. **Windows** reapplies the patch, rebuilds the SDK (rexgluerd!), `gtav_fast.exe`, the launcher,
   builds `gtav-recomp-windows-x64.zip` with both folders.
4. **Linux** finishes `gtav-recomp-linux-x64.tar.gz` with the iOS shell.
5. Each desktop: unattended install from the unpacked package (811/811 gen match, world reached),
   taint scan PASS. Android: the APK's on-phone install on the S23 FE.
6. Final assembly and publishing as in v0.4.0 step 5-6 (`RELEASE_NOTES_v0.5.0.md`).

## Linux status (this machine, 2026-11-22)

- Compat container build at 05d6176: runtime, rexglue, XenosRecomp, gtav_fast, launcher. Floors
  GLIBC 2.35 / GLIBCXX 3.4.32 (unchanged).
- Release-path check: release codegen (seeded partition) -> `build_game_dll.py` with the official
  LLVM 19.1.5 (407 files, 690 s on 6 jobs) -> `gtav_game.so` in a `make_release_linux.py --compat`
  folder: boots hidden, 3,600 shaders translated by the packaged XenosRecomp + dxc in ~2.5 min,
  7 min in the world, 0 failed shaders, no errors.
- Android tools on arm64 Android (Quest 3 used for the tool checks): clang/lld run; codegen
  811/811 identical to the PC (1,036 s as an adb process); one average generated file compiles in
  10 s / 133 MB; XenosRecomp + dxc output identical to the desktop's (HLSL and SPIR-V hashes).
  APK 173 MB (`dist/v0.5.0/android`).
- Left: full on-phone install + build + play on the S23 FE; package tarball after the iOS shell;
  unattended install from the unpacked tarball; `scan_release.sh` (include the APK's new tools).

## `v0.5.0-android`: what was checked (2026-11-23, Linux + S23 FE)

- **APK** `dist/v0.5.0/android/GTAV.apk` (`tools/android/make_shell.sh`, built at 51d0fea):
  180,475,225 bytes, SHA-256 0856e2fc8cfebc610b1af0ed52d3f3f9fe7ce3180ee8184de58cb6b7adfbe615,
  versionCode 5 / 0.5.0, not debuggable, shell `libgtav_fast.so` 0e816729..., signed with the same
  certificate as the 0.3.0 APK (SHA-256 3b070344...b33231b1), so it installs over it.
- **Game built on the phone by the app.** Release APK installed over the test build with the
  game stamp (`files/game.txt`) removed, which is what an update from 0.3.0 looks like to the
  app ("The game is not built yet. Press Install."). Codegen 4.5 minutes, "Generated code
  matches the reference build (811 files)", 406 files compiled 6 at a time and linked: 14 min
  48 s from the tap to the game's first log line, battery 40-42 C. The app started the game;
  open world reached. Log: the one known `gamecache:\data\gta5_cache_x.dat` line per boot,
  nothing else; the audio lines show the mix with no late frames.
- **Settings menu and touch buttons** checked on the phone with a test APK holding the same
  shell (same hash): screenshots, Advanced opened by a tap, the scroll bar dragged.
- **Not run again with this APK:** the copy and check of the disc images (`install.cpp` changed
  only in the compile flags since the on-phone install of 11-22), and no other phone model.
- **Taint scan** of the unpacked APK (kit.zip unpacked too, 744 files): against the standard
  sources (`scan_release.sh`, generated code of this manifest in `build/mp/gen_and`) plus a
  cross-built `libgtav_game.so`. Everything is short boilerplate runs except two findings, both
  against that library only: `libgtav_setup.so` 376 bytes and `libxenosrecomp_exe.so` 1,464 bytes,
  each inside the file's `.plt` section (arm64 jump stubs, `adrp x16 / ldr x17 / add x16 / br x17`
  per import; equal wherever two libraries' import tables sit at the same page offsets). No game
  content. Unpack the APK before scanning it: the scanner does not look into zip files.
- The standard scan alone (`scan_release.sh` on the unpacked APK, without the extra library):
  **PASS, 0 failing**.
- **Published** 2026-11-23 21:16 local: https://github.com/andrewnakas/gtav-recomp/releases/tag/v0.5.0-android,
  tag at 0b08506 (51d0fea + documentation), `Latest` left on v0.3.0 (the desktop packages are
  there), `main` not moved. Assets, both downloaded again and compared byte for byte:
  `GTAV.apk` 0856e2fc8cfebc610b1af0ed52d3f3f9fe7ce3180ee8184de58cb6b7adfbe615,
  `gtav-recomp-src-v0.5.0-android.tar.xz` f0b17693a2d4e19db1f0dad0f5e18281e8c4ad0fd9aa95cfe44aaedf9ce1cc9e
  (`TMPDIR` on a disk for `make_source_archive.sh`: `/tmp` is RAM here).
- The **draft `v0.5.0`** (not public, target `max-perf`) exists as the exchange point between the
  machines and holds `android-v0.5.0.tar` (`dist/v0.5.0/android`: that APK + `LICENSES/`). The
  Mac uploads `ios-shell-v0.5.0.tar` there (`docs/HANDOFF_MAC_2026-11-23.md`); then Linux can
  finish `gtav-recomp-linux-x64.tar.gz` (compat build at the release commit, package with the
  iOS shell, unattended install, scan).

## `v0.5.0-macos` (Mac, 2026-11-27)

The owner asked on 11-27 to ship the Mac package on its own (with the iPhone shell and the
Android app in it), as `v0.5.0-macos` like `v0.5.0-android`: `Latest` stays on v0.3.0 and `main`
does not move until Windows and Linux are ready. Text: `docs/RELEASE_NOTES_v0.5.0-macos.md`.

- **Built from `max-perf` at 5c433f3** (the iPhone speed work of 11-27 included; the tag adds
  documentation only) with `~/gtav05/rebuild_pkg5.sh` on the Mac: SDKs (mac-rel, ios-rel) as of
  the patch, the release trees of `tools/release/build_release_macos.sh`, the iPhone shell
  configured with `-DGTAV_D3D_PROBES=OFF` (its `interface/gtav_hooks.h` is 59 lines; the game
  library `--iphone` builds must match it), MoltenVK iOS taken from the 11-26 shell (the WD drive
  that held the xcframework failed).
- **`ios-shell-v0.5.0.tar` on the draft is the one for every package** (SHA-256 in the draft's
  text). Windows and Linux: download it and package with it; the iPhone builds of all three
  must use the identical `ios/` folder. It replaces nothing published.
- Checked: game started from the unzipped package, open world from a save, 45-57 fps hidden;
  taint scan PASS (0 failing; the same short boilerplate runs as the 11-26 package);
  `GTAV --iphone` from the package: 811 of 811 generated files match, 3,617 shaders, 0 failed,
  ipa built. The unattended disc install was not run again (the disc images were on the failed
  drive); the installer's Mac path has not changed since the 11-26 package, whose disc install
  passed.
- The taint scan needs about 3.5 GB of memory with its 64 MB chunks; on an 8 GB Mac under load
  it was run with `CHUNK = 16 << 20` in a copy of `taint_scan.py` (chunks overlap by one window,
  so the result does not depend on the chunk size).

### `v0.5.0-macos` files replaced (2026-11-27 evening)

The owner chose to replace the published files instead of a 0.5.1 (5 downloads by then): the
zip and the source archive were rebuilt from **`max-perf` at 483ffbe** (8771bee: waits on several
handles wake through `os_sync_wait_on_address` on Apple; 483ffbe: Mac defaults
`gtav_fence_block_after_us=200`, `gtav_guest_irq_lock=0`), and the tag moved to the commit that
adds this entry. The first zip was SHA-256 `04e2be3e…cb9a3` (tag then at c7c64ac).

- Same 1,392 files as the first zip; changed: `gtav_fast`, `librexruntime.dylib`, the iPhone
  shell's `gtav` and `librexruntime.dylib`, the iPhone `Info.plist` (build time), signatures.
  `android/GTAV.apk` unchanged.
- **New `ios-shell-v0.5.0.tar`** (draft, replaces the first one): Windows and Linux package with
  this one.
- Checked: game from the unzipped package, open world from a save, 52-55 fps hidden; taint scan
  PASS (same findings as the first zip); `GTAV --iphone` from the package: 811 of 811, 3,617
  shaders, 0 failed. That ipa's `gtav_game.dylib` and shaders are byte-identical to the first
  zip's; its runtime, GPU plugin and MoltenVK are identical (signatures and symbols removed) to
  `GTAV_mw_1127.ipa`, the test build run on the 13 mini, and its `gtav` differs from that one only
  by the version string (`v0.5.0-macos-2-g483ffbe` against `-dirty`).

## Linux package (2026-11-27 evening, on the draft)

`gtav-recomp-linux-x64.tar.gz`, built on the 4050 from **`max-perf` at 16c88af** (483ffbe's code:
the multi-wait refactor and the Apple wake-up of 8771bee; plus the packager without
`libTracyClient.so`, issue #18). 212,766,448 bytes, SHA-256
`1a404cee0db7f3886cea4f25e7cd4c29c7c6cf0bbcd1230eac97b105814765fb`; 811 files, 623 MB unpacked.

- Compat container build (`tools/release/compat/build.sh`). The first try failed to link `rexglue`:
  the SDK's static libraries in `out/linux-amd64/Release` (SDL3, glslang) had been built natively
  on 11-17 against glibc 2.39 (`__isoc23_strtol` and friends) and the container's ninja took them
  as current. Moved aside (`~/gtav-logs/native_a_1127`) and rebuilt in the container. Floors after:
  GLIBC 2.35 / GLIBCXX 3.4.32 for every shipped binary (XenosRecomp unchanged since 11-11).
- Packaged with `--compat --llvm build/llvm-19.1.5-rel` (official LLVM 19.1.5), `--ios
  dist/v0.5.0/ios` (the draft's `ios-shell-v0.5.0.tar`, `5c8eb910…`; the package's `ios/` is
  identical to it) and `--android dist/v0.5.0/android` (`GTAV.apk` 0856e2fc…, unchanged).
  `--licenses` needs a folder with `LLVM.txt`, `DirectXShaderCompiler.txt` and
  `third_party/GPL-3.0.txt` (`dist/v0.5.0/licenses`, made from the android LICENSES + the repo's
  LICENSE).
- Unattended install from the unpacked tarball (Toshiba, `gtav-install-0500`, disc images and TU27
  from `gtav-rex/game`, `GTAV_JOBS=4` on a busy machine): copy and check, codegen, "Generated code
  matches the reference build (811 files)", 407 files compiled, `gtav_game.so` linked, exit 0
  (42 minutes, 18:21-19:03).
- Boot from that install through the launcher, hidden and muted, A pressed for the first 4
  minutes (no save on a fresh install): the new game's prologue reached, picture checked on
  screenshots, 60-100 fps in the prologue on the RTX 4050 laptop while other work ran
  on it. Log: only the known stub and long-wait lines.
- Taint scan (`scan_release.sh`) of the tarball unpacked again, against the TU27 image,
  `default.xexp`, `update.rpf`, the generated code of this manifest (811/811 against
  `expected.txt`, copied from the Mac to `~/gtav-e2e-linux-cg/generated/default`), the repo's
  shader and census outputs, and the test install's `data/spirv` and `data/shader_cache`: **PASS,
  0 failing**. Short runs only: `rexglue` 112 B (codegen templates), `librexruntime.so` 72 B (a
  table of 16-bit counting numbers in `.lrodata`), `gtav_fast` 32 B (SPIR-V preamble),
  `clang++` 24 B, `gprlr_inline.h` 16 B. The APK inside is the one scanned unpacked on 11-23.
- Uploaded to the draft `v0.5.0` from the 4050, downloaded again on the Mac: byte-identical.

Left for 0.5.0: the Windows package (the 4050 rebooted into Windows: patch at 16c88af, SDK with
`rexgluerd`, `gtav_fast.exe`, launcher, `make_release.py` with the draft's `ios-shell` and
`android` tars, unattended install, `scan_release.cmd`), then the source archive and the final
notes (`RELEASE_NOTES_v0.5.0.md`).
