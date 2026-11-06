"""Assemble the player release: the launcher, the runtime, the tools that build the game on the
player's machine, and licenses. Nothing from the game or the console ships: no XEX/XEXP, disc or
title-update files, no generated C++, no compiled game code, no shaders, no dumps.

usage: make_release.py OUT_DIR --llvm DIR --expected FILE [--zip]
  OUT_DIR     a new or empty folder (refuses a folder that holds anything but a previous release)
  --llvm      folder with bin/clang.exe, bin/lld-link.exe, lib/clang/19/include, LICENSE.TXT
  --expected  tools/game_dll/expected.txt (make_expected.py data + gen, concatenated)
  --licenses  folder with extra license texts (dxc-LICENSE.TXT)
Run taint_scan.py on OUT_DIR before publishing.
"""
import argparse
import os
import shutil
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]  # E:\gtav-native
BUILD = REPO / "build-dll"
SDK = Path(r"E:\gtav-rex\rexglue-sdk")
SDK_BIN = SDK / "out" / "install" / "win-amd64" / "bin"
XR = REPO / "thirdparty" / "XenosRecomp"
MARKER = "gtav_release.txt"

# (source, destination below OUT_DIR)
FILES = [
    (REPO / "build-launcher" / "gtav_launcher.exe", "gtav_launcher.exe"),
    (BUILD / "gtav_fast.exe", "gtav_fast.exe"),
    (BUILD / "rexruntimerd.dll", "rexruntimerd.dll"),
    (BUILD / "rexgpu-xenosrd.dll", "rexgpu-xenosrd.dll"),
    (SDK_BIN / "rexgluerd.exe", "rexgluerd.exe"),
    # Shader translation on the player's machine (shader_compiler.cpp). dxil.dll is not needed
    # for SPIR-V and stays out.
    (BUILD / "tools" / "XenosRecomp.exe", "tools/XenosRecomp.exe"),
    (BUILD / "tools" / "shader_common.h", "tools/shader_common.h"),
    (BUILD / "tools" / "dxc.exe", "tools/dxc.exe"),
    (BUILD / "tools" / "dxcompiler.dll", "tools/dxcompiler.dll"),
    # Game DLL build inputs: our freestanding header and CRT, the hook list, import lists.
    (REPO / "gtav" / "fs" / "gtav_pch.h", "tools/game_dll/fs/gtav_pch.h"),
    (REPO / "gtav" / "fs" / "layout_check.h", "tools/game_dll/fs/layout_check.h"),
    (REPO / "gtav" / "fs" / "game_dll.cpp", "tools/game_dll/fs/game_dll.cpp"),
    (REPO / "gtav" / "fs" / "fs_crt.cpp", "tools/game_dll/fs/fs_crt.cpp"),
    (REPO / "gtav" / "fs" / "include" / "stdlib.h", "tools/game_dll/fs/include/stdlib.h"),
    (REPO / "gtav" / "fs" / "include" / "malloc.h", "tools/game_dll/fs/include/malloc.h"),
    (BUILD / "game_interface" / "gtav_hooks.h", "tools/game_dll/interface/gtav_hooks.h"),
    (BUILD / "game_interface" / "gtav_game.def", "tools/game_dll/interface/gtav_game.def"),
    (BUILD / "game_interface" / "gtav_fast.def", "tools/game_dll/interface/gtav_fast.def"),
    (BUILD / "game_interface" / "rexruntime.def", "tools/game_dll/interface/rexruntime.def"),
    (BUILD / "game_interface" / "ucrtbase.def", "tools/game_dll/interface/ucrtbase.def"),
    # Codegen inputs: function addresses and sizes only.
    (REPO / "gtav" / "gtav_manifest.toml", "tools/game_dll/gtav_manifest.toml"),
    (REPO / "gtav" / "generated" / "default" / "codegen.partition.json", "tools/game_dll/codegen.partition.json"),
    (REPO / "tools" / "release" / "README.txt", "README.txt"),
]

LICENSES = [
    (REPO / "LICENSE", "gtav-recomp-GPL-3.0.txt"),
    (SDK / "LICENSE", "ReXGlue-SDK.txt"),
    (XR / "LICENSE.md", "XenosRecomp.txt"),
]
# Third-party code linked into the SDK runtime / codegen and XenosRecomp.
SDK_THIRDPARTY = ["FFmpeg", "aes_128", "cli11", "crypto/des", "disasm", "disruptorplus", "dxbc", "fmt",
                  "glslang", "imgui", "inja", "libmspack", "o1heap", "pe", "sdl3", "simde", "spdlog",
                  "spirv-headers", "spirv-tools", "stb", "tiny-aes-c", "tomlplusplus", "tracy", "utfcpp",
                  "vulkan-headers", "vulkan-loader", "vulkan-memory-allocator", "xxHash"]
