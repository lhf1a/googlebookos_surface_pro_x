#!/usr/bin/env python3
"""Compile the supplied downstream MSM UAPI with a real prepared ARM64 kernel.

Records the actual compiler status and extracts sizeof/ioctl constants from the
object. This is not a full SDE build, an ioctl execution, or hardware validation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--header", required=True, type=Path)
    p.add_argument("--kernel-build", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path, help="fresh directory")
    p.add_argument("--reference-core-dir", type=Path,
                   help="reference only: resolve pristine quoted drm.h using this modern UAPI dir")
    a = p.parse_args()
    a.output = a.output.absolute()
    a.kernel_build = a.kernel_build.resolve()
    release = (a.kernel_build / "include/config/kernel.release").read_text().strip()
    if not release.startswith("6.18."):
        raise ValueError("Expected prepared Linux 6.18 build")
    data = a.header.read_bytes()
    # Ignore placeholder declarations within comments.
    text = re.sub(r"/\*.*?\*/", "", data.decode(), flags=re.S)
    structs = re.findall(r"^struct (drm_msm_\w+) \{", text, re.M)
    ioctls = re.findall(r"^#define (DRM_IOCTL_MSM_\w+)\s", text, re.M)
    if not structs or not ioctls:
        raise ValueError("Missing downstream UAPI declarations")
    expressions = ["sizeof(struct " + n + ")" for n in structs] + ioctls
    a.output.mkdir(parents=True, exist_ok=False)
    header = a.output / "include/uapi/drm/msm_drm.h"
    header.parent.mkdir(parents=True)
    header.write_bytes(data)
    (a.output / "uapi_probe.c").write_text(
        '#include <linux/compiler_attributes.h>\n#include <drm/msm_drm.h>\n'
        'const unsigned long long sde_uapi_abi[] __used __section(".sde_uapi_abi") = {\n'
        + ",\n".join(expressions) + "\n};\n")
    kbuild = "obj-m := uapi_probe.o\nLINUXINCLUDE := -I$(src)/include/uapi $(LINUXINCLUDE)\n"
    if a.reference_core_dir:
        kbuild += "ccflags-y += -I" + str(a.reference_core_dir.resolve()) + "\n"
    (a.output / "Kbuild").write_text(kbuild)
    runs = []

    def run(cmd):
        result = subprocess.run(cmd, capture_output=True, text=True)
        runs.append(dict(command=cmd, exit=result.returncode,
                         stdout=result.stdout, stderr=result.stderr))
        return result.returncode

    status = run(["make", "-C", str(a.kernel_build), "M=" + str(a.output),
                  "ARCH=arm64", "CROSS_COMPILE=aarch64-linux-gnu-", "KCFLAGS=-Werror",
                  "uapi_probe.o"])
    if status == 0:
        dep = (a.output / ".uapi_probe.o.cmd").read_text()
        if "include/uapi/drm/msm_drm.h" not in dep:
            raise ValueError("Missing dependency on supplied UAPI")
        status = run(["aarch64-linux-gnu-objcopy", "--dump-section",
                      ".sde_uapi_abi=" + str(a.output / "ABI.bin"),
                      str(a.output / "uapi_probe.o")])
        if status == 0 and (a.output / "ABI.bin").stat().st_size != 8 * len(expressions):
            raise ValueError("Unexpected ARM64 ABI record size")
    report = dict(header=str(a.header.resolve()), header_sha256=sha(a.header),
                  kernel_release=release, expressions=expressions, runs=runs, exit=status,
                  reference_only=bool(a.reference_core_dir), hardware_validated=False)
    if status == 0:
        report["abi_sha256"] = sha(a.output / "ABI.bin")
    (a.output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    print("UAPI ARM64 compile", status, "constants", len(expressions))
    return status


if __name__ == "__main__":
    raise SystemExit(main())
