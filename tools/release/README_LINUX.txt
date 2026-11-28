GTA V Xbox 360 recompilation - Linux
=====================================

This package contains no game. It holds a launcher, an Xbox 360 runtime and the tools that turn
your own copy of Grand Theft Auto V for Xbox 360 into a native Linux build on your computer.
No Rockstar Games or Microsoft game code, data, shaders or title-update files are included.

Do not share what you build
---------------------------
Everything this package makes from your discs is Rockstar Games' copyrighted work: the game folder
(data/), the recompiled game library (gtav_game.*), the generated C++, the translated shaders
(data/spirv, shader_cache), and the GTAV.ipa made by "Build for iPhone", and the game files and
game library the Android app makes on your phone. They are for your own devices only. Never upload,
share or link them, and never add them to a fork or a release. The GPL license of this project
covers its own code only; it gives no one the right to share Rockstar's. Rules for forks and
downstream releases:
https://github.com/andrewnakas/gtav-recomp#keeping-it-clean-rules-for-forks-and-downstream-releases

What you need
-------------
- Your own Xbox 360 discs of GTA V: Disc 1 (Install) and Disc 2 (Play), as ISO images or as
  extracted folders.
- Title Update 27 for GTA V from your own console: the TU file (tu00000009_00000000) or a folder
  with disc002/default.xexp and disc002/update.rpf.
- 64-bit Linux with glibc 2.35 and GCC 13's libstdc++ (GLIBCXX_3.4.32) or newer: Ubuntu 24.04,
  Debian 13, Fedora 39, Arch, openSUSE Tumbleweed or newer. (Ubuntu 22.04 works after installing
  libstdc++6 from the ubuntu-toolchain-r/test PPA; Debian 12 is too old.)
- A CPU with AVX2 (Intel Haswell / AMD Zen or newer), a GPU with current Vulkan drivers and the
  Vulkan loader (libvulkan.so.1; on Ubuntu/Debian: libvulkan1 and mesa-vulkan-drivers, or the
  NVIDIA driver). A desktop session (X11 or Wayland): the X11/xcb and Wayland client libraries,
  zlib and zstd that every desktop system has.
- A controller (Xbox-style, through SDL).
- About 25 GB free on this drive during setup (about 17 GB once installed), 8 GB RAM or more.
  Put the package on a Linux filesystem (ext4, btrfs, xfs) or exFAT; it needs executable files.

First run
---------
1. Unpack the archive somewhere with enough space:
     tar xf gtav-recomp-linux-x64.tar.gz
     cd gtav-recomp-linux-x64
