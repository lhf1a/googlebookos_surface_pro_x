#!/usr/bin/env python3
"""Verify pinned donor inputs and expose ELF/source gaps; never deploy anything."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def within(root, name):
    relative = PurePosixPath(name)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("Invalid reference-relative path: " + name)
    path = root / relative
    if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
        raise ValueError("Reference escapes input root: " + name)
    return path

def elf_dependencies(path):
    if path.read_bytes()[:4] != b"\x7fELF":
        return None
    data = subprocess.check_output(["readelf", "-dW", str(path)], text=True)
    return re.findall(r"\(NEEDED\).*\[(.*?)\]", data)

def inspect(vendor_root, source_root, config):
    pinned = {}
    for name in config["binary_manifests"]:
        manifest = json.loads((ROOT / "configs" / name).read_text())
        for relative, digest in manifest["files"].items():
            relative = relative.lstrip("/") if relative.startswith("/") else "lib64/" + relative
            if relative in pinned and pinned[relative] != digest:
                raise ValueError("Conflicting input hashes: " + relative)
            pinned[relative] = digest
    # Verify all bytes before reporting dependencies.
    for name, digest in pinned.items():
        if sha(within(vendor_root, name)) != digest:
            raise ValueError("Donor hash mismatch: " + name)
    for source in config["source_files"]:
        if sha(within(source_root, source["path"])) != source["sha256"]:
            raise ValueError("Source hash mismatch: " + source["path"])
    libraries, external = {}, set()
    by_soname = {}
    for name in pinned:
        by_soname.setdefault(PurePosixPath(name).name, []).append(name)
    for name, digest in sorted(pinned.items()):
        needed = elf_dependencies(within(vendor_root, name))
        if needed is None:
            continue
        dependencies = []
        for dependency in needed:
            if "/" in dependency:
                raise ValueError("Unexpected path-valued DT_NEEDED: " + dependency)
            candidates = sorted(by_soname.get(dependency, []))
            status = "pinned_reference_candidates" if candidates else "not_audited"
            if status == "not_audited":
                external.add(dependency)
            dependencies.append({"soname": dependency, "status": status, "candidates": candidates})
        libraries[name] = {"sha256": digest, "needed": dependencies}
    return {
        "status": "reference-audit-only", "ready_for_deployment": False,
        "binary_files_verified": len(pinned), "source_files_verified": len(config["source_files"]),
        "elf": libraries, "dependencies_not_audited": sorted(external),
        "source_pairing": config["source_pairing"], "components": config["components"],
        "limits": ["DT_NEEDED closure is not symbol/C++ ABI or linker-namespace validation",
                   "No OEM signature validation, GPU/firmware execution or deployment",
                   "Source references are not a proven exact match for the v77 binary build"],
    }

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vendor-root", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Output must be fresh")
    try:
        config = json.loads((ROOT / "configs/qualcomm-full-stack-inputs.json").read_text())
        report = inspect(args.vendor_root, args.source_root, config)
        with args.output.open("x") as file:
            json.dump(report, file, indent=2)
            file.write("\n")
        print(json.dumps({k: report[k] for k in (
            "status", "binary_files_verified", "source_files_verified", "dependencies_not_audited")}))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")

if __name__ == "__main__":
    main()
