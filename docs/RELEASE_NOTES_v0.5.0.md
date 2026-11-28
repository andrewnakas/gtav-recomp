**GTA V on Linux and Mac, plus iPhone and Android, from your own Xbox 360 copy: 0.5.0.**

One download per device. Each one sets the game up from **your own** discs and Title Update 27
and recompiles it on your computer or phone.

| your device | download (below, under *Assets*) |
|---|---|
| Linux x64 (glibc 2.35 and GLIBCXX 3.4.32, e.g. Ubuntu 24.04) | `gtav-recomp-linux-x64.tar.gz` |
| Mac with Apple silicon, macOS 15+ | `gtav-recomp-macos-arm64.zip` |
| Android phone | `GTAV.apk` (also in the `android/` folder of both packages) |
| iPhone | built by the Linux or the Mac package (*Build for iPhone*) |
| Windows | the 0.5.0 package is not out yet: use [v0.3.0](https://github.com/andrewnakas/gtav-recomp/releases/tag/v0.3.0) until it is added here |

This release replaces the separate `v0.5.0-android` and `v0.5.0-macos` releases. `GTAV.apk` and
the Mac zip are the same files that were published there (checksums at the end).

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
- **Linux:** x64 with glibc 2.35 and GCC 13's libstdc++ or newer (Ubuntu 24.04, Debian 13,
  Fedora 39, Arch, openSUSE Tumbleweed; Ubuntu 22.04 with the toolchain PPA's libstdc++), a CPU
  with AVX2, Vulkan drivers, an X11 or Wayland desktop. Details in the package's `README.txt`.
- **Mac:** Apple silicon (M1 or newer), macOS 15 or newer.
- Both computers: about **25 GB free** during setup (17 GB afterwards), 8 GB RAM. A controller is
  recommended; keyboard and mouse work.
- **iPhone (optional):** iPhone 12 or newer, iOS 17+, about 17 GB free, and one of the two
  computers above to build the app.
- **Android:** arm64, Android 11 or newer, Vulkan 1.1, Snapdragon 845 or newer; about 40 GB free
  while setting up from the disc images (they can be deleted afterwards), or about 16 GB if you
  copy the game files from a computer's install instead. Developed and tested on a Galaxy S23 FE
  (Snapdragon 8 Gen 1); other phones are untested. No computer is needed.

## Install on Linux

`tar xf gtav-recomp-linux-x64.tar.gz`, then open `gtav_launcher` (or run
`./gtav_launcher --setup DISC1 DISC2 TU` in a terminal). Choose Disc 1, Disc 2 and the title
update and press **Install** (15-45 minutes, once: it copies and checks your discs, recompiles
the game with the included compiler and checks the result against the reference build), then
**Play**.

## Install on a Mac

1. Unzip `gtav-recomp-macos-arm64.zip` and move **`GTAV.app`** to Applications.
2. Open it once. The app isn't notarized, so then go to **System Settings → Privacy & Security →
   Open Anyway**. Or run `xattr -dr com.apple.quarantine /Applications/GTAV.app`.
3. Choose Disc 1, Disc 2, the title update and a game folder, press **Install** (15-30 minutes,
   once), then **Play**.

**Updating a computer from 0.3.0:** set up 0.5.0 in a new folder, then copy your old `data/user`
folder into it (that holds your saves).

## iPhone

1. On the computer, after installing: **Build for iPhone** (Linux: the button in the setup window
   or `./gtav_launcher --iphone`; Mac: hold Option while opening GTAV.app). About 15-25 minutes;
   the result is `iphone/GTAV.ipa` in your game folder.
2. Install `GTAV.ipa` with **[Sideloadly](https://sideloadly.io)** or
   **[AltStore](https://altstore.io)**, signing with your Apple ID (a free Apple ID works, but you
   have to re-sign every 7 days).
3. Copy your **Disc 1 ISO**, **Disc 2 ISO** and **`tu00000009_00000000`** into GTA V's folder on
   the phone (Finder → your iPhone → Files → GTA V, or the Files app), open **GTA V**: its setup
   screen copies and checks about 16 GB, then the game starts.

**Updating from the 0.3.0 iPhone app:** build the new `GTAV.ipa` and install it over the old
app, signed with the same Apple ID: the game files and saves already on the phone stay (checked
with a developer install over the previous build, not with Sideloadly or AltStore).

## Android

1. Download **`GTAV.apk`** and install it. Android asks you to allow installing from your browser
   or file manager.
2. Copy the two disc images and the title update to the phone (USB or any other way).
3. Open **GTA V**, choose Disc 1, Disc 2 and the title update, and press **Install**. The app
   copies and checks the game files, recompiles the game and compiles it on the phone.
   **Keep the phone plugged in**: the build alone took 15 minutes on the S23 FE (slower phones
   take longer) and the phone gets warm. You can leave the app while it works.
4. The game starts when the build is done. Shaders are translated while you play, so the first
   minutes are slower.

**Already have the game installed on a computer?** Open the app once, then copy the `game`,
`update` and `user` folders from the computer install's `data` folder to
`Android/data/com.gtavrecomp.app/files/data` on the phone by USB, and press **Install** without
choosing any disc. The files are checked and only the game build runs.

**Updating from the 0.3.0 app:** install this APK over it and press **Install** once. Your game
files and saves stay; only the game is built again, on the phone. The old Android pack from the
computer is no longer used.

**Playing on the phone:** on-screen gamepad, or a Bluetooth / USB controller (the on-screen one
hides itself). **Menu** (top of the screen, or RB + Back on a controller) opens the settings:
*Best image* (30 fps with vsync: the phone stays cooler) and *Max performance* presets, frame
rate limit, touch button size, language, bug report. To run setup again later: long-press the app
icon > *Run setup again*.

## New since 0.3.0

- **No 30 fps cap by default.** Computers and the iPhone run with no frame cap and no vsync out
  of the box, Android with no cap and vsync on. The settings menu has VSync and a 30 / 60 fps
  limit; driving is uncapped too (*30 fps in vehicles* restores the Xbox handling).
- **Faster game code and renderer.** The recompiled game code keeps more of the Xbox registers
  in host registers, interlocked operations no longer take a global lock, and waiting threads
  sleep and wake as soon as what they wait for happens instead of checking every millisecond.
  The renderer records a frame's draws on its own threads, only binds the textures the shaders
  sample and converts each vertex snapshot once.
- **Android sets the game up by itself.** The app carries the recompiler and the compiler: no
  computer step and no Android pack any more. Galaxy S23 FE in the city: about 45-50 fps while
  the phone is cool and about 30 once it is hot (0.3.0: about 28 and 15).
- **iPhone, much faster and without the flicker.** iPhone 13 mini: 44-45 fps standing in light
  scenes, 37-41 fps walking while the phone is cool, about 25-30 once it is hot (0.3.0: about 30
  in the city with a 30 fps cap, about 20 in the heaviest scenes). While new shaders compile the
  last complete picture stays up instead of a frame with objects missing. The memory spikes that
  could get the app closed by iOS during long sessions are fixed.
- **Phone settings menu made for fingers:** large rows in two columns, a Close button and a
  slider for the size of the on-screen buttons (checked on Android; the iPhone uses the same
  layout). On Android the on-screen buttons are larger by default as well.
- **PC keyboard and mouse layout** following the PC game's defaults, with every action
  remappable (settings menu > Controls; controller buttons too).
- **Settings menu (#6):** F1 with keyboard and mouse (Esc otherwise, RB+Back on a controller):
  render scale, fullscreen, language (the game's 12 languages; speech stays English), controls.
- **Fullscreen (#8):** F11 or Alt+Enter.
- **One-click bug reports** from the settings menu; on computers a report is also made on the
  next start after a crash.
- **Android, optional:** *Reflections every other frame* (settings > Advanced): 2-5% more fps,
  reflections on cars update at half the frame rate.
- **Android: setup from a computer install's files (#19)** instead of the disc images.

## Fixed since 0.3.0

- **WASTED / BUSTED screen stuck above 30 fps (#21):** the death screen fades and you respawn at
  the hospital at any frame rate; on the Mac, the screen no longer turns to blue noise after it.
- **Cutscene dialogue stopping partway (#3, #4):** speech plays to the end.
- **Keyboard didn't work (#11):** keyboard and mouse reach the game on computers, including the
  Display Calibration screen (Enter = accept, Backspace = back).
- **Black world on the first launch** on computers while shaders were still being built.
- **Linux:** guest `Sleep(0)` slept 100 µs instead of yielding; long boot crawls from
  missing-file probes on slow disks are gone. The profiler library is no longer shipped (#18).
- **Mac:** the game fills Retina windows (#14), a grey-world setting is off by default (#13),
  fullscreen no longer toggles twice (#49). Thanks to their contributors.

## Performance

| | |
|---|---|
| Linux, RTX 4050 laptop | 60-100 fps in the prologue, around 90 in the open world (laptop busy with other work) |
| MacBook Air M1 | 52-55 fps in the open world |
| iPhone 13 mini | 44-45 fps standing, 37-41 walking while cool; 25-30 once hot |
| Galaxy S23 FE | about 45-50 fps while cool, about 30 once hot |

## Known issues

- Shadows and anti-aliasing differ from the 360 (#8, #29).
- Above 30 fps vehicles handle faster than on the Xbox (#12); *30 fps in vehicles* restores it.
- Phones heat up in sustained play and the frame rate drops with the GPU clock; the *Best image*
  preset (30 fps) holds longer.
- The first minutes after installing are slower while shaders are translated; a new area can hold
  the picture for a moment the first time.
- **Android, Galaxy S22 with Android 14 (#48):** setup compiles the game and then fails at
  *Linking* ("Pointer tag was truncated"). Not fixed in this release.
- **Android, phones with a Mali GPU (#59):** the game is built but does not start on a Redmi
  Note 14 Pro 5G (Mali-G615): its Vulkan driver lacks a feature the renderer asks for. Only
  Adreno (Snapdragon) phones are known to work.
- The Mac package's `README.txt` still describes the old Android pack from the computer; the
  Android steps above are the right ones.
- The open issues on GitHub list the rest. Logs are in `data/logs` (on phones: the app's `logs`
  folder); please attach them to an issue.

## Source and checks

- Built from branch `max-perf`. Linux and Mac: 483ffbe's code (the Linux tarball at 16c88af with
  the player README of a17e4f4, the Mac zip at 483ffbe; the commits in between and the tag
  `v0.5.0` change only the Linux packager, the READMEs and documentation). Complete corresponding
  source: `gtav-recomp-src-v0.5.0.tar.xz` (this project, the ReXGlue SDK with every submodule,
  XenosRecomp and plume at the commits the binaries were built from; our changes are the
  `*.patch` files).
- `GTAV.apk` was built earlier, at 51d0fea. Its complete corresponding source is
  `gtav-recomp-src-v0.5.0-android.tar.xz`.
- Both packages carry the same `ios/` folder (the iPhone shell, built at 483ffbe) and the same
  `android/GTAV.apk` as the one under *Assets*.
- **Linux:** unattended install from the unpacked tarball and the discs: 811 of 811 generated files
  match the reference build, compiled and linked; the game started through the launcher and
  reached the prologue (hidden window, sound muted, screenshots checked). With the `data/user`
  folder of an older install copied in, it loaded the save and ran in the open world (8 minutes,
  screenshots checked, no crash). Every program needs at most glibc 2.35 and GLIBCXX 3.4.32.
  `./gtav_launcher --iphone` from that install built
  `GTAV.ipa` in 24 minutes: 811 of 811 generated files match, 3,617 shaders translated, 0 failed;
  everything the game library imports is provided by the iPhone shell. That ipa was not run on
  a phone (the one built by the Mac package was).
- **Mac:** the game started from the unzipped package and reached the open world from a save
  (52-55 fps, hidden window, sound muted, screenshots checked). The installer's Mac path is
  unchanged since the 11-26 build, whose unattended install from the discs passed (811 of 811,
  compile, link, open world).
- **iPhone:** `GTAV --iphone` from the unzipped Mac package built the app (811 of 811 generated
  files match the reference build, 3,617 shaders translated, 0 failed). Its shell and runtime
  match, code for code, the test build that ran on an iPhone 13 mini: started over the previous
  build with the saves kept, and an 8-minute run with a scripted walk, no crash.
- **Android:** the APK installed over an existing install on a Galaxy S23 FE, the app recompiled
  the game on the phone (811 of 811), compiled and linked it, and the game reached the open
  world. Signed with the same key as the 0.3.0 app (certificate SHA-256 `3b070344…b33231b1`).
- **Taint scans** against the game's executable image, title update, generated code and
  translated shaders: the unpacked Linux tarball, the unpacked Mac package and the unpacked APK
  each **PASS, 0 failing**. The only matches are short boilerplate runs (recompiler code
  templates up to 112 bytes, a table of counting numbers, SPIR-V preambles).

## Checksums (SHA-256)

```
ee5c82779917a92f069dadfbdc888f7466170f978b3c853f19ec4218c77d9ed4  gtav-recomp-linux-x64.tar.gz
1af843a4f5ac5cab9d50181ba98414534d9b9c5548bfb63fd8e26468476f2cd6  gtav-recomp-macos-arm64.zip
0856e2fc8cfebc610b1af0ed52d3f3f9fe7ce3180ee8184de58cb6b7adfbe615  GTAV.apk
```

The source archives' checksums are in the published release text (the archive contains this file).
