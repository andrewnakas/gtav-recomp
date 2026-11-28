> **Merged into [v0.5.0](https://github.com/andrewnakas/gtav-recomp/releases/tag/v0.5.0) on 2026-11-28**
> (owner: one release for all 0.5 packages). `gtav-recomp-macos-arm64.zip` there is the same file; this text is kept
> as the record of what this separate release said. Links below to the separate release pages
> no longer lead to downloads.

**GTA V on a Mac, iPhone and Android, from your own Xbox 360 copy: 0.5.0 for macOS.**

This release is the **Mac package**: one download that installs the game on your Mac and also
builds the **iPhone** app, and carries the **Android** app. The Windows and Linux packages of
0.5.0 come later; until then [v0.3.0](https://github.com/andrewnakas/gtav-recomp/releases/tag/v0.3.0)
is the release for those. The Android app alone is also
[v0.5.0-android](https://github.com/andrewnakas/gtav-recomp/releases/tag/v0.5.0-android) (the same APK).

> **Updated 2026-11-27 evening:** the files were rebuilt with two changes for the Mac and the
> iPhone: threads waiting on several events now wake as soon as one fires, instead of checking
> every millisecond, and the Mac now uses the waiting settings the iPhone, Linux and Android
> already had. If you downloaded the zip earlier (SHA-256 `04e2be3e…cb9a3`), it still works;
> the new one is a little smoother. Nothing else changed.

> **No game is included.** No Rockstar Games or Microsoft code, data or shaders are in this
> download. You bring your own Xbox 360 discs and title update, and everything made from them is
> made on your own Mac or phone.

> [!WARNING]
> **What you build is for your devices only. Never share it.** Everything made from your discs is
> Rockstar Games' copyrighted work: the game files the installer copies, the game library it
> builds, the shaders it translates, and the `GTAV.ipa` that *Build for iPhone* makes. Don't
> upload them, send them to friends or post links, and don't put them in a fork or release. This
> also applies to discs and title-update files.
>
> **Making a fork or your own release?** The GPL covers only this project's code. It gives no one
> the right to distribute Rockstar's. Follow the [rules for forks and downstream releases](https://github.com/andrewnakas/gtav-recomp#keeping-it-clean-rules-for-forks-and-downstream-releases),
> and run the taint scan before you publish anything.

---

## What you need

- **Your own GTA V for Xbox 360:** Disc 1 (Install) and Disc 2 (Play) as ISO images, and
  **Title Update 27** (the file `tu00000009_00000000` from your console; only TU27 works).
- **A Mac with Apple silicon** (M1 or newer), **macOS 15** or newer, 8 GB RAM, about **25 GB
  free** during setup (17 GB afterwards). A controller is recommended; keyboard and mouse work.
- **iPhone (optional):** iPhone 12 or newer, iOS 17+, about 17 GB free.
- **Android (optional):** arm64, Android 11+, Vulkan 1.1, Snapdragon 845 or newer.

## Install on the Mac

1. Download **`gtav-recomp-macos-arm64.zip`** (below, under *Assets*), unzip it and move
   **`GTAV.app`** to Applications.
2. Open it once. The app isn't notarized, so then go to **System Settings → Privacy & Security →
   Open Anyway**. Or run `xattr -dr com.apple.quarantine /Applications/GTAV.app`.
3. Choose Disc 1, Disc 2, the title update and a game folder, press **Install** (15-30 minutes,
   once: it copies and checks your discs, recompiles the game with the included compiler and
   checks the result against the reference build), then **Play**.

**Updating from 0.3.0:** set up 0.5.0 in a new game folder, then copy your old `data/user`
folder into it (that holds your saves).

## iPhone

1. Hold **Option** while opening GTAV.app and press **Build for iPhone** (about 15-25 minutes).
   The result is `iphone/GTAV.ipa` in your game folder.
2. Install `GTAV.ipa` with **[Sideloadly](https://sideloadly.io)** or
   **[AltStore](https://altstore.io)**, signing with your Apple ID (a free Apple ID works, but
   you have to re-sign every 7 days).
3. Copy your **Disc 1 ISO**, **Disc 2 ISO** and **`tu00000009_00000000`** into GTA V's folder on
   the phone (Finder → your iPhone → Files → GTA V, or the Files app), open **GTA V**: its setup
   screen copies and checks about 16 GB, then the game starts.

**Updating from the 0.3.0 iPhone app:** build the new `GTAV.ipa` and install it over the old
app, signed with the same Apple ID: the game files and saves already on the phone stay (checked
with a developer install over the previous build, not with Sideloadly or AltStore).

## Android

The package's `android/` folder holds **`GTAV.apk`** (the same app as `v0.5.0-android`). Install
it on the phone; the app copies your discs and builds the game on the phone by itself (no Mac
step). Its steps are in the [v0.5.0-android notes](https://github.com/andrewnakas/gtav-recomp/releases/tag/v0.5.0-android).

## New since 0.3.0

- **Uncapped by default** (no frame cap and no vsync out of the box; the settings menu still has
  VSync and a 30/60 fps limit). The recompiled game code keeps more of the Xbox registers in host
  registers; the renderer only binds the textures the shaders sample.
- **iPhone, much faster and without the flicker.** iPhone 13 mini: 44-45 fps standing in light
  scenes, 37-41 fps walking while the phone is cool, about 25-30 once it is hot (0.3.0: about 30
  in the city with a 30 fps cap, about 20 in the heaviest scenes). The game code skips a
  floating-point mode switch, interlocked operations no longer take a global lock, waiting threads
  sleep and wake as soon as what they wait for happens, and textures are decoded on the GPU where the phone can. While new shaders compile the last complete picture stays up instead of a frame
  with objects missing ("Preparing shaders" at the start). The memory spikes that could get the
  app closed by iOS during long sessions are fixed. *Best image* in the settings menu is the
  steady 30 fps with vsync, which keeps the phone cooler.
- **Android sets itself up on the phone** (see [v0.5.0-android](https://github.com/andrewnakas/gtav-recomp/releases/tag/v0.5.0-android)):
  no Android pack from the Mac any more.
- **PC keyboard and mouse layout** following the PC game's defaults, with every action
  remappable (settings menu > Controls; controller buttons too).
- **Settings menu (#6):** F1 with keyboard and mouse (Esc otherwise, RB+Back on a controller):
  render scale, fullscreen, language (the game's 12 languages; speech stays English), controls.
- **Fullscreen (#8):** F11 or Alt+Enter.
- **One-click bug reports** from the settings menu.
- **Mac:** threads waiting on several events wake as soon as one fires (8% less renderer CPU time
  a frame on an M1 Air), and the game's waits for the GPU sleep instead of spinning (about half a
  core less).

## Fixed since 0.3.0

- **WASTED / BUSTED screen stuck above 30 fps (#21):** the death screen fades and you respawn at
  the hospital at any frame rate; on the Mac, the screen no longer turns to blue noise after it.
- **Cutscene dialogue stopping partway (#3, #4):** speech plays to the end.
- **Black world on the first launch** while shaders were still being built.
- **The game fills Retina windows** instead of a corner (#14), and a grey-world setting is off by
  default (#13). Thanks to the contributor of #13 and #14.
- **Fullscreen toggled twice** with Alt+Enter / F11 (#49). Thanks to its contributor.

## Performance

| | |
|---|---|
| MacBook Air M1 | 45-57 fps in the open world |
| iPhone 13 mini | 44-45 fps standing, 37-41 walking while cool; 25-30 once hot |
| Galaxy S23 FE | about 45-50 fps while cool, about 30 once hot |

## Known issues

- Phones heat up in sustained play and the frame rate drops with the GPU clock.
- The first minutes after installing are slower on phones while their shaders compile; a new
  area can hold the picture for a moment the first time.
- Shadows and anti-aliasing differ from the 360 (#29).
- The open issues on GitHub list the rest. Logs are in `data/logs` (on phones: the app's `logs`
  folder); please attach them to an issue.

## Source and checks

- Built from branch `max-perf` at 483ffbe (the tag `v0.5.0-macos` adds documentation only), on
  an M1 MacBook Air with macOS 15 as the target. Complete corresponding source:
  `gtav-recomp-src-v0.5.0-macos.tar.xz` (this project, the ReXGlue SDK with every submodule,
  XenosRecomp and plume at the commits the binaries were built from; our changes are the
  `*.patch` files).
- The package's `ios/` folder (the iPhone shell) and `android/GTAV.apk` (SHA-256
  `0856e2fc…dfbe615`, the `v0.5.0-android` app) are the ones the Windows and Linux packages of
  0.5.0 will carry.
- Mac: the game started from the unzipped package and reached the open world from a save
  (52-55 fps, hidden window, sound muted, screenshots checked). The installer's Mac path is
  unchanged since the 11-26 build, whose unattended install from the discs passed (811 of 811
  generated files match the reference build, compile, link, open world). The new wake-up,
  switched on and off every 60 frames in one run (16 report pairs): 58.1 -> 58.6 fps, slowest
  10% of frames 20.1 -> 19.4 ms, renderer CPU 6.84 -> 6.28 ms a frame.
- iPhone: `GTAV --iphone` from the unzipped package built the app (811 of 811 generated files
  match the reference build, 3,617 shaders translated, 0 failed). Its game library and shaders are
  byte-identical to the first 0.5.0 build's, and its shell and runtime match, code for code, the
  test build that ran on an iPhone 13 mini this evening: started over the previous build with the
  saves kept, and an 8-minute run with a scripted walk, no crash. Two clean same-run pairs
  while walking with the wake-up off and on: 24.8 -> 26.7 and 29.7 -> 33.9 fps.
- iPhone shell code before the wake-up: a 40-minute scripted walk on an iPhone 13 mini (no memory
  warning, not closed, at least 879 MB of memory left throughout).
- Taint scan of the unpacked package against the game's executable image, title update,
  generated code and translated shaders: **PASS, 0 failing**. The only matches are short
  boilerplate runs (up to 112 bytes, in the recompiler's code templates), the same as in the
  scans of the earlier 0.5.0 test packages.

## Checksums (SHA-256)

```
1af843a4f5ac5cab9d50181ba98414534d9b9c5548bfb63fd8e26468476f2cd6  gtav-recomp-macos-arm64.zip
0856e2fc8cfebc610b1af0ed52d3f3f9fe7ce3180ee8184de58cb6b7adfbe615  GTAV.app/Contents/Resources/gtav/android/GTAV.apk (inside the zip)
```

The source archive's checksum is in the published release text (the archive contains this file).