2. Open gtav_launcher: double-click it in your file manager (GNOME Files: right-click it, "Run as
   a Program"), or run ./gtav_launcher in a terminal. The setup window opens: choose Disc 1,
   Disc 2 and the title update (each an image/file or an extracted folder), then press Install.
   Without a desktop (or with --cli) the same setup runs in the terminal instead, and
     ./gtav_launcher --setup "/path/Disc 1.iso" "/path/Disc 2.iso" /path/tu00000009_00000000
   runs it with the paths given.
3. The launcher checks every file against known-good hashes, copies them into data/, generates C++
   from your game executable, compiles it into gtav_game.so with the bundled clang and removes the
   build files. This takes 15-30 minutes depending on your CPU. Cancel (or Ctrl+C in the terminal)
   stops it; running setup again continues where it stopped.
4. Press Play. The first minutes of play translate shaders in the background; some objects can be
   missing until their shader is ready. They are kept in data/spirv for later runs. Setup also adds
   "GTA V recomp" to your applications menu.

Later runs: the menu entry or ./gtav_launcher starts the game directly. ./gtav_launcher --setup
(or the menu entry's "Run setup again") opens the setup window again, for example after moving
your discs.

Game options go after "--". On a laptop with two GPUs, pick the dedicated one with
  ./gtav_launcher -- --vulkan_device=1
F1 in game opens the settings menu (fullscreen, resolution, frame rate limit, VSync, controls,
Create bug report); with a controller, RB + Back. Keyboard and mouse use GTA V PC's default keys.
The game log is data/logs/gtav.log, the setup log data/logs/install.log.

Steam Deck: in desktop mode Steam keeps the Deck's controls on its desktop layout (trackpad =
mouse), so the game sees no controller. Add gtav_launcher to Steam (Add a Non-Steam Game), start it
from Steam and pick the Gamepad controller layout.

iPhone (experimental)
---------------------
Play GTA V on an iPhone 12 or newer (iOS 17+). The phone cannot build the game itself, so this
PC does it once (about 15 minutes); the phone then installs the game files from your discs.
1. Set up and play the game here for a while first (the phone gets the shaders the PC has seen:
   drive around, go indoors).
2. Press "Build for iPhone" in the setup window (or run ./gtav_launcher --iphone). When it is
   done, iphone/GTAV.ipa is next to it.
3. Install GTAV.ipa with a sideloading tool that runs on Linux (for example AltServer-Linux with
   AltStore), or copy it to a Windows PC or Mac with Sideloadly. A free Apple ID re-signs every
   7 days.
4. Put your Disc 1 ISO, Disc 2 ISO and the TU file on a USB stick and plug it into the phone (or
   copy them into GTA V's folder in the phone's Files app).
5. Open GTA V on the phone and press Install. It copies and checks about 16 GB, then starts.
Objects missing on the phone? Copy the phone's GTA V > shader_cache folder to iphone/shader_cache
here, run --iphone again (a few minutes) and reinstall GTAV.ipa; the game files stay.

Android (experimental)
----------------------
Play GTA V on an Android phone: arm64, Android 11 or newer, Vulkan 1.1, Snapdragon 845 or newer
(tested: Galaxy S23 FE). The app has no game in it. It sets the game up on the phone by itself,
the way this package does here: it copies and checks your disc files, recompiles the game and
compiles it on the phone. Nothing is built for Android on this computer any more.
1. Copy android/GTAV.apk from this folder to the phone and open it there to install it
   (allow installing apps from that source).
2. Copy your Disc 1 ISO, Disc 2 ISO and the TU file to the phone (for example its Download
   folder). Setup needs about 40 GB free; the disc images can be deleted afterwards.
3. Open GTA V, choose the three files and press Install. Keep the phone plugged in: the build
   takes about 15 minutes on the S23 FE, longer on slower phones. The game starts when it is
   done; shaders are translated while you play, so the first minutes are slower.
Already set up here? Open the app once, copy the game, update and user folders from data/ here to
the phone's Android/data/com.gtavrecomp.app/files/data by USB and press Install without choosing
anything: about 16 GB on the phone instead of about 40.
Updating from 0.3.0: install the new GTAV.apk over the old app and press Install once; the game
files and saves stay. The Android pack from the PC is no longer used.
Known issues on Android: the phone heats up in sustained play and the frame rate drops with it
(S23 FE: about 45-50 fps while cool, about 30 once hot); the touch controls are basic (a
controller works better).

Everything made from your files stays in this folder (data/, gtav_game.so and iphone/GTAV.ipa)
and on your phone. Do not share any of it: it contains Rockstar Games' copyrighted
work. (android/GTAV.apk as downloaded holds no game and is fine to share.)

This project is free software under the GNU GPL, version 3 or later (LICENSES/gtav-recomp-GPL-3.0.txt).
Source code: https://github.com/andrewnakas/gtav-recomp. The complete source of every program in
this package that is under the GPL or LGPL (this project, the ReXGlue SDK runtime with the FFmpeg
it links statically, XenosRecomp, plume) is in gtav-recomp-src-<version>.tar.xz on the same
release page.
Bundled tools: clang and lld from LLVM 19.1.5 (official release build, Apache-2.0 with LLVM
exception), DirectX Shader Compiler (NCSA). tools/llvm/lib/libxml2.so.2 is not libxml2: it is a
small stand-in from this project (tools/release/lld_libxml2_stub.c) so lld starts on systems
without libxml2; the ELF linking the launcher does never uses it.
Licenses for the bundled open-source components are in LICENSES/.
Grand Theft Auto V is a trademark of Take-Two Interactive Software. This project is not affiliated
with or endorsed by Rockstar Games, Take-Two Interactive or Microsoft.
