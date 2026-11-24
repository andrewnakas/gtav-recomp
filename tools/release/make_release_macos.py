"""Assemble the macOS player release (the macOS twin of make_release_linux.py): GTAV.app with the
setup window, the runtime, the tools that build the game on the player's Mac, and licenses. Nothing
from the game or the console ships: no XEX/XEXP, disc or title-update files, no generated C++, no
compiled game code, no shaders, no dumps.

usage: make_release_macos.py OUT_DIR --llvm DIR --expected FILE --licenses DIR [--sdk DIR] [--zip]
  OUT_DIR     a new or empty folder; GTAV.app, README.txt and LICENSES/ go into it
  --llvm      the unpacked official LLVM 19.1.7 macOS arm64 release (LLVM-19.1.7-macOS-ARM64):
              bin/clang-19, bin/lld, lib/clang/19/include
  --expected  tools/release/expected.txt (same file as Windows and Linux: Mac codegen is identical)
  --licenses  folder with LLVM.txt and DirectXShaderCompiler.txt
  --sdk       the SDK install prefix (default rexglue-sdk/out/install/mac-arm64)
  --zip       also write OUT_DIR.zip next to it (ditto, keeps the signatures)
Builds first (tools/release/build_release_macos.sh, fresh trees under build/release-mac):
gtav-fast (gtav/ with -DGTAV_GAME_DLL=ON), launcher (loader/), xenosrecomp (thirdparty/XenosRecomp,
BUILD_SHARED_LIBS=OFF), all for macOS 15 against the SDK in rexglue-sdk/out/install/mac-rel.
Run scan_release_macos.sh on OUT_DIR before publishing.

App layout (the bundle is read-only once signed; the launcher copies the runtime into the game
folder the player picks, see loader/src/install.cpp):
  GTAV.app/Contents/MacOS/GTAV                 setup window + launcher (loader/src/main_mac.mm)
  GTAV.app/Contents/Resources/gtav/            the package (install.cpp's root)
    gtav_fast, lib*.dylib, vulkan/             runtime: copied to the game folder before each launch
    rexglue                                    ReXGlue codegen
    tools/llvm/                                clang + ld64.lld
    tools/game_dll/                            freestanding header, simde, interface lists, manifest,
                                               partition seed, libSystem.tbd, expected hashes
    tools/shaders/                             XenosRecomp, dxc, libdxcompiler (-> game folder tools/)
"""
import argparse
import os
import plistlib
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_release import (FFMPEG_NOTICE, FORBIDDEN_EXT, MARKER, SDK_THIRDPARTY, XR_THIRDPARTY,  # noqa: E402
                          find_license, header_notice)

REPO = Path(__file__).resolve().parents[2]
BUILD = REPO / "build" / "release-mac" / "gtav-fast"
LAUNCHER = REPO / "build" / "release-mac" / "launcher" / "GTAV.app"
SDK = REPO / "rexglue-sdk"
XR_BUILD = REPO / "build" / "release-mac" / "xenosrecomp"
XR = REPO / "thirdparty" / "XenosRecomp"
DXC = XR / "thirdparty" / "dxc-bin"
FS = REPO / "gtav" / "fs"

LICENSES = [
    (REPO / "LICENSE", "gtav-recomp-GPL-3.0.txt"),
    (REPO / "LICENSE", "third_party/GPL-3.0.txt"),  # binutils' PowerPC disassembler (SDK)
    (SDK / "LICENSE", "ReXGlue-SDK.txt"),
    (XR / "LICENSE.md", "XenosRecomp.txt"),
]

FFMPEG_NOTICE_MACOS = FFMPEG_NOTICE.replace("rexruntimerd.dll", "librexruntime.dylib")

# Libraries a shipped Mach-O may load: macOS's own, or ones in the package.
PACKAGED_LIBS = {"@rpath/librexruntime.dylib", "@rpath/librexgpu-xenos.dylib",
                 "@rpath/libTracyClient.dylib", "@rpath/libdxcompiler.dylib",
                 "@rpath/libvulkan.1.dylib", "@rpath/libMoltenVK.1.dylib",
                 "@rpath/gtav_game.dylib"}  # gtav_game.dylib: built by the installer


