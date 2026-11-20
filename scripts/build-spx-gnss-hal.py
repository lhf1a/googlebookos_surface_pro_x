#!/usr/bin/env python3
"""Build Surface Pro X android.hardware.gnss.IGnss/default (AIDL V4) HAL binary."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GNSS_DIR = ROOT / "probe/gnss"

DEFAULT_INTERFACES = Path(
    os.environ.get(
        "SQ2_HARDWARE_INTERFACES",
        "/mnt/spx-workspace/toolchains/sq2-c2-hardware-interfaces",
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
        raise ValueError(f"{binary}: expected AArch64 executable/PIE (got type={e_type} mach={e_machine})")
    return {
        "e_machine": "EM_AARCH64",
        "e_type": "ET_DYN" if e_type == 3 else "ET_EXEC",
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def toolchains_available(
    interfaces: Path = DEFAULT_INTERFACES,
    aidl_bin: Path = DEFAULT_AIDL,
    ndk_llvm: Path = DEFAULT_NDK_LLVM,
    binder_headers: Path = DEFAULT_BINDER_HEADERS,
    binder_lib: Path = DEFAULT_BINDER_LIB,
) -> bool:
    return (
        interfaces.is_dir()
        and aidl_bin.is_file()
        and (ndk_llvm / "bin/aarch64-linux-android35-clang++").is_file()
        and (binder_headers / "include_cpp").is_dir()
        and binder_lib.is_file()
    )


def load_aidl_builder():
    spec = importlib.util.spec_from_file_location(
        "build_spx_gnss_aidl", ROOT / "scripts/build-spx-gnss-aidl.py"
    )
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def build_hal(
    output: Path,
    interfaces: Path = DEFAULT_INTERFACES,
    aidl_bin: Path = DEFAULT_AIDL,
    ndk_llvm: Path = DEFAULT_NDK_LLVM,
    binder_headers: Path = DEFAULT_BINDER_HEADERS,
    binder_lib: Path = DEFAULT_BINDER_LIB,
) -> dict:
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    aidl_dir = output / "aidl"
    aidl_mod = load_aidl_builder()
    archive = aidl_mod.build_aidl(interfaces, aidl_bin, ndk_llvm, binder_headers, aidl_dir)

    cc = ndk_llvm.resolve() / "bin/aarch64-linux-android35-clang"
    cxx = ndk_llvm.resolve() / "bin/aarch64-linux-android35-clang++"

    c_objs = []
    for name in ("spx-qmi-loc.c", "spx-nmea.c"):
        obj = output / f"{Path(name).stem}.o"
        subprocess.run(
            [
                str(cc),
                "-std=c11",
                "-O2",
                "-fPIE",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I" + str(GNSS_DIR),
                "-c",
                str(GNSS_DIR / name),
                "-o",
                str(obj),
            ],
            check=True,
        )
        c_objs.append(str(obj))

    hal_bin = output / "android.hardware.gnss-service.example"
    cmd = [
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
        "-I" + str(GNSS_DIR),
        "-I" + str(aidl_dir / "include"),
        "-I" + str(binder_headers.resolve() / "include_cpp"),
        "-I" + str(binder_headers.resolve() / "include_platform"),
        str(GNSS_DIR / "spx-gnss-hal.cpp"),
        *c_objs,
        str(archive),
        str(binder_lib.resolve()),
        "-llog",
        "-lm",
        "-Wl,--allow-shlib-undefined",
        "-o",
        str(hal_bin),
    ]
    subprocess.run(cmd, check=True)
    strip_bin = ndk_llvm.resolve() / "bin/llvm-strip"
    if strip_bin.is_file():
        subprocess.run([str(strip_bin), "--strip-unneeded", str(hal_bin)], check=True)
    elf_info = verify_arm64_elf(hal_bin)
    meta = {
        "binary": str(hal_bin),
        "aidl_archive": str(archive),
        "elf": elf_info,
    }
    (output / "GNSS-HAL.json").write_text(json.dumps(meta, indent=2) + "\n")
    return meta


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, default=ROOT / "out/spx-gnss-hal")
    p.add_argument("--interfaces", type=Path, default=DEFAULT_INTERFACES)
    p.add_argument("--aidl", type=Path, default=DEFAULT_AIDL)
    p.add_argument("--ndk-llvm", type=Path, default=DEFAULT_NDK_LLVM)
    p.add_argument("--binder-headers", type=Path, default=DEFAULT_BINDER_HEADERS)
    p.add_argument("--binder-lib", type=Path, default=DEFAULT_BINDER_LIB)
    a = p.parse_args()
    meta = build_hal(a.output, a.interfaces, a.aidl, a.ndk_llvm, a.binder_headers, a.binder_lib)
    print(f"Built {meta['binary']} ({meta['elf']['e_machine']} {meta['elf']['size']} bytes)")


if __name__ == "__main__":
    main()
