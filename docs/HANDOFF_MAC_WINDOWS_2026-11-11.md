# Handoff: test branch `issue-fixes` on Mac and Windows, then ship v0.4.0

Written 2026-11-11 on Linux. For the Claude sessions on the owner's Mac and Windows PC.
**Read all of it first.** The build steps for the release itself are in `docs/RELEASE_v0.4.0.md`;
the release text is drafted in `docs/RELEASE_NOTES_v0.4.0.md`.

## Rules (owner's, unchanged)

- Push to `issue-fixes` only. **Never push to `main`, never tag, never create or publish a GitHub
  release** until the owner says Mac and Windows pass and says to ship.
- Issue replies are fine; say "fixed on branch issue-fixes, in the next release".
- Hidden/automated game runs must be muted (`--audio_mute=true`); warn before killing a game the
  owner is playing.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Where things stand

- `issue-fixes` = `main` (2a76184) + 41 commits, version **0.4.0** (d8e21bf: Android versionCode 4,
  Mac app 0.4.0 build 4, iPhone 0.4.0). `main` has not moved, so shipping is a fast-forward.
- **Linux: PASSED** (release package, unattended install, 811/811 generated files match
  `tools/release/expected.txt`, world in 50 s, dying respawns at the hospital, lit screenshots).
- **Android: PASSED** on the S23 FE (APK + pack from the Linux package's Build for Android: open
  world in ~45 s, WASTED -> hospital, touch controls, the new copied-files setup, clean log).
- **Mac, iPhone, Windows: NOT tested.** Nothing on this branch was compiled there yet.
- `rexglue-sdk-gtav-linux.patch` matches the Linux SDK tree exactly (64 files; it grew ~1,400 lines
  since v0.3.0: XMA dialogue fix, input forwarding, script-thread hooks). Mac and Windows get every
  SDK change only through it, so **re-apply it** (Windows uses this same patch, as for v0.3.0;
  `rexglue-sdk-gtav-windows.patch` is old and unused).

## Setup on each machine

1. `git fetch && git checkout issue-fixes && git pull` (at d8e21bf or later).
2. Reset the SDK, XenosRecomp and plume trees and apply `rexglue-sdk-gtav-linux.patch`,
   `xenosrecomp-gtav.patch`, `plume-gtav.patch` (and `sdl3-gtav.patch`, `ffmpeg-gtav.patch` to those
   submodules) as for v0.3.0. Rebuild the codegen tool and SDK runtime, then the game and launcher.
3. Codegen check: 811/811 against `tools/release/expected.txt` (it did not change since v0.3.0, the
   recompiled code is the same; new behaviour is in hooks in `gtav/src`).

New source files the build must pick up (already in `gtav/CMakeLists.txt`):
`gtav/src/crash_guards.cpp`, `gtav/src/script_timer_fix.cpp`.

## What to test

Use the developer build first (fast to fix), then the release package (that is what ships). For
each line: pass/fail + one sentence. A failure: fix on `issue-fixes`, push, write it under
"Results" below.

### Both Mac and Windows

| # | test | how | pass |
|---|---|---|---|
| 1 | Build | developer build + codegen | compiles, 811/811 |
| 2 | Boot | new game folder or existing save | reaches the world, lit, no black world (d44d006) |
| 3 | **WASTED (#21)** | die (jump off something, or `--gtav_debug_commands=true` and write `kill` into `<game folder>/logs/native_cmd`) | WASTED fades, player respawns at a hospital within ~15 s |
| 4 | Above 30 fps | uncapped, look at the fps | #21 fix holds at 60+ fps; cars feel like the 360 (fab5c28 runs vehicles at 30) |
| 5 | Keyboard + mouse (#11) | first-boot Display Calibration screen, then walk/drive/shoot | Enter accepts, Backspace back, WASD+mouse play; F1 = settings menu |
| 6 | Settings menu (#6) | F1: Resolution (render scale), Fullscreen, Language, Controls | each applies; Controls remap is saved (controls.ini) |
| 7 | Fullscreen (#8) | F11 / Alt+Enter / menu | toggles both ways, mouse still works |
| 8 | Cutscene dialogue (#3, #4) | watch the first cutscenes after the prologue (Franklin and Lamar) | speech plays to the end, no stall |
| 9 | Bug report | settings menu > bug report | file written, names the shader status |
| 10 | Build for iPhone / Android | from the **release package**: `--iphone`, `--android` | 811/811, 0 failed shaders |

### Windows only

| # | test | pass |
|---|---|---|
| W1 | Mission Failed in the prologue (#3 crash) | fail the prologue on purpose (e.g. die): no crash; if `[guard] sub_8281E5F0` appears in the log, the guard caught it (that's the fix working) |
| W2 | No Tracy DLL (#18) | package has no `TracyClient*.dll`; Defender quiet on the zip |
| W3 | VC++ runtime | launcher, gtav_fast, XenosRecomp, dxc start on a PC without the redistributable (as 0.3.0) |
| W4 | `debug find/words` compile fix (8b4ddc0) | the build itself |

### Mac only

| # | test | pass |
|---|---|---|
| M1 | Retina window (PR #14 / d670365) | game fills the window, not cropped to a quarter, on a Retina display |
| M2 | Input attachment resolves off (PR #13 / 97db325, 63a9a1c) | no grey world or regressions vs 0.3.0 |
| M3 | **iPhone shell compiles** | b2276fa fixed Android's shell build (desktop-only SDK window forwarding); the iPhone shell had the same gap and was never compiled with the fix: build `build/release-ios-shell`, then `tools/ios/make_shell.sh` |
| M4 | iPhone | ipa from the package's `--iphone` on the iPhone 13 mini: world loads, WASTED respawns |
| M5 | keyboard at Display Calibration (#11) | Enter/arrows work (the reporter is on macOS) |

## Results (fill in)

| machine | commit | result | notes |
|---|---|---|---|
| Linux | d8e21bf | PASS | release package + Android on S23 FE |
| Mac | | | |
| iPhone | | | |
| Windows | | | |

## When both pass

Tell the owner. On their go: follow `docs/RELEASE_v0.4.0.md` (draft release, each machine uploads
its package, final checks, owner publishes). If a machine needed fixes, the release commit is the
last fix commit and the Linux/Android packages are rebuilt from it (Linux session, ~1 h).

## Open issues not in this release (for after)

New since 11-08: #22 crash pausing the first cutscene on first boot, #23 pause-screen player icon,
#24 decals with the wrong texture, #25 black screen on startup (Windows 11, persists after reinstall:
ask for logs/crash.txt; may be d44d006's first-launch fix), #26 water horizon, #27 text loads slowly
the first time, #28 headlights through objects, #29 no AA (4x MSAA aliased into 1x targets; render
scale above 1.0 blocked). Older: #2 (feature request), #5 (OpenGL), #7, #9 (needs the reporter's
save), #12 (physics/flags tied to frame rate above 30), #17/#20 (stretched textures, not reproduced),
#10 (Steam Deck black screen). PR #16 (45 fps Mac preset, draft, author on hold), PR #1 (untested
workflow): owner decides.
