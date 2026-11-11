#!/usr/bin/env python3
"""Check actual KGSL switch ownership and event completion/cancellation paths."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[1]

def extract(text, name):
    pattern = r"^(?:static )?(?:inline )?(?:int|void|bool)\s+" + re.escape(name) + r"\([^;{]*\)\n\{.*?^\}"
    found = re.findall(pattern, text, re.M | re.S)
    assert len(found) == 1, name
    return found[0]

def check(source, output):
    output.mkdir(parents=True, exist_ok=False)
    snippets, inputs = [], []
    for file, names in (
        ("kgsl_events.c", ("signal_event", "_kgsl_event_worker", "_do_process_group", "_process_event_group", "kgsl_flush_event_group", "kgsl_cancel_events_timestamp", "kgsl_cancel_events", "kgsl_add_event")),
        ("adreno_iommu.c", ("adreno_iommu_set_pt_ctx",)),
        ("adreno_ringbuffer.c", ("_rb_readtimestamp", "_adreno_ringbuffer_init")),
        ("adreno_drawctxt.c", ("_drawctxt_switch_wait_callback", "adreno_drawctxt_retire_switches", "adreno_drawctxt_switch")),
    ):
        path = source / file
        inputs.append(path)
        text = path.read_text()
        if file == "adreno_drawctxt.c":
            snippets.append(re.search(r"struct adreno_context_switch \{.*?^\};", text, re.M | re.S)[0])
        snippets.extend(extract(text, name) for name in names)
    header = output / "context-switch-functions.h"
    header.write_text("\n\n".join(snippets) + "\n")
    harness = ROOT / "tests/kgsl_context_switch_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    build = subprocess.run(command, capture_output=True, text=True)
    (output / "compile.log").write_text(build.stdout + build.stderr)
    build.check_returncode()
    run = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(run.stdout + run.stderr)
    inputs += [source / "adreno_ringbuffer.h", header, harness, Path(__file__)]
    report = dict(exit=run.returncode, output=run.stdout, command=command,
        sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
        limits="Actual switch, set_pt, ringbuffer initialization and event functions; CP submission/MMIO, references, allocation, locks and workqueue modeled. Ordered callback interleavings, not kernel concurrency or hardware retirement. Cancelled/registration-failed owners require future checked reclamation; test teardown is not a driver recovery path.")
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    run.check_returncode()
    print(run.stdout, end="")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.output.absolute())