XR_THIRDPARTY = ["fmt", "smol-v", "xxHash", "zstd"]

FFMPEG_NOTICE = """\
FFmpeg (https://ffmpeg.org) is used under the GNU Lesser General Public License, version 2.1 or
later (sdk-FFmpeg.txt; the LGPL text is in FFmpeg's COPYING.LGPLv2.1, included in the source
archive). It is linked statically into rexruntimerd.dll, configured without GPL or non-free parts
(decoders mp3, wmav2, xmaframes only), from the Xenia FFmpeg branch (github.com/wmarti/FFmpeg,
xenia-ffmpeg-canary-full) as pinned by the ReXGlue SDK.

The complete source of rexruntimerd.dll, FFmpeg included, and of everything else in this release
is in gtav-recomp-src-<version>.tar.xz on the same GitHub release page. To use a modified FFmpeg,
rebuild the SDK from that archive and replace rexruntimerd.dll (see SOURCE.txt in the archive).
"""

# Never in a release, whatever the source lists say.
FORBIDDEN_EXT = {".xex", ".xexp", ".rpf", ".iso", ".spv", ".bin", ".pdb", ".ilk", ".obj", ".lib", ".exp",
                 ".timestamp", ".dmp", ".log", ".hlsl"}


def find_license(folder):
    for name in ("LICENSE", "LICENSE.txt", "LICENSE.TXT", "LICENSE.md", "COPYING", "COPYING.txt",
                 "LICENSE.MIT", "COPYING.LGPLv2.1", "LICENSE.rst"):
        p = folder / name
        if p.is_file():
            return p
    hits = sorted((p for p in folder.rglob("*") if p.is_file() and p.name.upper().startswith(("LICEN", "COPYING"))),
                  key=lambda p: (len(p.parts), str(p)))
    return hits[0] if hits else None


def header_notice(folder):
    """No license file: the leading comment of the first source file, which carries the notice."""
    for p in sorted(folder.rglob("*"), key=lambda p: (len(p.parts), str(p))):
        if p.suffix.lower() in (".c", ".h", ".hpp", ".cpp"):
            lines = p.read_text(errors="replace").splitlines()
            # stb-style headers keep the license at the end.
            tail = [i for i, l in enumerate(lines) if "ALTERNATIVE A - MIT License" in l]
            text = lines[tail[0] - 2:] if tail else lines[:45]
            return f"(license notice from {p.relative_to(folder.parent).as_posix()})\n\n" + "\n".join(text) + "\n"
    return None


