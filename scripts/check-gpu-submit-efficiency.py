#!/usr/bin/env python3
"""Reconstruct and test the saved, opt-in v5 source checkpoint; never deploy.

Input hashes identify this historical test fixture, not product/boot eligibility.
Requires Python 3, patch, clang (ASan/UBSan) and a matching Linux source tree.
The input tree is read-only. Output must be a new directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "configs/gpu-submit-efficiency-v5.json"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def checked(path, expected):
    data = path.read_bytes()
    if digest(data) != expected:
        raise ValueError(f"Saved source fixture differs: {path}")
    return data


def run(command, output, **kwargs):
    p = subprocess.run(command, capture_output=True, text=True, **kwargs)
    record = dict(command=command, stdout=p.stdout, stderr=p.stderr, exit=p.returncode)
    with (output / "VERIFICATION.txt").open("a", encoding="utf-8") as f:
        f.write(json.dumps(record, indent=2) + "\n")
    print(p.stdout, end="", flush=True)
    print(p.stderr, end="", file=sys.stderr, flush=True)
    if p.returncode:
        raise RuntimeError(f"Command exit {p.returncode}: {command}")


def prepare(kernel, output):
    config = json.loads(CONFIG.read_text(encoding="utf-8"))
    if output.exists():
        raise FileExistsError(f"Use a fresh output directory: {output}")
    # Preflight every source before creating output; never write into kernel.
    source = {e["path"]: checked(kernel / e["path"], e["baseline_sha256"])
              for e in config["files"]}
    support = config["test_support"]
    checked(kernel / support["path"], support["sha256"])
    patch = checked(ROOT / config["patch"], config["patch_sha256"])
    incremental = checked(ROOT / config["incremental_patch"], config["incremental_patch_sha256"])
    output.mkdir(parents=True)
    tree = output / "modified-tree"
    parent = output / "v4-tree"
    for entry in config["files"]:
        p = tree / entry["path"]
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(source[entry["path"]])
        (output / entry["baseline_role"]).write_bytes(source[entry["path"]])
    run(["patch", "--batch", "--fuzz=0", "-d", str(tree), "-p1", "-i", str(ROOT / config["patch"])], output)
    shutil.copytree(tree, parent)
    run(["patch", "--batch", "--fuzz=0", "-R", "-d", str(parent), "-p1", "-i",
         str(ROOT / config["incremental_patch"])], output)
    archive = output / "archive-combined-v4"
    archive.mkdir()
    flat_patch = patch.decode("utf-8")
    for entry in config["files"]:
        data = checked(tree / entry["path"], entry["v5_sha256"])
        (output / entry["modified_role"]).write_bytes(data)
        data = checked(parent / entry["path"], entry["v4_sha256"])
        (archive / entry["modified_role"]).write_bytes(data)
        for side in ("a", "b"):
            flat_patch = flat_patch.replace(f"{side}/{entry['path']}\n", f"{side}/{entry['flat_name']}\n")
    (output / "DIFF_FILE.patch").write_text(flat_patch, encoding="utf-8")
    rollback = output / "ROLLBACK.sh"
    rollback.write_text('''#!/bin/sh
set -eu
[ "$#" = 1 ] || [ "$#" = 2 ] || exit 64
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "${2:-source}" in
 source) B=BASELINE.c ;;
 submit) B=BASELINE.submit.c ;;
 scheduler) B=BASELINE.scheduler.c ;;
 exec) B=BASELINE.exec.c ;;
 exec-header) B=BASELINE.exec.h ;;
 *) exit 64 ;;
esac
cp "$D/$B" "$1"
''', encoding="utf-8")
    rollback.chmod(0o755)
    return config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    kernel, output = args.kernel_source.resolve(), args.output.resolve()
    # A test output directory must not become part of the original source tree.
    if output == kernel or kernel in output.parents:
        parser.error("Output must be outside the input source tree")
    config = prepare(kernel, output)
    if not args.prepare_only:
        env = dict(os.environ, GPU_SUBMIT_TEST_DIR=str(output), GPU_SUBMIT_KERNEL_SOURCE=str(kernel))
        fixtures = ROOT / "tests/fixtures/gpu-submit-efficiency-v5"
        for name in ("vma", "descriptor_scheduler", "mapping_exec", "walk", "errors"):
            run([sys.executable, str(fixtures / (name + ".py"))], output, env=env)
    # Read back both original and candidate after all rollback/reapply fixtures.
    for entry in config["files"]:
        checked(kernel / entry["path"], entry["baseline_sha256"])
        checked(output / entry["modified_role"], entry["v5_sha256"])
    report = dict(revision="combined-v5", prepared=True, host_tests_run=not args.prepare_only,
                  original_sources_unchanged=True, candidate_exact=True,
                  device_tests_run=False, deployment_performed=False)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
