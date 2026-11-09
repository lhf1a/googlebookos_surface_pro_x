#!/usr/bin/env python3
"""Build a source-pinned QTI composer mapper5 import diagnostic, not a HWC service."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def prepare(source, output):
    config = json.loads((ROOT / "configs/sm8350-display-reference.json").read_text())
    pins = config["importer_patch"]["files"]
    for name, pin in pins.items():
        path = source / name
        if path.is_symlink() or sha(path) != pin["original"]:
            raise ValueError("Unrecognized SM8350 importer source: " + name)
    output.mkdir(parents=True, exist_ok=False)
    for name in pins:
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((source / name).read_bytes())
    subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i",
                    str(ROOT / config["importer_patch"]["path"])], cwd=output, check=True)
    for name, pin in pins.items():
        if sha(output / name) != pin["patched"]:
            raise ValueError("Patched importer hash mismatch: " + name)
    return pins

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "output", "cc", "mapper-headers", "common-headers",
                 "binder-headers", "core-headers", "hardware-headers", "platform"):
        p.add_argument("--" + name, type=Path, required=True)
    a = p.parse_args()
    pins = prepare(a.source, a.output)
    flags = [str(a.cc), "--target=aarch64-linux-android35", "-std=c++20", "-O2",
             "-fno-rtti", "-nostdlib++", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-parameter", "-include",
             str(ROOT / "probe/graphics/quest2-platform-cxx.h")]
    for directory in (a.mapper_headers, a.common_headers, a.binder_headers,
                      a.core_headers, a.hardware_headers):
        flags += ["-isystem", str(directory)]
    flags += ["-I", str(a.output / "composer")]
    commands, objects, deps = [], [], []
    for name, source in (("importer", a.output / "composer/QtiComposerHandleImporter.cpp"),
                         ("probe", ROOT / "probe/graphics/sm8350-composer-import-probe.cpp")):
        obj = a.output / (name + ".o")
        dep = a.output / (name + ".d")
        command = flags + ["-MD", "-MF", str(dep), "-c", str(source), "-o", str(obj)]
        commands.append(command)
        subprocess.run(command, check=True)
        objects.append(str(obj))
        deps += shlex.split(dep.read_text().split(":", 1)[1].replace("\\\n", " "))
    command = flags + objects + ["-L", str(a.platform), "-landroid", "-lEGL", "-lc++",
                                 "-lutils", "-lcutils", "-llog", "-ldl",
                                 "-Wl,--no-undefined", "-o",
                                 str(a.output / "sm8350-composer-import-probe")]
    commands.append(command)
    subprocess.run(command, check=True)
    inputs = {str(Path(d).resolve()): sha(Path(d)) for d in deps}
    for path in (a.cc.resolve(), ROOT / "configs/sm8350-display-reference.json",
                 ROOT / "patches/sm8350-composer-mapper5-import.patch", Path(__file__)):
        inputs[str(path)] = sha(path)
    for name in ("libc++.so", "libutils.so", "libcutils.so"):
        inputs[str(a.platform / name)] = sha(a.platform / name)
    build = {"status": "importer-only-diagnostic", "deployed": False,
             "hwc_service_built": False, "commands": commands, "source_pins": pins,
             "inputs": inputs, "sha256": {
                 "sm8350-composer-import-probe": sha(a.output / "sm8350-composer-import-probe")}}
    (a.output / "BUILD.json").write_text(json.dumps(build, indent=2) + "\n")
    print(json.dumps({k: build[k] for k in ("status", "hwc_service_built", "sha256")}))

if __name__ == "__main__":
    main()
