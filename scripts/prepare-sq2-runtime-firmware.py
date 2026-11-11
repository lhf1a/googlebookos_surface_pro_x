#!/usr/bin/env python3
"""Materialize the two existing QCA firmware aliases on a fresh runtime copy.

No firmware version or bytes are changed: each alias becomes its existing target
bytes. Unknown/outside/directory aliases are rejected, not silently followed.
Private firmware remains local; this tool never stages or activates a device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import stat

ALIASES = {"qca/crbtfw01.tlv": "crbtfw21.tlv", "qca/crnv01.bin": "crnv21.bin"}


def snapshot(root):
    result = {}
    for p in sorted(root.rglob("*")):
        name = p.relative_to(root).as_posix()
        mode = stat.S_IMODE(p.lstat().st_mode)
        if p.is_symlink():
            result[name] = dict(type="link", target=str(p.readlink()), mode=mode)
        elif p.is_file():
            result[name] = dict(type="file", sha256=hashlib.sha256(p.read_bytes()).hexdigest(), mode=mode)
        elif p.is_dir():
            result[name] = dict(type="dir", mode=mode)
        else:
            raise ValueError("Unsupported firmware input: " + name)
    return result


def prepare(source, output):
    if source.is_symlink():
        raise ValueError("Firmware root must not be a symlink")
    source, output = source.resolve(), output.absolute()
    if not source.is_dir() or output.exists() or output.is_symlink():
        raise ValueError("Use a firmware directory and fresh output directory")
    if output.resolve().is_relative_to(source):
        raise ValueError("Output must not be inside source")
    before = snapshot(source)
    aliases = {}
    for name, value in before.items():
        if value["type"] != "link":
            continue
        if name not in ALIASES or value["target"] != ALIASES[name]:
            raise ValueError("Unapproved firmware alias: " + name)
        target = source / Path(name).parent / value["target"]
        if target.is_symlink() or not target.is_file() or not target.resolve().is_relative_to(source):
            raise ValueError("Alias target must be an inside-root regular file: " + name)
        aliases[name] = dict(target=value["target"], sha256=hashlib.sha256(target.read_bytes()).hexdigest())
    shutil.copytree(source, output, symlinks=True)
    for name, value in aliases.items():
        p = output / name
        # Only these validated copied links are removed; source is read-only.
        if not p.is_symlink() or str(p.readlink()) != value["target"]:
            raise ValueError("Copied alias changed: " + name)
        p.unlink()
        shutil.copy2(source / Path(name).parent / value["target"], p)
    after = snapshot(output)
    for name, old in before.items():
        if old["type"] != "link":
            if after[name] != old:
                raise ValueError("Firmware bytes/metadata changed: " + name)
        elif after[name]["type"] != "file" or after[name]["sha256"] != aliases[name]["sha256"]:
            raise ValueError("Alias bytes changed: " + name)
    if before != snapshot(source):
        raise ValueError("Source changed during construction")
    return dict(status="runtime-copy-normalized-not-device-validated", source=str(source), output=str(output),
                aliases_materialized=aliases, original_unchanged=True,
                firmware_version_changed=False, before=before, after=after)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--report", type=Path, required=True)
    args = p.parse_args()
    if args.report.is_symlink() or args.report.resolve().is_relative_to(args.source.resolve()):
        p.error("Report must not modify the firmware source")
    result = prepare(args.source, args.output)
    args.report.write_text(json.dumps(result, indent=2) + "\n")
    print(result["status"], "aliases=", len(result["aliases_materialized"]), args.output.absolute())
