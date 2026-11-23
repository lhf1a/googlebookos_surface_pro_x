#!/usr/bin/env python3
"""Build Surface Pro X android.hardware.biometrics.face.IFace/default (AIDL V4) HAL and CLI."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FACE_DIR = ROOT / "probe/face"
CAMERA_DIR = ROOT / "probe/camera"

DEFAULT_INTERFACES = Path(
    os.environ.get(
        "SQ2_HARDWARE_INTERFACES",
        "/mnt/spx-workspace/toolchains/sq2-c2-hardware-interfaces",
    )
)
DEFAULT_GUI_AIDL = Path(
    os.environ.get(
        "SQ2_GUI_AIDL_DIR",
        "/mnt/spx-workspace/toolchains/sq2-c2-native-reference/aidl/gui",
    )
)
DEFAULT_AIDL = Path(
    os.environ.get(
        "SQ2_AIDL_BIN",
        "/mnt/spx-workspace/toolchains/sq2-graphics/sdk/android-15/aidl",
    )
)
DEFAULT_NDK_LLVM = Path(
    os.environ.get(
        "SQ2_NDK_LLVM",
        "/mnt/spx-workspace/toolchains/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64",
    )
)
DEFAULT_BINDER_HEADERS = Path(
    os.environ.get(
        "SQ2_BINDER_HEADERS",
        "/mnt/spx-workspace/toolchains/sq2-c2-native-reference/libs/binder/ndk",
    )
)
DEFAULT_BINDER_LIB = Path(
    os.environ.get(
        "SQ2_BINDER_LIB",
        "/mnt/spx-workspace/chromebookOS/out/hybrid-finish-platform-integrated-20261116-v2/platform/lib64/libbinder_ndk.so",
    )
)

EM_AARCH64 = 183


def verify_arm64_elf(binary: Path) -> dict:
    data = binary.read_bytes()
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError(f"{binary}: expected ELF64 little-endian binary")
    e_type, e_machine = struct.unpack_from("<HH", data, 16)
    if e_machine != EM_AARCH64 or e_type not in (2, 3):
        raise ValueError(
            f"{binary}: expected AArch64 executable/PIE (got type={e_type} mach={e_machine})"
        )
    return {
        "e_machine": "EM_AARCH64",
        "e_type": "ET_DYN" if e_type == 3 else "ET_EXEC",
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def toolchains_available(
    interfaces: Path = DEFAULT_INTERFACES,
    gui_aidl: Path = DEFAULT_GUI_AIDL,
    aidl_bin: Path = DEFAULT_AIDL,
    ndk_llvm: Path = DEFAULT_NDK_LLVM,
    binder_headers: Path = DEFAULT_BINDER_HEADERS,
    binder_lib: Path = DEFAULT_BINDER_LIB,
) -> bool:
    return (
        interfaces.is_dir()
        and gui_aidl.is_dir()
        and aidl_bin.is_file()
        and (ndk_llvm / "bin/aarch64-linux-android35-clang++").is_file()
        and (binder_headers / "include_cpp").is_dir()
        and binder_lib.is_file()
    )


def load_aidl_builder():
    spec = importlib.util.spec_from_file_location(
        "build_spx_face_aidl", ROOT / "scripts/build-spx-face-aidl.py"
    )
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def build_hal(
    output: Path,
    interfaces: Path = DEFAULT_INTERFACES,
    gui_aidl: Path = DEFAULT_GUI_AIDL,
    aidl_bin: Path = DEFAULT_AIDL,
    ndk_llvm: Path = DEFAULT_NDK_LLVM,
    binder_headers: Path = DEFAULT_BINDER_HEADERS,
    binder_lib: Path = DEFAULT_BINDER_LIB,
) -> dict:
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    aidl_dir = output / "aidl"
    aidl_mod = load_aidl_builder()
    archive = aidl_mod.build_aidl(interfaces, gui_aidl, aidl_bin, ndk_llvm, binder_headers, aidl_dir)

    cxx = ndk_llvm.resolve() / "bin/aarch64-linux-android35-clang++"
    core_sources = [
        CAMERA_DIR / "spx-capture.cpp",
        FACE_DIR / "spx-ir-preprocess.cpp",
        FACE_DIR / "spx-face-crypto.cpp",
        FACE_DIR / "spx-face-engine.cpp",
        FACE_DIR / "spx-lid-monitor.cpp",
    ]
    core_objs = []
    for src in core_sources:
        obj = output / f"{src.stem}.o"
        subprocess.run(
            [
                str(cxx),
                "-std=c++20",
                "-O2",
                "-fPIE",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I" + str(FACE_DIR),
                "-I" + str(CAMERA_DIR),
                "-c",
                str(src),
                "-o",
                str(obj),
            ],
            check=True,
        )
        core_objs.append(str(obj))

    hal_bin = output / "android.hardware.biometrics.face-service.default"
    subprocess.run(
        [
            str(cxx),
            "-std=c++20",
            "-O2",
            "-fPIE",
            "-pie",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DBINDER_STABILITY_SUPPORT",
            "-static-libstdc++",
            "-I" + str(FACE_DIR),
            "-I" + str(CAMERA_DIR),
            "-I" + str(aidl_dir / "include"),
            "-I" + str(binder_headers.resolve() / "include_cpp"),
            "-I" + str(binder_headers.resolve() / "include_platform"),
            str(FACE_DIR / "spx-face-hal.cpp"),
            str(FACE_DIR / "spx-orientation-monitor.cpp"),
            *core_objs,
            str(archive),
            str(binder_lib.resolve()),
            "-lnativewindow",
            "-llog",
            "-lm",
            "-Wl,--allow-shlib-undefined",
            "-o",
            str(hal_bin),
        ],
        check=True,
    )

    cli_bin = output / "spx-face-cli"
    subprocess.run(
        [
            str(cxx),
            "-std=c++20",
            "-O2",
            "-fPIE",
            "-pie",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-static-libstdc++",
            "-I" + str(FACE_DIR),
            "-I" + str(CAMERA_DIR),
            str(FACE_DIR / "spx-face-cli.cpp"),
            *core_objs,
            "-lm",
            "-o",
            str(cli_bin),
        ],
        check=True,
    )

    strip_bin = ndk_llvm.resolve() / "bin/llvm-strip"
    if strip_bin.is_file():
        subprocess.run([str(strip_bin), "--strip-unneeded", str(hal_bin), str(cli_bin)], check=True)

    hal_elf = verify_arm64_elf(hal_bin)
    cli_elf = verify_arm64_elf(cli_bin)
    meta = {
        "binary": str(hal_bin),
        "cli_binary": str(cli_bin),
        "aidl_archive": str(archive),
        "elf": hal_elf,
        "cli_elf": cli_elf,
    }
    (output / "FACE-HAL.json").write_text(json.dumps(meta, indent=2) + "\n")
    return meta


def run_host_tests() -> None:
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("No host C++ compiler (clang++/g++) found for ASan/UBSan tests")
    test_src = ROOT / "tests/spx_face_test.cpp"
    if not test_src.is_file():
        raise FileNotFoundError(f"Missing host face test suite: {test_src}")
    with tempfile.TemporaryDirectory(prefix="spx-face-host-test-") as td:
        exe = Path(td) / "spx_face_test"
        subprocess.run(
            [
                compiler,
                "-std=c++20",
                "-O1",
                "-g",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I" + str(FACE_DIR),
                str(FACE_DIR / "spx-ir-preprocess.cpp"),
                str(FACE_DIR / "spx-face-crypto.cpp"),
                str(FACE_DIR / "spx-face-engine.cpp"),
                str(FACE_DIR / "spx-lid-monitor.cpp"),
                str(test_src),
                "-pthread",
                "-lm",
                "-o",
                str(exe),
            ],
            check=True,
        )
        subprocess.run([str(exe)], check=True)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, default=ROOT / "out/spx-face-hal")
    p.add_argument("--interfaces", type=Path, default=DEFAULT_INTERFACES)
    p.add_argument("--gui-aidl", type=Path, default=DEFAULT_GUI_AIDL)
    p.add_argument("--aidl", type=Path, default=DEFAULT_AIDL)
    p.add_argument("--ndk-llvm", type=Path, default=DEFAULT_NDK_LLVM)
    p.add_argument("--binder-headers", type=Path, default=DEFAULT_BINDER_HEADERS)
    p.add_argument("--binder-lib", type=Path, default=DEFAULT_BINDER_LIB)
    p.add_argument("--run-host-tests", action="store_true")
    a = p.parse_args()
    if a.run_host_tests:
        run_host_tests()
        return
    meta = build_hal(
        a.output, a.interfaces, a.gui_aidl, a.aidl, a.ndk_llvm, a.binder_headers, a.binder_lib
    )
    print(f"Built {meta['binary']} ({meta['elf']['e_machine']} {meta['elf']['size']} bytes)")


if __name__ == "__main__":
    main()
