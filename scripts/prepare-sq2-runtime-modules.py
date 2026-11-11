#!/usr/bin/env python3
"""Normalize only non-runtime links/cache on a fresh matching-module COPY.

Never follows build/source, changes a .ko, builds, inserts, or stages a device.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil

SPEC = importlib.util.spec_from_file_location("runtime_snapshot", Path(__file__).with_name("prepare-sq2-runtime-firmware.py"))
SNAP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SNAP)
LINKS = {"build", "source"}
CACHES = {"modules.dep.bb", "modules.dep.bb.new"}


def prepare(source, output):
    if source.is_symlink():
        raise ValueError("Module root must not be a symlink")
    source, output = source.resolve(), output.absolute()
    if not source.is_dir() or output.exists() or output.is_symlink():
        raise ValueError("Use a module directory and fresh output directory")
    if output.resolve().is_relative_to(source):
        raise ValueError("Output must not be inside source")
    before = SNAP.snapshot(source)
    removed = {}
    for name, item in before.items():
        if item["type"] == "link":
            if name not in LINKS:
                raise ValueError("Unapproved module tree link: " + name)
            removed[name] = item
        elif name in LINKS:
            raise ValueError("Non-runtime link has unexpected type: " + name)
        elif name in CACHES:
            if item["type"] != "file":
                raise ValueError("Cache has unexpected type: " + name)
            removed[name] = item
    shutil.copytree(source, output, symlinks=True)
    for name in removed:
        (output / name).unlink()
    after = SNAP.snapshot(output)
    if after != {n: value for n, value in before.items() if n not in removed}:
        raise ValueError("Runtime module bytes/metadata changed")
    if before != SNAP.snapshot(source):
        raise ValueError("Source changed during construction")
    return dict(status="runtime-copy-normalized-not-device-validated", source=str(source), output=str(output),
                removed=removed, source_unchanged=True, before=before, after=after,
                modules_unchanged=sum(n.endswith(".ko") and v["type"] == "file" for n, v in after.items()))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--report", type=Path, required=True)
    args = p.parse_args()
    if args.report.is_symlink() or args.report.resolve().is_relative_to(args.source.resolve()):
        p.error("Report must not modify the module source")
    result = prepare(args.source, args.output)
    args.report.write_text(json.dumps(result, indent=2) + "\n")
    print(result["status"], "modules=", result["modules_unchanged"], args.output.absolute())
