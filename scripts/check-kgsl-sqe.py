#!/usr/bin/env python3
"""Execute actual KGSL ucode loading with modeled firmware and allocation APIs."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("kgsl_extract", ROOT / "scripts/check-kgsl-dmabuf.py")
extractor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extractor)


def check(source, output, firmware=None):
    code = source.read_text()
    functions = "\n\n".join(extractor.extract(code, name) for name in
                             ("get_ucode_version", "adreno_get_firmware"))
    output.mkdir(parents=True, exist_ok=False)
    extracted = output / "sqe-functions.h"
    extracted.write_text(functions + "\n")
    harness = ROOT / "tests/kgsl_sqe_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
               "-I", str(output), str(harness), "-o", str(output / "check")]
    compiled = subprocess.run(command, capture_output=True, text=True)
    (output / "compile.stdout").write_text(compiled.stdout)
    (output / "compile.stderr").write_text(compiled.stderr)
    cases = []
    if not compiled.returncode:
        for case in range(51 if firmware else 50):
            run = [str(output / "check"), str(case)] + ([str(firmware)] if case == 50 else [])
            result = subprocess.run(run, capture_output=True, text=True,
                                    env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
            (output / f"case-{case}.stdout").write_text(result.stdout)
            (output / f"case-{case}.stderr").write_text(result.stderr)
            cases.append(dict(command=run, exit=result.returncode,
                              stdout=result.stdout, stderr=result.stderr))
    inputs = [source, harness, Path(__file__), extracted] + ([firmware] if firmware else [])
    report = dict(status="actual-functions-host-model-not-hardware-validated",
                  compile=dict(command=command, exit=compiled.returncode,
                               stdout=compiled.stdout, stderr=compiled.stderr),
                  cases=cases, passed=sum(c["exit"] == 0 for c in cases),
                  failed=sum(c["exit"] != 0 for c in cases),
                  input_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs})
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"SQE cases: {report['passed']} passed, {report['failed']} failed; compile={compiled.returncode}")
    return 1 if compiled.returncode or report["failed"] else 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Prepared adreno.c")
    parser.add_argument("--output", type=Path, required=True, help="Fresh evidence directory")
    parser.add_argument("--firmware", type=Path, help="Optional captured SQE file, never bundled")
    args = parser.parse_args()
    raise SystemExit(check(args.source.resolve(), args.output.absolute(),
                           args.firmware.resolve() if args.firmware else None))
