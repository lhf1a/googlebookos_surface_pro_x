#!/usr/bin/env python3
"""Build Surface Pro X QRTR QMI LOC GNSS daemon (ARM64) and run host tests."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
GNSS_DIR = ROOT / "probe/gnss"

LIB_SOURCES = [
    GNSS_DIR / "spx-qmi-loc.c",
    GNSS_DIR / "spx-nmea.c",
    GNSS_DIR / "spx-gnss-session.c",
]
DAEMON_SOURCES = [GNSS_DIR / "spx-gnss-daemon.c", *LIB_SOURCES]
HOST_TEST_SOURCES = [ROOT / "tests/spx_gnss_test.c", *LIB_SOURCES]

EM_AARCH64 = 183
ET_EXEC = 2
ET_DYN = 3


def verify_aarch64_elf(binary: Path) -> dict:
    """Verify binary is a valid 64-bit little-endian AArch64 ELF executable."""
    data = binary.read_bytes()
    if len(data) < 64:
        raise ValueError(f"{binary}: file too small to be ELF ({len(data)} bytes)")
    if data[:4] != b"\x7fELF":
        raise ValueError(f"{binary}: missing ELF magic")
    ei_class, ei_data, ei_version = data[4], data[5], data[6]
    if ei_class != 2:
        raise ValueError(f"{binary}: expected ELF64 (class=2), got {ei_class}")
    if ei_data != 1:
        raise ValueError(f"{binary}: expected little-endian ELF (data=1), got {ei_data}")
    if ei_version != 1:
        raise ValueError(f"{binary}: unexpected ELF version {ei_version}")
    e_type, e_machine = struct.unpack_from("<HH", data, 16)
    if e_machine != EM_AARCH64:
        raise ValueError(
            f"{binary}: expected e_machine=EM_AARCH64 ({EM_AARCH64}), got {e_machine}"
        )
    if e_type not in (ET_EXEC, ET_DYN):
        raise ValueError(f"{binary}: expected executable/PIE ELF, got e_type={e_type}")
    return {
        "elf_class": "ELF64",
        "endian": "little",
        "e_type": "ET_DYN" if e_type == ET_DYN else "ET_EXEC",
        "e_machine": "EM_AARCH64",
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def resolve_cc(cc: str) -> str:
    resolved = shutil.which(cc)
    if not resolved:
        raise FileNotFoundError(f"Compiler not found in PATH: {cc}")
    return resolved


def build_arm64(output: Path, cc: str = "aarch64-linux-gnu-gcc",
                static: bool = False) -> dict:
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    compiler = resolve_cc(cc)
    binary = output / "spx-gnss-daemon"
    cmd = [
        compiler,
        "-std=c11",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(GNSS_DIR),
    ]
    if static:
        cmd.append("-static")
    cmd.extend(str(p) for p in DAEMON_SOURCES)
    cmd.extend(["-lm", "-o", str(binary)])
    subprocess.run(cmd, check=True)
    elf_info = verify_aarch64_elf(binary)
    meta = {
        "binary": str(binary),
        "command": cmd,
        "elf": elf_info,
    }
    (output / "BUILD.json").write_text(json.dumps(meta, indent=2) + "\n")
    return meta


def run_host_tests(cc: str = "gcc") -> subprocess.CompletedProcess:
    compiler = resolve_cc(cc)
    with tempfile.TemporaryDirectory(prefix="spx-gnss-test-") as tmp:
        exe = Path(tmp) / "spx_gnss_test"
        cmd = [
            compiler,
            "-std=c11",
            "-O1",
            "-g",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-I",
            str(GNSS_DIR),
            *(str(p) for p in HOST_TEST_SOURCES),
            "-lm",
            "-o",
            str(exe),
        ]
        subprocess.run(cmd, check=True)
        return subprocess.run([str(exe)], check=True, text=True,
                              capture_output=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--target",
        choices=("arm64", "host-test", "all"),
        default="arm64",
        help="Build ARM64 daemon, run ASan/UBSan host tests, or both.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "out/spx-gnss",
        help="Output directory for ARM64 spx-gnss-daemon binary.",
    )
    parser.add_argument(
        "--cc",
        default="aarch64-linux-gnu-gcc",
        help="Cross-compiler for ARM64 target (e.g. aarch64-linux-gnu-gcc or NDK clang).",
    )
    parser.add_argument(
        "--host-cc",
        default="gcc",
        help="Host C compiler for ASan/UBSan unit tests.",
    )
    parser.add_argument(
        "--static",
        action="store_true",
        help="Link ARM64 binary statically.",
    )
    args = parser.parse_args()

    if args.target in ("host-test", "all"):
        res = run_host_tests(args.host_cc)
        print(res.stdout, end="")
    if args.target in ("arm64", "all"):
        meta = build_arm64(args.output, cc=args.cc, static=args.static)
        print(
            f"Built {meta['binary']} ({meta['elf']['e_machine']} "
            f"{meta['elf']['e_type']}, sha256={meta['elf']['sha256'][:16]}...)"
        )


if __name__ == "__main__":
    main()
