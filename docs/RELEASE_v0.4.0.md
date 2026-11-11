# v0.4.0 build sheet

Same shape as v0.3.0 (`docs/RELEASE_v0.3.0.md`: read its sections for the full commands; only the
differences are written here). One GitHub release, tag `v0.4.0`, every package built from the
**same commit** on `issue-fixes`. Version strings are already set (d8e21bf): Mac app 0.4.0 / 4,
iPhone 0.4.0, Android versionName 0.4.0 / versionCode 4.

**Nothing starts here until the owner says Mac and Windows passed `docs/HANDOFF_MAC_WINDOWS_2026-11-11.md`
and says to ship.** Until then: no draft release, no tag, no push to `main`.

## Release commit

The newest `issue-fixes` commit that Mac, Windows and Linux all tested. If Mac or Windows needed
fixes, that is their last fix commit, and Linux/Android are rebuilt from it (step 3). Write it down
here: `release commit = ________`.

## Assets

| asset | made on | contents |
|---|---|---|
| `gtav-recomp-windows-x64.zip` | Windows | launcher, runtime, VC++ runtime, tools, `ios/`, `android/` |
| `gtav-recomp-linux-x64.tar.gz` | Linux | the same for Linux (glibc 2.35 floor, compat container) |
| `gtav-recomp-macos-arm64.zip` | Mac | `GTAV.app` with `ios/` and `android/` inside |
| `gtav-recomp-src-v0.4.0.tar.xz` | any (one) | complete corresponding source (make_source_archive.sh) |

All three packages carry the identical `ios/` (Mac-built) and `android/` (Linux-built) folders.
They are shared through the draft as `ios-shell-v0.4.0.tar` and `android-v0.4.0.tar`, deleted
before publishing.

## Order on the owner's go

1. **Mac** creates the draft `v0.4.0` (target = release commit), builds the iPhone shell
   (`build/release-ios-shell`, then `tools/ios/make_shell.sh`; must include b2276fa) and uploads
   `ios-shell-v0.4.0.tar`.
2. **Linux** uploads `android-v0.4.0.tar` and, after downloading the iOS shell,
   `gtav-recomp-linux-x64.tar.gz`. Prepared ahead of the go (see "Linux status" below) so this is
   an upload, unless the release commit changed.
3. **Mac** builds `gtav-recomp-macos-arm64.zip` with both folders; **Windows** builds
   `gtav-recomp-windows-x64.zip` with both folders (`make_release.py ... --ios --android --zip`,
   from a VS x64 developer prompt so the VC++ runtime is bundled). No stand-in `android/` this
   time: download `android-v0.4.0.tar` from the draft first.
4. Each machine, from its **unpacked** package with a test game: `--iphone` and `--android`
   (811/811, 0 failed shaders) and its taint scan (`scan_release.sh` / `scan_release.cmd` /
   `scan_release_macos.sh`, PASS), then uploads.
5. **Final assembly** (any machine): the three packages' `android/GTAV.apk` and `ios/GTAV.app/gtav`
   have equal SHA-256; source archive (`make_source_archive.sh gtav-recomp-src-v0.4.0.tar.xz v0.4.0
   rexglue-sdk thirdparty/XenosRecomp thirdparty/plume`, needs the local tag at the release
   commit); delete the two helper tars; paste `docs/RELEASE_NOTES_v0.4.0.md` with the SHA-256 list.
6. **Owner** publishes: fast-forward `main` to the release commit (`git push origin <commit>:main`,
   main has not moved since 2a76184), push the tag, publish the draft as Latest. Then the issues
   listed in the notes as fixed can be closed with a link to the release.

## Linux status (this machine)

- Compat container build of the runtime, XenosRecomp, gtav_fast and the launcher at d8e21bf:
  see the line below (filled in when done).
- Android shell (`build/android-dll`, b2276fa source; the 0.4.0 manifest only changes the APK)
  -> `dist/v0.4.0/android` with `tools/android/make_shell.sh`, tested on the S23 FE.
- Package: `make_release_linux.py OUT --compat --llvm ... --expected tools/release/expected.txt
  --licenses ... --ios <ios> --android dist/v0.4.0/android --tar` needs the Mac's iOS shell, so it
  is finished after step 1.

Linux build log (2026-11-11): compat container build at d8e21bf **done** (clean rebuild: the
recreated image's headers invalidated the 11-01 precompiled headers, so the old `*-compat` trees were
deleted). Floors: glibc 2.35, GLIBCXX_3.4.32 (same as 0.3.0). Clean `ubuntu:24.04` container: the
launcher and XenosRecomp start; gtav_fast only lacks `gtav_game.so`, which the player's install
builds. `dist/v0.4.0/android` built (APK versionCode 4 / 0.4.0, shell `ecd8c201...`, the one tested
on the S23 FE). Left: package with the Mac's iOS shell, unattended install + `--android` from the
unpacked tarball, `scan_release.sh`.