def copy(src, dst):
    if not src.is_file():
        sys.exit(f"missing: {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


# App-local Visual C++ runtime (see main): what gtav_fast.exe, the SDK DLLs, the launcher and the
# shader tools import from it.
VC_RUNTIME = ("msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "msvcp140_atomic_wait.dll",
              "vcruntime140.dll", "vcruntime140_1.dll")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--llvm", required=True)
    ap.add_argument("--ios", help="tools/ios/make_shell.sh output (built on a Mac): the iPhone step's ios/ folder")
    ap.add_argument("--android", help="tools/android/make_shell.sh output (built on Linux): the Android step's android/ folder")
    ap.add_argument("--expected", required=True)
    ap.add_argument("--licenses", required=True)
    ap.add_argument("--zip", action="store_true")
    ap.add_argument("--vcruntime", help="folder with the x64 Visual C++ runtime DLLs (default: "
                    "%%VCToolsRedistDir%%\\x64\\Microsoft.VC14x.CRT from a VS developer prompt)")
    a = ap.parse_args()

    out = Path(a.out).resolve()
    # Only ever clear a folder this script made.
    if out.exists() and any(out.iterdir()):
        if not (out / MARKER).is_file() or len(out.parts) < 3:
            sys.exit(f"refusing to clear {out}: not an earlier release folder")
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)

    # The Tracy profiler stays out of player builds (issue #18: antivirus flags TracyClientrd.dll,
    # and its client opens a listening socket). Build the SDK with -DREXGLUE_ENABLE_TRACY=OFF, as
    # the macOS, Linux and Android releases do.
    for dll in (BUILD / "rexruntimerd.dll", BUILD / "rexgpu-xenosrd.dll", BUILD / "gtav_fast.exe"):
        if dll.is_file() and b"TracyClient" in dll.read_bytes():
            sys.exit(f"{dll.name} imports TracyClient: rebuild the SDK with -DREXGLUE_ENABLE_TRACY=OFF")
    for src, rel in FILES:
        copy(src, out / rel)
    # The Visual C++ runtime, app-local (Microsoft's redistributable files, deployed next to the
    # executables that use them): players no longer install the VC++ redistributable. tools\ gets a
    # copy too (XenosRecomp.exe, dxc.exe use it); clang/lld are statically linked.
    crt = Path(a.vcruntime) if a.vcruntime else None
    if crt is None and os.environ.get("VCToolsRedistDir"):
        found = sorted(Path(os.environ["VCToolsRedistDir"], "x64").glob("Microsoft.VC14*.CRT"))
        crt = found[-1] if found else None
    if crt is None or not crt.is_dir():
        sys.exit("--vcruntime: the x64 Visual C++ runtime folder (Microsoft.VC14x.CRT) was not found")
    for dll in VC_RUNTIME:
        copy(crt / dll, out / dll)
        copy(crt / dll, out / "tools" / dll)
    copy(Path(a.expected), out / "tools/game_dll/expected.txt")

    if a.ios:
        # The iPhone step (install.cpp RunIphone): an iOS shell without game code, interface lists,
        # libSystem_ios.tbd and simde; built on a Mac by tools/ios/make_shell.sh, copied as it is.
        ios = Path(a.ios)
        for f in ("GTAV.app/gtav", "GTAV.app/libMoltenVK.dylib", "interface/gtav_game.undefs",
                  "libSystem_ios.tbd", "include/simde/x86/sse4.1.h", "LICENSES/MoltenVK.txt"):
            if not (ios / f).is_file():
                sys.exit(f"--ios: missing {f}")
        shutil.copytree(ios, out / "ios")

    if a.android:
        # The Android step (install.cpp RunAndroid): the public APK without game code, the APK's
        # own libgtav_fast.so / librexruntime.so to link against, interface lists and simde; built
        # on Linux by tools/android/make_shell.sh, copied as it is.
        android = Path(a.android)
        for f in ("GTAV.apk", "link/libgtav_fast.so", "link/librexruntime.so", "interface/gtav_game.exports",
                  "interface/gtav_hooks.h", "include/simde/x86/sse4.1.h", "LICENSES/simde.txt"):
            if not (android / f).is_file():
                sys.exit(f"--android: missing {f}")
        shutil.copytree(android, out / "android")

    llvm = Path(a.llvm)
    copy(llvm / "bin" / "clang.exe", out / "tools/llvm/bin/clang++.exe")
    copy(llvm / "bin" / "lld-link.exe", out / "tools/llvm/bin/lld-link.exe")
    shutil.copytree(llvm / "lib" / "clang" / "19" / "include", out / "tools/llvm/lib/clang/19/include")

    lic = out / "LICENSES"
    for src, name in LICENSES:
        copy(src, lic / name)
    copy(llvm / "LICENSE.TXT", lic / "LLVM.txt")
    copy(Path(a.licenses) / "dxc-LICENSE.TXT", lic / "DirectXShaderCompiler.txt")
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
    # binutils' PowerPC disassembler (SDK thirdparty/disasm) is GPL-3.0-or-later.
    copy(Path(a.licenses) / "GPL-3.0.txt", lic / "third_party" / "GPL-3.0.txt")
    (lic / "third_party" / "FFmpeg-NOTICE.txt").write_text(FFMPEG_NOTICE)

    bad = [p for p in out.rglob("*") if p.is_file() and p.suffix.lower() in FORBIDDEN_EXT]
    if bad:
        sys.exit("forbidden files in release:\n" + "\n".join(str(p) for p in bad))
    (out / MARKER).write_text("gtav-recomp release folder (made by tools/release/make_release.py)\n")

    total = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    count = sum(1 for p in out.rglob("*") if p.is_file())
    print(f"{count} files, {total / 2**20:.0f} MB -> {out}")
    if a.zip:
        z = out.with_suffix(".zip")
        with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
            for p in sorted(out.rglob("*")):
                if p.is_file():
                    zf.write(p, Path(out.name) / p.relative_to(out))
        print(f"zip {z.stat().st_size / 2**20:.0f} MB -> {z}")


if __name__ == "__main__":
    main()
