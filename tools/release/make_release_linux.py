"""Assemble the Linux player release (the Linux twin of make_release.py): the launcher, the runtime,
the tools that build the game on the player's machine, and licenses. Nothing from the game or the
console ships: no XEX/XEXP, disc or title-update files, no generated C++, no compiled game code, no
shaders, no dumps.

usage: make_release_linux.py OUT_DIR --llvm DIR --expected FILE --licenses DIR [--compat] [--tar]
  OUT_DIR     a new or empty folder (refuses a folder that holds anything but a previous release)
  --llvm      the unpacked official LLVM 19.1.5 Linux release (LLVM-19.1.5-Linux-X64): bin/clang-19,
              bin/lld, lib/clang/19/include
  --expected  tools/release/expected.txt (make_expected.py data + gen, concatenated)
  --licenses  folder with LLVM.txt, DirectXShaderCompiler.txt and third_party/GPL-3.0.txt (the
              Windows release's LICENSES folder has all three)
  --compat    package the portable build (tools/release/compat/build.sh: *-compat trees, glibc 2.35)
              instead of the native one
  --tar       also write OUT_DIR.tar.gz next to it
Builds first: build-dll (gtav/ with -DGTAV_GAME_DLL=ON), build-launcher (loader/), the SDK install
(rexglue-sdk/out/install/linux-amd64) and build/xenosrecomp. Run scan_release.sh on OUT_DIR before
publishing.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_release import (FFMPEG_NOTICE, FORBIDDEN_EXT, MARKER, SDK_THIRDPARTY, XR_THIRDPARTY,  # noqa: E402
                          find_license, header_notice)

REPO = Path(__file__).resolve().parents[2]
COMPAT = "--compat" in sys.argv
BUILD = REPO / ("build-dll-compat" if COMPAT else "build-dll")
LAUNCHER = REPO / ("build-launcher-compat" if COMPAT else "build-launcher")
SDK = REPO / "rexglue-sdk"
SDK_INSTALL = SDK / "out" / "install" / ("compat" if COMPAT else "linux-amd64")
XR_BUILD = REPO / "build" / ("xenosrecomp-compat" if COMPAT else "xenosrecomp")
XR = REPO / "thirdparty" / "XenosRecomp"
DXC = XR / "thirdparty" / "dxc-bin"
STUB = REPO / "tools" / "release" / "lld_libxml2_stub"

# The Tracy profiler stays out of player builds (issue #18; the SDK option defaults off since
# f7707d5): a runtime that still needs libTracyClient.so fails check_needed below.
# (source, destination below OUT_DIR, executable)
FILES = [
    (LAUNCHER / "gtav_launcher", "gtav_launcher", True),
    (BUILD / "gtav_fast", "gtav_fast", True),
    (SDK_INSTALL / "lib" / "librexruntime.so", "librexruntime.so", True),
    (SDK_INSTALL / "lib" / "librexgpu-xenos.so", "librexgpu-xenos.so", True),
    (SDK_INSTALL / "bin" / "rexglue", "rexglue", True),  # RUNPATH $ORIGIN finds librexruntime.so
    # Shader translation on the player's machine (shader_compiler.cpp). libdxil.so is not needed
    # for SPIR-V and stays out (as dxil.dll does on Windows).
    (XR_BUILD / "XenosRecomp" / "XenosRecomp", "tools/XenosRecomp", True),
    (XR / "XenosRecomp" / "shader_common.h", "tools/shader_common.h", False),
    (BUILD / "dxc_wrapper.sh", "tools/dxc", True),
    (DXC / "bin" / "x64" / "dxc-linux", "tools/dxc-linux", True),
    (DXC / "lib" / "x64" / "libdxcompiler.so", "tools/lib/libdxcompiler.so", True),
    # Game library build inputs: our freestanding header and CRT, the hook list, the export list.
    (REPO / "gtav" / "fs" / "gtav_pch.h", "tools/game_dll/fs/gtav_pch.h", False),
    (REPO / "gtav" / "fs" / "layout_check.h", "tools/game_dll/fs/layout_check.h", False),
    (REPO / "gtav" / "fs" / "gprlr_inline.h", "tools/game_dll/fs/gprlr_inline.h", False),
    (REPO / "gtav" / "fs" / "game_dll.cpp", "tools/game_dll/fs/game_dll.cpp", False),
    (REPO / "gtav" / "fs" / "fs_crt.cpp", "tools/game_dll/fs/fs_crt.cpp", False),
    (REPO / "gtav" / "fs" / "include" / "stdlib.h", "tools/game_dll/fs/include/stdlib.h", False),
    (REPO / "gtav" / "fs" / "include" / "malloc.h", "tools/game_dll/fs/include/malloc.h", False),
    (BUILD / "game_interface" / "gtav_hooks.h", "tools/game_dll/interface/gtav_hooks.h", False),
    (BUILD / "game_interface" / "gtav_game.exports", "tools/game_dll/interface/gtav_game.exports", False),
    # Codegen inputs: function addresses and sizes only.
    (REPO / "gtav" / "gtav_manifest.toml", "tools/game_dll/gtav_manifest.toml", False),
    (REPO / "tools" / "game_dll" / "codegen.partition.json", "tools/game_dll/codegen.partition.json", False),
    (REPO / "tools" / "release" / "README_LINUX.txt", "README.txt", False),
]

LICENSES = [
    (REPO / "LICENSE", "gtav-recomp-GPL-3.0.txt"),
    (SDK / "LICENSE", "ReXGlue-SDK.txt"),
    (XR / "LICENSE.md", "XenosRecomp.txt"),
]

FFMPEG_NOTICE_LINUX = FFMPEG_NOTICE.replace("rexruntimerd.dll", "librexruntime.so")


def copy(src, dst, exe=False):
    if not src.is_file():
        sys.exit(f"missing: {src}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    os.chmod(dst, 0o755 if exe else 0o644)


def set_runpath(path, new):
    """Replace an ELF DT_RUNPATH/DT_RPATH string in place (the new one must not be longer)."""
    old = subprocess.run(["readelf", "-d", str(path)], capture_output=True, text=True,
                         check=True).stdout
    line = next((l for l in old.splitlines() if "(RUNPATH)" in l or "(RPATH)" in l), None)
    if not line:
        return
    current = line.split("[", 1)[1].rsplit("]", 1)[0].encode()
    data = bytearray(path.read_bytes())
    needle = current + b"\0"
    if data.count(needle) != 1 or len(new) > len(current):
        sys.exit(f"cannot rewrite the runpath of {path} ({current!r})")
    i = data.index(needle)
    data[i:i + len(current)] = new.encode().ljust(len(current), b"\0")
    path.write_bytes(bytes(data))


# Libraries a shipped ELF may need: the system's (glibc, libstdc++, zlib/zstd, X11/xcb, Wayland,
# which every desktop has) or ones in the package. Anything else would be missing on players'
# machines (a dynamically linked libfmt slipped into XenosRecomp once).
SYSTEM_LIBS = {"libc.so.6", "libm.so.6", "libstdc++.so.6", "libgcc_s.so.1", "ld-linux-x86-64.so.2",
               "libdl.so.2", "libpthread.so.0", "librt.so.1", "libz.so.1", "libzstd.so.1",
               "libX11.so.6", "libX11-xcb.so.1", "libxcb.so.1", "libXau.so.6", "libXdmcp.so.6",
               "libwayland-client.so.0", "libffi.so.8"}
PACKAGED_LIBS = {"librexruntime.so", "librexgpu-xenos.so", "libdxcompiler.so",
                 "libxml2.so.2", "gtav_game.so"}  # gtav_game.so: built by the installer


def check_needed(out):
    bad = []
    for p in sorted(out.rglob("*")):
        if not p.is_file() or p.read_bytes()[:4] != b"\x7fELF":
            continue
        if p.relative_to(out).parts[0] == "android":  # phone libraries (bionic), linked against only
            continue
        d = subprocess.run(["readelf", "-d", str(p)], capture_output=True, text=True).stdout
        for line in d.splitlines():
            if "(NEEDED)" in line:
                lib = line.split("[", 1)[1].rstrip("]")
                if lib not in SYSTEM_LIBS | PACKAGED_LIBS:
                    bad.append(f"{p.relative_to(out)} needs {lib}")
    if bad:
        sys.exit("shipped binaries need libraries players will not have:\n" + "\n".join(bad))


def build_xml_stub(out):
    """tools/llvm/lib/libxml2.so.2: lets the official lld start without libxml2 (see the .c)."""
    out.parent.mkdir(parents=True, exist_ok=True)
    cc = shutil.which("clang-20") or shutil.which("clang") or shutil.which("cc")
    subprocess.run([cc, "-O2", "-fPIC", "-shared", "-nostdlib", "-Wl,-soname,libxml2.so.2",
                    f"-Wl,--version-script={STUB}.map", f"{STUB}.c", "-o", str(out)], check=True)
    os.chmod(out, 0o755)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--llvm", required=True)
    ap.add_argument("--ios", help="tools/ios/make_shell.sh output (built on a Mac): the iPhone step's ios/ folder")
    ap.add_argument("--android", help="tools/android/make_shell.sh output (built on Linux): the Android step's android/ folder")
    ap.add_argument("--expected", required=True)
    ap.add_argument("--licenses", required=True)
    ap.add_argument("--compat", action="store_true")
    ap.add_argument("--tar", action="store_true")
    a = ap.parse_args()

    out = Path(a.out).resolve()
    # Only ever clear a folder this script made.
    if out.exists() and any(out.iterdir()):
        if not (out / MARKER).is_file() or len(out.parts) < 3:
            sys.exit(f"refusing to clear {out}: not an earlier release folder")
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)

    for src, rel, exe in FILES:
        copy(src, out / rel, exe)
    copy(Path(a.expected), out / "tools/game_dll/expected.txt")
    # Built against the dev tree's dxc; in the release libdxcompiler.so is in tools/lib.
    set_runpath(out / "tools/XenosRecomp", "$ORIGIN/lib")
    # No debug info (it carries this machine's build paths), like the .pdb files left out on Windows.
    ours = ["gtav_launcher", "gtav_fast", "librexruntime.so", "librexgpu-xenos.so", "rexglue",
            "tools/XenosRecomp"]
    subprocess.run(["strip", "--strip-debug"] + [str(out / f) for f in ours], check=True)

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
        for f in ("GTAV.apk", "LICENSES/simde.txt", "LICENSES/LLVM.txt", "LICENSES/DirectXShaderCompiler.txt"):
            if not (android / f).is_file():
                sys.exit(f"--android: missing {f}")
        shutil.copytree(android, out / "android")

    llvm = Path(a.llvm)
    copy(llvm / "bin" / "clang-19", out / "tools/llvm/bin/clang++", True)
    copy(llvm / "bin" / "lld", out / "tools/llvm/bin/ld.lld", True)  # lld picks ELF from the name
    shutil.copytree(llvm / "lib" / "clang" / "19" / "include", out / "tools/llvm/lib/clang/19/include")
    build_xml_stub(out / "tools/llvm/lib/libxml2.so.2")

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
    # binutils' PowerPC disassembler (SDK thirdparty/disasm) is GPL-3.0-or-later.
    copy(Path(a.licenses) / "third_party" / "GPL-3.0.txt", lic / "third_party" / "GPL-3.0.txt")
    (lic / "third_party" / "FFmpeg-NOTICE.txt").write_text(FFMPEG_NOTICE_LINUX)

    check_needed(out)

    bad = [p for p in out.rglob("*") if p.is_file() and p.suffix.lower() in FORBIDDEN_EXT]
    if bad:
        sys.exit("forbidden files in release:\n" + "\n".join(str(p) for p in bad))
    (out / MARKER).write_text("gtav-recomp release folder (made by tools/release/make_release_linux.py)\n")

    total = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    count = sum(1 for p in out.rglob("*") if p.is_file())
    print(f"{count} files, {total / 2**20:.0f} MB -> {out}")
    if a.tar:
        t = out.parent / (out.name + ".tar.gz")
        with tarfile.open(t, "w:gz", compresslevel=6) as tf:
            tf.add(out, arcname=out.name, filter=lambda ti: (setattr(ti, "uid", 0), setattr(ti, "gid", 0),
                                                             setattr(ti, "uname", ""), setattr(ti, "gname", ""), ti)[-1])
        print(f"tar {t.stat().st_size / 2**20:.0f} MB -> {t}")


if __name__ == "__main__":
    main()
