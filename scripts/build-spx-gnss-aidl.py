#!/usr/bin/env python3
"""Build frozen AOSP android.hardware.gnss V4 AIDL C++ NDK bindings."""
import argparse
import concurrent.futures
import os
from pathlib import Path
import subprocess

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
GNSS_REVISION = "1a56e38edc2f2f6189ef405ee1edce554e15cbc0"
GNSS_API_SUBPATH = "gnss/aidl/aidl_api/android.hardware.gnss/4"


def extract_aidl_api(interfaces_repo: Path, dst: Path) -> Path:
    api_dir = interfaces_repo / GNSS_API_SUBPATH
    if api_dir.is_dir():
        return api_dir
    extracted = dst / "api"
    extracted.mkdir(parents=True, exist_ok=True)
    archive = subprocess.Popen(
        [
            "git",
            "-C",
            str(interfaces_repo),
            "archive",
            GNSS_REVISION,
            GNSS_API_SUBPATH,
        ],
        stdout=subprocess.PIPE,
    )
    subprocess.run(["tar", "-xf", "-", "-C", str(extracted)], stdin=archive.stdout, check=True)
    archive.stdout.close()
    if archive.wait():
        raise RuntimeError(f"Failed to extract {GNSS_API_SUBPATH} from {interfaces_repo}")
    return extracted / GNSS_API_SUBPATH


def build_aidl(interfaces: Path, aidl_bin: Path, ndk_llvm: Path, binder_headers: Path, out: Path) -> Path:
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    api_dir = extract_aidl_api(interfaces.resolve(), out)
    hash_file = api_dir / ".hash"
    if not hash_file.is_file():
        raise FileNotFoundError(f"Missing frozen AIDL hash file: {hash_file}")
    digest = hash_file.read_text().strip().splitlines()[-1]
    if len(digest) != 40:
        raise ValueError(f"Unexpected AIDL hash format in {hash_file}: {digest!r}")

    src_dir = out / "src"
    inc_dir = out / "include"
    for src in sorted(api_dir.rglob("*.aidl")):
        subprocess.run(
            [
                str(aidl_bin.resolve()),
                "--lang=ndk",
                "--structured",
                "--stability=vintf",
                "--version=4",
                "--hash=" + digest,
                "--out=" + str(src_dir),
                "--header_out=" + str(inc_dir),
                "-I" + str(api_dir),
                str(src),
            ],
            check=True,
        )

    compiler = ndk_llvm.resolve() / "bin/aarch64-linux-android35-clang++"
    cpp_srcs = sorted(src_dir.rglob("*.cpp"))
    if not cpp_srcs:
        raise RuntimeError("No AIDL C++ sources generated")
    flags = [
        str(compiler),
        "-std=c++20",
        "-O2",
        "-fPIC",
        "-DBINDER_STABILITY_SUPPORT",
        "-Wno-deprecated-anon-enum-enum-conversion",
        "-I" + str(inc_dir),
        "-I" + str(binder_headers.resolve() / "include_cpp"),
        "-I" + str(binder_headers.resolve() / "include_platform"),
    ]

    def compile_one(item):
        idx, src = item
        obj = out / f"{idx}.o"
        subprocess.run([*flags, "-c", str(src), "-o", str(obj)], check=True)
        return str(obj)

    workers = min(len(cpp_srcs), max(1, (os.cpu_count() or 2) - 1))
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        objects = list(pool.map(compile_one, enumerate(cpp_srcs)))

    archive = out / "libspx-gnss-aidl.a"
    subprocess.run([str(ndk_llvm.resolve() / "bin/llvm-ar"), "rcs", str(archive), *objects], check=True)
    return archive


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--interfaces", type=Path, default=DEFAULT_INTERFACES)
    p.add_argument("--aidl", type=Path, default=DEFAULT_AIDL)
    p.add_argument("--ndk-llvm", type=Path, default=DEFAULT_NDK_LLVM)
    p.add_argument("--binder-headers", type=Path, default=DEFAULT_BINDER_HEADERS)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    archive = build_aidl(a.interfaces, a.aidl, a.ndk_llvm, a.binder_headers, a.output)
    print(f"Built {archive}")


if __name__ == "__main__":
    main()
