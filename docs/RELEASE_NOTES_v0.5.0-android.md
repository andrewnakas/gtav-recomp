> **Merged into [v0.5.0](https://github.com/andrewnakas/gtav-recomp/releases/tag/v0.5.0) on 2026-11-28**
> (owner: one release for all 0.5 packages). `GTAV.apk` there is the same file; this text is kept
> as the record of what this separate release said. Links below to the separate release pages
> no longer lead to downloads.

**GTA V on Android from your own Xbox 360 copy: the app now sets the game up on the phone itself.**

This release is the **Android app only**. The Windows, Linux, Mac and iPhone packages of 0.5.0 are
still being tested; until they are out, [v0.3.0](https://github.com/andrewnakas/gtav-recomp/releases/tag/v0.3.0)
is the release for those.

> **No game is included.** `GTAV.apk` holds no Rockstar Games or Microsoft code, data or shaders.
> You bring your own Xbox 360 discs and title update, and everything made from them is made on
> your own phone.

> [!WARNING]
> **What you build is for your devices only. Never share it.** Everything made from your discs is
> Rockstar Games' copyrighted work: the game files the app copies, the game library it builds and
> the shaders it translates. Don't upload them, send them to friends or post links, and don't put
> them in a fork or release. This also applies to discs and title-update files.
>
> **Making a fork or your own release?** The GPL covers only this project's code. It gives no one
> the right to distribute Rockstar's. Follow the [rules for forks and downstream releases](https://github.com/andrewnakas/gtav-recomp#keeping-it-clean-rules-for-forks-and-downstream-releases),
> and run the taint scan before you publish anything.

---

## What you need

- **Your own GTA V for Xbox 360:** Disc 1 (Install) and Disc 2 (Play) as ISO images, and
  **Title Update 27** (the file `tu00000009_00000000` from your console; only TU27 works).
- **An Android phone:** arm64, Android 11 or newer, Vulkan 1.1, Snapdragon 845 or newer.
  Developed and tested on a Galaxy S23 FE (Snapdragon 8 Gen 1); other phones are untested.
- **Space:** about 40 GB free while setting up from the disc images (they can be deleted
  afterwards), or about 16 GB if you copy the game files from a PC install instead (below).

## Install

1. Download **`GTAV.apk`** (below, under *Assets*) and install it. Android asks you to allow
   installing from your browser or file manager.
2. Copy the two disc images and the title update to the phone (USB or any other way).
3. Open **GTA V**, choose Disc 1, Disc 2 and the title update, and press **Install**. The app
   copies and checks the game files, recompiles the game and compiles it on the phone.
   **Keep the phone plugged in**: the build alone took 15 minutes on the S23 FE (slower phones
   take longer) and the phone gets warm. You can leave the app while it works.
4. The game starts when the build is done. Shaders are translated while you play, so the first
   minutes are slower.

**Already have the PC version installed?** Open the app once, then copy the `game`, `update` and
`user` folders from the PC install's `data` folder to
`Android/data/com.gtavrecomp.app/files/data` on the phone by USB, and press **Install** without
choosing any disc. The files are checked and only the game build runs.

**Updating from the 0.3.0 app:** install this APK over it and press **Install** once. Your game
files and saves stay; only the game is built again, on the phone. The old Android pack from the
PC is no longer used.

## Playing

- On-screen gamepad, or a Bluetooth / USB controller (the on-screen one hides itself).
- **Menu** (top of the screen, or RB + Back on a controller) opens the settings: *Best image*
  (30 fps with vsync: the phone stays cooler) and *Max performance* presets, frame rate limit,
  touch button size, language, bug report.
- To run setup again later (other discs, or to rebuild): long-press the app icon >
  *Run setup again*.

## New since 0.3.0 (Android)

- **The phone sets the game up by itself.** The app carries the recompiler and the compiler
  (codegen, clang, lld, XenosRecomp, dxc). No PC step and no game pack any more.
- **Much faster.** Galaxy S23 FE in the city: about 45-50 fps while the phone is cool and about
  30 once it is hot and the phone lowers its GPU clock (0.3.0: about 28 and 15). The recompiled
  game code keeps the Xbox registers in host registers and no longer takes a global lock for
  every interlocked operation; the renderer records a frame's draws on its own thread on the
  fastest core; waiting threads sleep instead of spinning, which leaves more of the heat budget
  to the GPU.
- **No 30 fps cap by default** (VSync stays on). The settings menu has VSync and a 30 / 60 fps
  limit; driving is uncapped too (*30 fps in vehicles* restores the Xbox handling).
- **Settings menu made for fingers:** large rows in two columns, a Close button, and a slider for
  the size of the on-screen buttons (they are larger by default as well).
- **Optional:** *Reflections every other frame* (settings > Advanced): 2-5% more fps, reflections
  on cars update at half the frame rate.
- **Setup from a PC install's files (#19)** instead of the disc images.
- **One-tap bug reports** from the settings menu.

## Fixed since 0.3.0

- **WASTED / BUSTED screen stuck above 30 fps (#21):** the death screen fades and you respawn at
  the hospital at any frame rate.
- **Cutscene dialogue stopping partway (#3, #4):** speech plays to the end.

## Known issues

- Sustained play heats the phone and the frame rate drops with its GPU clock; the *Best image*
  preset (30 fps) holds longer.
- Shadows and anti-aliasing differ from the 360 (#8, #29).
- Above 30 fps vehicles handle faster than on the Xbox (#12); *30 fps in vehicles* restores it.
- The open issues on GitHub list the rest.

## Source and checks

- Built from branch `max-perf` at 51d0fea (the tag `v0.5.0-android` adds documentation only).
  Complete corresponding source: `gtav-recomp-src-v0.5.0-android.tar.xz`.
- Checked on a Galaxy S23 FE: this APK installed over an existing install, the app recompiled
  the game on the phone (811 of 811 generated files match the reference build), compiled and
  linked it, and the game reached the open world.
- Taint scan of the unpacked APK against the game's executable, title update, translated
  shaders, generated code and a built game library: no game content. The only runs above the
  boilerplate limit are arm64 jump stubs (`.plt`, written by the linker) that the setup library
  and the shader translator share with the game library built for comparison.
- Signed with the same key as the 0.3.0 app (certificate SHA-256 `3b070344…b33231b1`).

## Checksums (SHA-256)

```
0856e2fc8cfebc610b1af0ed52d3f3f9fe7ce3180ee8184de58cb6b7adfbe615  GTAV.apk
```

The source archive's checksum is in the published release text (the archive contains this file).
