# GTA V Xbox 360 recompilation v0.4.0 (draft release text)

One download per computer. Each package installs the game from **your own** discs and Title
Update 27, recompiles it on your machine, and can also build the iPhone app and the Android game
pack. No game files are included.

| your computer | download |
|---|---|
| Windows 10/11 x64 | `gtav-recomp-windows-x64.zip` |
| Linux x64 (glibc 2.35+, e.g. Ubuntu 22.04+) | `gtav-recomp-linux-x64.tar.gz` |
| Mac with Apple silicon, macOS 15+ | `gtav-recomp-macos-arm64.zip` |

**Updating from 0.3.0:** set up the new version in a new folder, then copy your old `data/user`
folder into it (that holds your saves). Phones: install the new APK / ipa over the old one and
choose the new game pack (Android: long-press the app icon > Run setup again).

## Fixed

- **WASTED / BUSTED screen stuck above 30 fps (#21):** the death screen now fades and you respawn
  at the hospital at any frame rate.
- **Mission Failed crash in the prologue on Windows (#3).**
- **Cutscene dialogue stopping partway (#3, #4):** speech plays to the end.
- **Keyboard didn't work (#11):** keyboard and mouse now reach the game on every desktop, including
  the Display Calibration screen (Enter = accept, Backspace = back).
- **Black world on the first launch** while shaders were still being built.
- **Antivirus warning on the Windows package (#18):** the profiler DLL is no longer shipped.
- **Driving above 30 fps (#12, partly):** vehicles use the 360's 30 fps handling by default; the
  rest of the game stays uncapped.
- macOS: the game fills Retina windows instead of a corner (#14), and a grey-world setting is off
  by default (#13). Thanks to the contributors of #13 and #14.

## New

- **PC keyboard and mouse layout**, following the PC game's defaults, with every action
  remappable (settings menu > Controls; controller buttons too).
- **Settings menu (#6):** F1 with keyboard and mouse (Esc otherwise, RB+Back on a controller):
  Resolution (render scale up to 2x), Fullscreen, Language (the game's 12 languages; speech stays
  English), Controls.
- **Fullscreen (#8):** F11 or Alt+Enter.
- **One-click bug reports** from the settings menu, and a crash report offered on the next start.
- **Android without the disc images (#19):** copy the `game`, `update` and `user` folders from your
  PC install to the phone by USB and choose only the game pack: about 16 GB on the phone instead of
  about 40.

## Known issues

Shadows and anti-aliasing differ from the 360 (#8, #29), some physics are tied to the frame rate
above 30 fps (#12), and the open issues on GitHub list the rest. Phones: S23 FE ~28 fps when cool;
iPhone 13 mini ~30 fps in the city.

## Checksums

_(SHA-256 of every asset, filled in at final assembly)_
