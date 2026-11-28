**GTA V on Windows, Linux and Mac, plus iPhone and Android, from your own Xbox 360 copy: 0.5.0.**

One download per computer. Each package installs the game from **your own** discs and Title
Update 27 and recompiles it on your computer. Each one can also build the **iPhone** app, and
each one carries the **Android** app.

| your computer | download |
|---|---|
| Windows 10/11 x64 | `gtav-recomp-windows-x64.zip` |
| Linux x64 (glibc 2.35 and GLIBCXX 3.4.32, e.g. Ubuntu 24.04) | `gtav-recomp-linux-x64.tar.gz` |
| Mac with Apple silicon, macOS 15+ | `gtav-recomp-macos-arm64.zip` (the same file as [v0.5.0-macos](https://github.com/andrewnakas/gtav-recomp/releases/tag/v0.5.0-macos)) |

The Android app is in every download (folder `android/`). It is also published on its own as
[v0.5.0-android](https://github.com/andrewnakas/gtav-recomp/releases/tag/v0.5.0-android) (the same APK).

> **No game is included.** No Rockstar Games or Microsoft code, data or shaders are in these
> downloads. You bring your own Xbox 360 discs and title update, and everything made from them is
> made on your own computer or phone.

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
- **Windows:** Windows 10/11 x64, a CPU with AVX2, a GPU with current Vulkan drivers.
- **Linux:** x64 with glibc 2.35 and GCC 13's libstdc++ or newer (Ubuntu 24.04, Debian 13,
  Fedora 39, Arch, openSUSE Tumbleweed; Ubuntu 22.04 with the toolchain PPA's libstdc++), a CPU
  with AVX2, Vulkan drivers, an X11 or Wayland desktop. Details in the package's `README.txt`.
- **Mac:** Apple silicon (M1 or newer), macOS 15 or newer.
- About **25 GB free** during setup (17 GB afterwards), 8 GB RAM. A controller is recommended;
  keyboard and mouse work.
- **iPhone (optional):** iPhone 12 or newer, iOS 17+, about 17 GB free.
- **Android (optional):** arm64, Android 11+, Vulkan 1.1, Snapdragon 845 or newer, about 40 GB
  free during setup.

## Install

- **Windows:** unzip, start `gtav_launcher.exe`, choose Disc 1, Disc 2 and the title update,
  press **Install** (15-30 minutes, once: it copies and checks your discs, recompiles the game
  with the included compiler and checks the result against the reference build), then **Play**.
- **Linux:** `tar xf gtav-recomp-linux-x64.tar.gz`, then open `gtav_launcher` (or
  `./gtav_launcher --setup DISC1 DISC2 TU` in a terminal). Same steps as on Windows.
- **Mac:** see the [v0.5.0-macos notes](https://github.com/andrewnakas/gtav-recomp/releases/tag/v0.5.0-macos).

**Updating from 0.3.0:** set up 0.5.0 in a new folder, then copy your old `data/user` folder
into it (that holds your saves).

## iPhone

1. On any of the three computers, after installing: **Build for iPhone** (Windows: hold Shift
   while starting `gtav_launcher.exe`; Linux: the button in the setup window or
   `./gtav_launcher --iphone`; Mac: hold Option while opening GTAV.app). About 15-25 minutes; the
   result is `iphone/GTAV.ipa`.
2. Install `GTAV.ipa` with **[Sideloadly](https://sideloadly.io)** or
   **[AltStore](https://altstore.io)**, signing with your Apple ID (a free Apple ID works, but you
   have to re-sign every 7 days).
3. Copy your **Disc 1 ISO**, **Disc 2 ISO** and **`tu00000009_00000000`** into GTA V's folder on
   the phone, open **GTA V**: its setup screen copies and checks about 16 GB, then the game starts.

## Android

Copy `android/GTAV.apk` from your download to the phone and install it, copy your disc images and
the title update there, open GTA V and press **Install**: the app recompiles and builds the game
on the phone by itself (no computer step). Details in the
[v0.5.0-android notes](https://github.com/andrewnakas/gtav-recomp/releases/tag/v0.5.0-android).

## New since 0.3.0

- **Uncapped by default** (no frame cap and no vsync out of the box; the settings menu still has
  VSync and a 30/60 fps limit; driving is uncapped too, *30 fps in vehicles* restores the Xbox
  handling). The recompiled game code keeps more of the Xbox registers in host registers; the
  renderer only binds the textures the shaders sample and converts each vertex snapshot once.
- **Linux, Mac, iPhone and Android:** waiting threads sleep and wake as soon as what they wait
  for happens instead of checking every millisecond, and interlocked operations no longer take a
  global lock (Windows keeps its 0.3.0 behaviour for these two).
- **iPhone, much faster and without the flicker.** iPhone 13 mini: 44-45 fps standing in light
  scenes, 37-41 fps walking while the phone is cool, about 25-30 once it is hot (0.3.0: about 30
  in the city with a 30 fps cap). While new shaders compile the last complete picture stays up.
- **Android sets itself up on the phone:** no Android pack from the computer any more.
- **PC keyboard and mouse layout** following the PC game's defaults, with every action
  remappable (settings menu > Controls; controller buttons too).
- **Settings menu (#6):** F1 with keyboard and mouse (Esc otherwise, RB+Back on a controller):
  render scale, fullscreen, language (the game's 12 languages; speech stays English), controls.
- **Fullscreen (#8):** F11 or Alt+Enter.
- **One-click bug reports** from the settings menu, and a crash report offered on the next start.

## Fixed since 0.3.0

- **WASTED / BUSTED screen stuck above 30 fps (#21):** the death screen fades and you respawn at
  the hospital at any frame rate.
- **Mission Failed crash in the prologue on Windows (#3).**
- **Cutscene dialogue stopping partway (#3, #4):** speech plays to the end.
- **Keyboard didn't work (#11):** keyboard and mouse reach the game on every desktop, including
  the Display Calibration screen (Enter = accept, Backspace = back).
- **Black world on the first launch** while shaders were still being built.
- **Antivirus warning on the Windows package (#18):** the profiler library is no longer shipped
  (on any platform).
- **Linux:** guest `Sleep(0)` slept 100 µs instead of yielding; long boot crawls from
  missing-file probes on slow disks are gone.
- **Mac:** the game fills Retina windows (#14), a grey-world setting is off by default (#13),
  fullscreen no longer toggles twice (#49). Thanks to their contributors.

## Performance

| | |
|---|---|
| Windows | _(filled in after the Windows checks)_ |
| Linux, RTX 4050 laptop | 60-100 fps in the prologue, from a fresh install |
| MacBook Air M1 | 45-57 fps in the open world |
| iPhone 13 mini | 44-45 fps standing, 37-41 walking while cool; 25-30 once hot |
| Galaxy S23 FE | about 45-50 fps while cool, about 30 once hot |

## Known issues

- Shadows and anti-aliasing differ from the 360 (#29).
- Phones heat up in sustained play and the frame rate drops with the GPU clock.
- The first minutes after installing are slower while shaders are translated; a new area can hold
  the picture for a moment the first time.
- The open issues on GitHub list the rest. Logs are in `data/logs`; please attach them to an issue.

## Source and checks

- Built from branch `max-perf`: Linux at 16c88af with the player README of a17e4f4,
  Windows at _(commit)_, Mac at 483ffbe. Complete corresponding source:
  `gtav-recomp-src-v0.5.0.tar.xz` (this project, the ReXGlue SDK with every submodule,
  XenosRecomp and plume at the commits the binaries were built from; our changes are the
  `*.patch` files).
- Every package carries the same `ios/` folder (the iPhone shell, built at 483ffbe) and the same
  `android/GTAV.apk` (SHA-256 `0856e2fc…dfbe615`).
- **Linux:** unattended install from the unpacked tarball and the discs: 811 of 811 generated files
  match the reference build, compiled and linked; the game started through the launcher and
  reached the prologue (hidden window, sound muted, screenshots checked). Every program needs at
  most glibc 2.35 and GLIBCXX 3.4.32. Taint scan of the unpacked tarball against the game's
  executable image, title update, generated code and translated shaders: **PASS, 0 failing**
  (only short runs: recompiler code templates up to 112 bytes, a table of counting numbers,
  SPIR-V boilerplate).
- **Windows:** _(filled in after the Windows checks)_
- **Mac:** see the [v0.5.0-macos notes](https://github.com/andrewnakas/gtav-recomp/releases/tag/v0.5.0-macos).

## Checksums (SHA-256)

```
_(filled in at final assembly)_
```
