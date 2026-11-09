#!/usr/bin/env python3
"""Build a bounded composer HAL diagnostic against an existing matched build manifest."""
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
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build = json.loads((args.composer_build / "BUILD.json").read_text())
    args.output.mkdir(parents=True, exist_ok=False)
    compile_command = next(c for c in build["commands"] if "-c" in c)
    flags = compile_command[:compile_command.index("-MD")]
    deps_root = args.composer_build.parent / "sm8350-composer-deps-v1"
    for version in ("2.1", "2.2", "2.3", "2.4"):
        flags += ["-I", str(deps_root / "composer-hal" / version / "utils/command-buffer/include")]
    objects, dependencies, commands = [], set(), []
    for name in ("sq2-composer-fence-probe", "quest2-mapper5-video-buffer"):
        obj = args.output / (name + ".o")
        dep = args.output / (name + ".d")
        command = flags + ["-MD", "-MF", str(dep), "-c",
                           str(ROOT / "probe/graphics" / (name + ".cpp")), "-o", str(obj)]
        commands.append(command)
        subprocess.run(command, check=True)
        objects.append(str(obj))
        dependencies.update(shlex.split(dep.read_text().split(":", 1)[1].replace("\\\n", " ")))
    service = next(c for c in build["commands"] if "-lhidlbase" in c)
    link = service[:service.index("-L")]
    for i, value in enumerate(service):
        if value == "-L":
            link += ["-L", service[i + 1]]
    link += objects + ["-landroid", "-lEGL", "-lGLESv2", "-lGLESv3", "-lc++", "-lutils", "-lcutils", "-llog",
                       "-ldl", "-lhidlbase", "-lfmq", "-lsync"]
    link += ["-l:android.hardware.graphics.composer@" + v + ".so" for v in ("2.1", "2.2", "2.3", "2.4")]
    binary = args.output / "sq2-composer-fence-probe"
    link += ["-o", str(binary)]
    commands.append(link)
    subprocess.run(link, check=True, stdout=(args.output / "link.txt").open("w"))
    dependencies.update((str(Path(__file__).resolve()), str(args.composer_build / "BUILD.json")))
    sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
    report = {"status": "diagnostic-built-not-executed", "commands": commands,
              "inputs": {p: sha(p) for p in sorted(dependencies)}, "sha256": sha(binary)}
    (args.output / "BUILD.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "sha256": report["sha256"]}))

if __name__ == "__main__":
    main()
