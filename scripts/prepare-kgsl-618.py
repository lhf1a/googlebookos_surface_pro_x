#!/usr/bin/env python3
"""Prepare pinned KGSL sources for the ongoing 6.18 port; never deploy or claim a runnable module."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(source, output, config=None):
    source, output = source.resolve(), output.absolute()
    if output.exists():
        raise ValueError("Use a fresh output directory")
    config = config or ROOT / "configs/kgsl-618-port.json"
    port = json.loads(config.read_text())
    donor = json.loads((ROOT / "configs" / port["source_manifest"]).read_text())
    names = {name: digest for name, digest in donor["sha256"].items()
             if name.startswith("drivers/gpu/msm/") or name in
             ("include/linux/msm_kgsl.h", "include/uapi/linux/msm_kgsl.h")}
    # Only copy verified KGSL/UAPI, never stale objects or downstream core headers.
    contents = {}
    for name, digest in names.items():
        path = source / name
        if path.is_symlink() or not path.resolve().is_relative_to(source) or sha(path) != digest:
            raise ValueError("Unrecognized donor source: " + name)
        contents[name] = path.read_bytes()
    for item in port["patches"]:
        if sha(ROOT / item["path"]) != item["sha256"]:
            raise ValueError("Unrecognized patch: " + item["path"])
    output.mkdir(parents=True, exist_ok=False)
    for name, data in contents.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    prepared_names = set(names)
    for item in port["patches"]:
        for name, hashes in item["files"].items():
            if hashes["before"] is None:
                path = output / name
                if not name.startswith("drivers/gpu/msm/") or ".." in Path(name).parts:
                    raise ValueError("Unexpected added source path: " + name)
                if path.exists() or path.is_symlink():
                    raise ValueError("Added source already exists: " + name)
                continue
            if sha(output / name) != hashes["before"]:
                raise ValueError("Unexpected pre-patch source: " + name)
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(ROOT / item["path"])],
                       cwd=output, check=True)
        for name, hashes in item["files"].items():
            if sha(output / name) != hashes["after"]:
                raise ValueError("Unexpected patched source: " + name)
            prepared_names.add(name)
    report = dict(status=port["status"], commit=donor["commit"], patches=port["patches"],
                  sha256={name: sha(output / name) for name in sorted(prepared_names)})
    (output / "SOURCE.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Verified complete donor kernel directory")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--config", type=Path, help="Pinned port config; defaults to the Oculus candidate")
    args = parser.parse_args()
    report = prepare(args.source, args.output, args.config)
    print(report["status"], len(report["sha256"]), args.output)