def copy(src, dst, exe=False):
    if not src.is_file():
        sys.exit(f"missing: {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    os.chmod(dst, 0o755 if exe else 0o644)


def is_macho(p):
    return p.read_bytes()[:4] in (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe")


def thin(path):
    """arm64 slice only (dxc ships universal)."""
    archs = subprocess.run(["lipo", "-archs", str(path)], capture_output=True, text=True).stdout.split()
    if len(archs) > 1:
        subprocess.run(["lipo", str(path), "-thin", "arm64", "-output", str(path)], check=True)


def check_macho(out, ios_dir=None):
    bad = []
    for p in sorted(out.rglob("*")):
        if not p.is_file() or p.is_symlink() or not is_macho(p):
            continue
        if ios_dir and ios_dir in p.parents:
            continue  # check_ios_shell
        libs = subprocess.run(["otool", "-L", str(p)], capture_output=True, text=True).stdout
        for line in libs.splitlines()[1:]:
            lib = line.strip().split(" (")[0]
            if lib.startswith(("/usr/lib/", "/System/Library/")) or lib in PACKAGED_LIBS:
                continue
            if lib == f"@rpath/{p.name}":
                continue  # a dylib's own install name
            bad.append(f"{p.relative_to(out)} needs {lib}")
        cmds = subprocess.run(["otool", "-l", str(p)], capture_output=True, text=True).stdout
        for line in cmds.splitlines():
            line = line.strip()
            if line.startswith("path /") or line.startswith("name /Users"):
                bad.append(f"{p.relative_to(out)} has an absolute path: {line}")
        minos = [l.split()[1] for l in cmds.splitlines() if l.strip().startswith("minos ")]
        if minos and tuple(int(x) for x in minos[0].split(".")[:2]) > (15, 0):
            bad.append(f"{p.relative_to(out)} needs macOS {minos[0]} (build for 15.0)")
    if bad:
        sys.exit("shipped binaries that would fail on players' Macs:\n" + "\n".join(bad))


# The iOS shell (tools/ios/make_shell.sh): everything next to the exe; the game library is made by
# the player's Mac (install.cpp RunIphone).
IOS_LIBS = {"@rpath/librexruntime.dylib", "@rpath/librexgpu-xenos.dylib", "@rpath/libMoltenVK.dylib",
            "@rpath/gtav_game.dylib"}


def check_ios_shell(app):
    bad = []
    for p in sorted(app.rglob("*")):
        if not p.is_file() or not is_macho(p):
            continue
        cmds = subprocess.run(["otool", "-l", str(p)], capture_output=True, text=True).stdout
        if "platform 2" not in cmds:
            bad.append(f"{p.name} is not an iOS binary")
        libs = subprocess.run(["otool", "-L", str(p)], capture_output=True, text=True).stdout
        for line in libs.splitlines()[1:]:
            lib = line.strip().split(" (")[0]
            if lib.startswith(("/usr/lib/", "/System/Library/")) or lib in IOS_LIBS or lib == f"@rpath/{p.name}":
                continue
            bad.append(f"ios/{p.name} needs {lib}")
        for line in cmds.splitlines():
            line = line.strip()
            if line.startswith("path /") or line.startswith("name /Users"):
                bad.append(f"ios/{p.name} has an absolute path: {line}")
        minos = [l.split()[1] for l in cmds.splitlines() if l.strip().startswith("minos ")]
        if minos and tuple(int(x) for x in minos[0].split(".")[:2]) > (17, 0):
            bad.append(f"ios/{p.name} needs iOS {minos[0]} (build for 17.0)")
    if subprocess.run(["otool", "-L", str(app / "gtav")], capture_output=True, text=True).stdout.count(
            "@rpath/gtav_game.dylib") != 1:
        bad.append("ios/gtav is not the shell build (it must load gtav_game.dylib)")
    if bad:
        sys.exit("iOS shell problems:\n" + "\n".join(bad))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--llvm", required=True)
    ap.add_argument("--expected", required=True)
    ap.add_argument("--licenses", required=True)
    ap.add_argument("--sdk", default=str(SDK / "out" / "install" / "mac-arm64"))
    ap.add_argument("--zip", action="store_true")
    ap.add_argument("--ios-shell", help="tools/ios/make_shell.sh output folder (the iPhone step's ios/)")
    ap.add_argument("--android", help="tools/android/make_shell.sh output (built on Linux): the Android step's android/ folder")
    ap.add_argument("--allow-newer-os", action="store_true", help="skip the macOS 15 floor check (testing)")
    a = ap.parse_args()
    sdk_install = Path(a.sdk)

    out = Path(a.out).resolve()
    if out.exists() and any(p.name != ".DS_Store" for p in out.iterdir()):
        if not (out / MARKER).is_file() or len(out.parts) < 3:
            sys.exit(f"refusing to clear {out}: not an earlier release folder")
        # Empty it in place: Finder writes .DS_Store into an open folder while it is removed.
        for p in out.iterdir():
            if p.is_dir() and not p.is_symlink():
                shutil.rmtree(p)
            else:
                p.unlink(missing_ok=True)
    out.mkdir(parents=True, exist_ok=True)

    app = out / "GTAV.app"
    shutil.copytree(LAUNCHER, app, symlinks=True)
    pkg = app / "Contents" / "Resources" / "gtav"
    files = [
        (BUILD / "gtav_fast", "gtav_fast", True),
        (sdk_install / "lib" / "librexruntime.dylib", "librexruntime.dylib", True),
        (sdk_install / "lib" / "librexgpu-xenos.dylib", "librexgpu-xenos.dylib", True),
        (sdk_install / "bin" / "rexglue", "rexglue", True),  # rpath @loader_path: librexruntime
        (sdk_install / "lib" / "libvulkan.1.dylib", "vulkan/lib/libvulkan.1.dylib", True),
        (sdk_install / "lib" / "libMoltenVK.dylib", "vulkan/lib/libMoltenVK.dylib", True),
        (sdk_install / "share" / "vulkan" / "icd.d" / "MoltenVK_icd.json",
         "vulkan/share/vulkan/icd.d/MoltenVK_icd.json", False),
        # Shader translation on the player's Mac (shader_compiler.cpp); copied to <game>/tools.
        (BUILD / "tools" / "XenosRecomp", "tools/shaders/XenosRecomp", True),
        (BUILD / "tools" / "dxc", "tools/shaders/dxc", True),
        (DXC / "lib" / "arm64" / "libdxcompiler.dylib", "tools/shaders/lib/libdxcompiler.dylib", True),
        (XR / "XenosRecomp" / "shader_common.h", "tools/shaders/shader_common.h", False),
        # Game library build inputs: our freestanding header, the hook list, the export lists.
        (FS / "gtav_pch.h", "tools/game_dll/fs/gtav_pch.h", False),
        (FS / "layout_check.h", "tools/game_dll/fs/layout_check.h", False),
        (FS / "game_dll.cpp", "tools/game_dll/fs/game_dll.cpp", False),
        (FS / "include" / "stdlib.h", "tools/game_dll/fs/include/stdlib.h", False),
        (FS / "include" / "malloc.h", "tools/game_dll/fs/include/malloc.h", False),
        (REPO / "tools" / "game_dll" / "libSystem.tbd", "tools/game_dll/libSystem.tbd", False),
        (BUILD / "game_interface" / "gtav_hooks.h", "tools/game_dll/interface/gtav_hooks.h", False),
        (BUILD / "game_interface" / "gtav_game.exports", "tools/game_dll/interface/gtav_game.exports", False),
        (BUILD / "game_interface" / "gtav_game.undefs", "tools/game_dll/interface/gtav_game.undefs", False),
        # Codegen inputs: function addresses and sizes only.
        (REPO / "gtav" / "gtav_manifest.toml", "tools/game_dll/gtav_manifest.toml", False),
        (REPO / "tools" / "game_dll" / "codegen.partition.json", "tools/game_dll/codegen.partition.json", False),
    ]
    # Tracy is off in release SDK builds since #18 (antivirus flagged the Windows DLL); ship it only
    # when the SDK was built with it, as gtav_fast then links it.
    tracy = sdk_install / "lib" / "libTracyClient.dylib"
    if tracy.is_file():
        files.append((tracy, "libTracyClient.dylib", True))
    for src, rel, exe in files:
        copy(src, pkg / rel, exe)
    copy(Path(a.expected), pkg / "tools/game_dll/expected.txt")
    # simde (MIT): the SSE-on-NEON headers the generated code compiles against, as in the SDK build.
    shutil.copytree(SDK / "thirdparty" / "simde" / "simde", pkg / "tools/game_dll/fs/include/simde",
                    ignore=shutil.ignore_patterns("*.md", "*.txt", ".*"))
    thin(pkg / "tools/shaders/lib/libdxcompiler.dylib")
    # libvulkan finds MoltenVK through the ICD manifest next to it (as in the dev build tree).
    subprocess.run(["strip", "-S", "-x"] + [str(pkg / f) for f in
                    ("gtav_fast", "librexruntime.dylib", "librexgpu-xenos.dylib", "rexglue",
                     "tools/shaders/XenosRecomp") + (("libTracyClient.dylib",) if tracy.is_file() else ())], check=True, capture_output=True)

    ios_dir = None
    if a.ios_shell:  # tools/ios/make_shell.sh output: the iPhone step's inputs (install.cpp RunIphone)
        shutil.copytree(Path(a.ios_shell), pkg / "ios")
        ios_dir = pkg / "ios" / "GTAV.app"
        check_ios_shell(ios_dir)

    if a.android:
        # The Android step (install.cpp RunAndroid): the public APK without game code, the APK's own
        # libgtav_fast.so / librexruntime.so to link against, interface lists and simde; built on Linux
        # by tools/android/make_shell.sh, copied as it is (same checks as make_release_linux.py).
        android = Path(a.android)
        for f in ("GTAV.apk", "link/libgtav_fast.so", "link/librexruntime.so", "interface/gtav_game.exports",
                  "interface/gtav_hooks.h", "include/simde/x86/sse4.1.h", "LICENSES/simde.txt"):
            if not (android / f).is_file():
                sys.exit(f"--android: missing {f}")
        shutil.copytree(android, pkg / "android")

    llvm = Path(a.llvm)
    copy(llvm / "bin" / "clang-19", pkg / "tools/llvm/bin/clang++", True)
    copy(llvm / "bin" / "lld", pkg / "tools/llvm/bin/ld64.lld", True)  # lld picks Mach-O from the name
    shutil.copytree(llvm / "lib" / "clang" / "19" / "include", pkg / "tools/llvm/lib/clang/19/include")

    lic = out / "LICENSES"
    for src, name in LICENSES:
        copy(src, lic / name)
    copy(Path(a.licenses) / "LLVM.txt", lic / "LLVM.txt")
    copy(Path(a.licenses) / "DirectXShaderCompiler.txt", lic / "DirectXShaderCompiler.txt")
    for base, names, prefix in ((SDK / "thirdparty", SDK_THIRDPARTY, "sdk-"), (XR / "thirdparty", XR_THIRDPARTY, "xenosrecomp-")):
        for n in names:
            dst = lic / "third_party" / (prefix + n.replace("/", "-") + ".txt")
            p = find_license(base / n)
            if p:
                copy(p, dst)
            elif notice := header_notice(base / n):
                dst.write_text(notice)
            else:
                sys.exit(f"no license found in {base / n}")
    for p in ((SDK / "thirdparty" / "moltenvk"),):
        if lp := find_license(p):
            copy(lp, lic / "third_party" / "sdk-moltenvk.txt")
    (lic / "third_party" / "FFmpeg-NOTICE.txt").write_text(FFMPEG_NOTICE_MACOS)
    copy(REPO / "tools" / "release" / "README_MACOS.txt", out / "README.txt")
    shutil.copytree(lic, app / "Contents" / "Resources" / "LICENSES")

    if not a.allow_newer_os:
        check_macho(app, ios_dir)
    bad = [p for p in out.rglob("*") if p.is_file() and p.suffix.lower() in FORBIDDEN_EXT]
    if bad:
        sys.exit("forbidden files in release:\n" + "\n".join(str(p) for p in bad))

    # Ad-hoc signature, inside out (no --deep: it skips files outside the code locations).
    machos = [p for p in app.rglob("*") if p.is_file() and not p.is_symlink() and is_macho(p)
              and p != app / "Contents" / "MacOS" / "GTAV"]
    for p in machos:
        subprocess.run(["codesign", "--force", "--sign", "-", str(p)], check=True, capture_output=True)
    subprocess.run(["codesign", "--force", "--sign", "-", str(app)], check=True)
    subprocess.run(["codesign", "--verify", "--strict", str(app)], check=True)

    with open(app / "Contents" / "Info.plist", "rb") as f:
        version = plistlib.load(f).get("CFBundleShortVersionString", "?")
    (out / MARKER).write_text(f"gtav-recomp release folder {version} (made by tools/release/make_release_macos.py)\n")

    total = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    count = sum(1 for p in out.rglob("*") if p.is_file())
    print(f"{count} files, {total / 2**20:.0f} MB -> {out}")
    if a.zip:
        z = out.parent / (out.name + ".zip")
        z.unlink(missing_ok=True)
        # No ._ AppleDouble entries (xattrs such as com.apple.provenance): unzipped off a Mac they
        # become real files (v0.2.0-macos had 1,873 of them; harmless on a Mac).
        subprocess.run(["ditto", "-c", "-k", "--norsrc", "--noextattr", "--noacl", "--keepParent", str(out), str(z)],
                       check=True)
        print(f"zip {z.stat().st_size / 2**20:.0f} MB -> {z}")


if __name__ == "__main__":
    main()
