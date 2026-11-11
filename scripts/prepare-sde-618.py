#!/usr/bin/env python3
"""Verify and stage complete pinned downstream SDE sources for 6.18 porting.

The generated external Kbuild is a full-source API diagnostic, not a target
hardware preset. No legacy DRM core headers, provider stubs or device operations.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def safe_source(source, name):
    relative = PurePosixPath(name)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("Unsafe source path: " + name)
    path = source / name
    if path.is_symlink() or not path.resolve().is_relative_to(source):
        raise ValueError("Unsafe source: " + name)
    return path


def prepare(source, output, config=None):
    source, output = source.resolve(), output.absolute()
    config = config or ROOT / "configs/sde-xiaomi-618-port.json"
    pins = json.loads(config.read_text())
    donor = json.loads((ROOT / "configs" / pins["source_manifest"]).read_text())
    names = {name: digest for name, digest in donor["sha256"].items()
             if name.startswith(pins["source_prefix"])}
    names.update(pins["additional_sources"])
    if output.exists():
        raise ValueError("Use a fresh output directory")
    contents = {}
    for name, expected in names.items():
        path = safe_source(source, name)
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError("Unrecognized source: " + name)
        contents[name] = data
    for item in pins["patches"]:
        if sha(ROOT / item["path"]) != item["sha256"]:
            raise ValueError("Unrecognized patch: " + item["path"])
    # A diagnostic input set must still contain the actual driver and UAPI.
    for name in ("techpack/display/msm/Makefile", "techpack/display/msm/msm_drv.c",
                 "techpack/display/msm/sde/sde_hw_catalog.c", "include/uapi/drm/msm_drm.h"):
        if name not in contents:
            raise ValueError("Incomplete display source set: " + name)
    output.mkdir(parents=True, exist_ok=False)
    for name, data in contents.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    for item in pins["patches"]:
        for name, hashes in item["files"].items():
            path = safe_source(output.resolve(), name)
            if hashes["before"] is None:
                if not name.startswith(pins["source_prefix"]) or path.exists():
                    raise ValueError("Unexpected added source: " + name)
                continue
            if name not in names or sha(path) != hashes["before"]:
                raise ValueError("Unexpected pre-patch source: " + name)
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(ROOT / item["path"])],
                       cwd=output, check=True)
        for name, hashes in item["files"].items():
            if sha(output / name) != hashes["after"]:
                raise ValueError("Unexpected patched source: " + name)
            names[name] = hashes["after"]
    msm = output / "techpack/display/msm"
    make = (msm / "Makefile").read_text().replace(
        "$(srctree)/techpack/display", "$(src)/..")
    options = """# Compile diagnostics only; not a Surface runtime configuration.
CONFIG_DISPLAY_BUILD := m
CONFIG_DRM_MSM := y
CONFIG_DRM_MSM_SDE := y
CONFIG_DRM_MSM_DP := y
CONFIG_DRM_MSM_DSI := y
CONFIG_DRM_MSM_DP_MST := y
# Explicit pinned donor UAPI before the kernel UAPI with the same header name.
LINUXINCLUDE := -I$(src)/../../../include/uapi $(LINUXINCLUDE)
"""
    make = options + make + "\nccflags-y += -I$(src)/../include -I$(src)/../include/uapi/display -DCONFIG_DRM_MSM_SDE=1\n"
    (msm / "Kbuild").write_text(make)
    report = dict(status=pins["status"], commit=donor["commit"],
                  source_manifest_sha256=sha(ROOT / "configs" / pins["source_manifest"]),
                  config_sha256=sha(config), patches=pins["patches"],
                  sha256={name: sha(output / name) for name in sorted(names)},
                  generated={"techpack/display/msm/Kbuild": sha(msm / "Kbuild")},
                  limits=pins["open"])
    (output / "SOURCE.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Pinned complete Xiaomi kernel source")
    parser.add_argument("--output", type=Path, required=True, help="Fresh external source tree")
    parser.add_argument("--config", type=Path)
    args = parser.parse_args()
    result = prepare(args.source, args.output, args.config)
    print(result["status"], len(result["sha256"]), args.output)
