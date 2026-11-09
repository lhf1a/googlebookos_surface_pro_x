#!/usr/bin/env python3
"""Build real native VPU/HWC ownership diagnostics from matched platform manifests."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--composer-build", type=Path, required=True)
    parser.add_argument("--mapper-build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    composer = json.loads((args.composer_build / "BUILD.json").read_text())
    mapper = json.loads((args.mapper_build / "BUILD.json").read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    cc = next(c for c in mapper["commands"] if "-c" in c and c[0].endswith("/clang"))
    cflags = cc[:cc.index("-c")] + ["-DQUEST2_NATIVE_HWC"]
    cpp = next(c for c in composer["commands"] if "-c" in c)
    cppflags = cpp[:cpp.index("-MD")]
    deps_root = args.composer_build.parent / "sm8350-composer-deps-v1"
    for version in ("2.1", "2.2", "2.3", "2.4"):
        cppflags += ["-I", str(deps_root / "composer-hal" / version / "utils/command-buffer/include")]
    commands, dependencies, objects = [], set(), []
    def run(command):
        commands.append(command)
        subprocess.run(command, check=True)
    for source, flags in [
        ("probe/video/quest2-omx-decode.c", cflags),
        ("probe/graphics/quest2-native-video-capture.c", cflags),
        ("probe/graphics/quest2-mapper5-video-buffer.cpp", cppflags),
        ("probe/graphics/quest2-vpu-hwc.cpp", cppflags),
    ]:
        obj = args.output / (Path(source).name + ".o")
        dep = obj.with_suffix(".d")
        run(flags + ["-MD", "-MF", str(dep), "-c", str(ROOT / source), "-o", str(obj)])
        dependencies.update(shlex.split(dep.read_text().split(":", 1)[1].replace("\\\n", " ")))
        objects.append(str(obj))
    service = next(c for c in composer["commands"] if "-lhidlbase" in c)
    link = service[:service.index("-L")]
    for i, value in enumerate(service):
        if value == "-L":
            link += ["-L", service[i + 1]]
    link += objects + ["-landroid", "-lEGL", "-lGLESv3", "-lgrallocutils", "-lqdMetaData",
                       "-lc++", "-lutils", "-lcutils", "-llog", "-ldl", "-lhidlbase",
                       "-lfmq", "-lsync", "-lm", "-pthread", "-Wl,--export-dynamic"]
    link += ["-l:android.hardware.graphics.composer@" + v + ".so" for v in ("2.1", "2.2", "2.3", "2.4")]
    binary = args.output / "quest2-vpu-hwc-decode"
    run(link + ["-o", str(binary)])
    bridge = next(c for c in mapper["commands"] if any(v.endswith("/quest2-vidc-bridge.c") for v in c))
    bridge = bridge[:bridge.index("-o")] + ["-DQUEST2_VPU_HWC_TRACE", "-MD", "-MF",
        str(args.output / "bridge.d"), "-o", str(args.output / "libquest2-vidc-bridge.so")]
    run(bridge)
    dependencies.update(shlex.split((args.output / "bridge.d").read_text().split(":", 1)[1].replace("\\\n", " ")))
    dependencies.update(map(str, [Path(__file__).resolve(), args.composer_build / "BUILD.json", args.mapper_build / "BUILD.json"]))
    sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
    report = {"status": "diagnostic-built-not-executed", "commands": commands,
              "inputs": {p: sha(p) for p in sorted(dependencies)},
              "outputs": {p.name: sha(p) for p in (binary, args.output / "libquest2-vidc-bridge.so")}}
    (args.output / "BUILD.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "outputs": report["outputs"]}))


if __name__ == "__main__":
    main()
