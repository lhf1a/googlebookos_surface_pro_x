#!/usr/bin/env python3
"""Prepare the pinned kernel provider prerequisite in a fresh tree; never deploy."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(source, output, config=None):
    source, output = source.resolve(), output.absolute()
    pins = json.loads((config or ROOT / "configs/kgsl-618-smmu-provider.json").read_text())
    patch = ROOT / pins["patch"]
    if sha(patch) != pins["sha256"]:
        raise ValueError("Unrecognized provider patch")
    contents = {}
    for name, hashes in pins["files"].items():
        relative = PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("Unsafe provider source path: " + name)
        path = source / name
        if path.is_symlink() or not path.resolve().is_relative_to(source):
            raise ValueError("Unsafe provider source: " + name)
        if hashes["before"] is None:
            if path.exists():
                raise ValueError("Provider addition already exists: " + name)
            continue
        if sha(path) != hashes["before"]:
            raise ValueError("Unrecognized provider source: " + name)
        contents[name] = path.read_bytes()
    output.mkdir(parents=True, exist_ok=False)
    for name, data in contents.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)], cwd=output, check=True)
    for name, hashes in pins["files"].items():
        if sha(output / name) != hashes["after"]:
            raise ValueError("Unexpected patched provider source: " + name)
    (output / "SOURCE.json").write_text(json.dumps(pins, indent=2) + "\n")
    return pins


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Matching full Linux 6.18 kernel source")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--config", type=Path, help="Pinned provider config; defaults to SMMU")
    args = parser.parse_args()
    print(prepare(args.source, args.output, args.config)["status"])
