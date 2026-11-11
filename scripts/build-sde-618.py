#!/usr/bin/env python3
"""Compile prepared downstream SDE against an explicit prepared Linux 6.18 build.

Preserves actual failure status. Never deploys, ignores MODPOST failures, copies
old core headers, or claims source/compile checks prove hardware functionality.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(source, kernel_build, output, jobs=8, cross_compile="aarch64-linux-gnu-"):
    source, kernel_build, output = source.resolve(), kernel_build.resolve(), output.absolute()
    if output.exists():
        raise ValueError("Use a fresh evidence directory")
    report = json.loads((source / "SOURCE.json").read_text())
    for name, expected in {**report["sha256"], **report["generated"]}.items():
        if sha(source / name) != expected:
            raise ValueError("Prepared source changed: " + name)
    release = (kernel_build / "include/config/kernel.release").read_text().strip()
    if not release.startswith("6.18."):
        raise ValueError("Expected explicit prepared 6.18 kernel")
    kernel_inputs = {name: sha(kernel_build / name) for name in
                     (".config", "Module.symvers", "include/config/kernel.release")}
    output.mkdir(parents=True, exist_ok=False)
    cmd = ["make", "-C", str(kernel_build), "M=" + str(source / "techpack/display/msm"),
           "ARCH=arm64", "CROSS_COMPILE=" + cross_compile, "KCFLAGS=-Werror",
           "-j" + str(jobs), "-k", "modules"]
    run = subprocess.run(cmd, capture_output=True, text=True)
    (output / "stdout.txt").write_text(run.stdout)
    (output / "stderr.txt").write_text(run.stderr)
    result = dict(status="compiled-not-hardware-validated" if run.returncode == 0 else "compile-failed",
                  command=cmd, exit=run.returncode, stdout=run.stdout, stderr=run.stderr,
                  source_manifest_sha256=sha(source / "SOURCE.json"),
                  kernel_build=str(kernel_build), kernel_inputs=kernel_inputs,
                  objects=[str(p.relative_to(source)) for p in sorted(source.rglob("*.o"))],
                  modules={str(p.relative_to(source)): sha(p) for p in sorted(source.rglob("*.ko"))},
                  hardware_validated=False, deployment_ready=False)
    (output / "RESULT.json").write_text(json.dumps(result, indent=2) + "\n")
    print(result["status"], "exit=" + str(run.returncode), "objects=" + str(len(result["objects"])))
    return run.returncode


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--kernel-build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=8, choices=range(1, 65))
    parser.add_argument("--cross-compile", default="aarch64-linux-gnu-")
    a = parser.parse_args()
    raise SystemExit(build(a.source, a.kernel_build, a.output, a.jobs, a.cross_compile))
