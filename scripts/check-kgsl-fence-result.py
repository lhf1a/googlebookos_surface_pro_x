#!/usr/bin/env python3
"""Exercise actual KGSL fence results, event registration and invalidation."""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def extract(s, name):
    pat = r"^(?:static )?(?:inline )?(?:int|void|bool)\s+" + re.escape(name) + r"\([^;{]*\)\n\{.*?^\}"
    matches = re.findall(pat, s, re.M | re.S)
    if len(matches) != 1:
        raise ValueError("Expected actual function " + name)
    return matches[0]
def check(source, output):
    output.mkdir(parents=True, exist_ok=False)
    snippets, paths = [], []
    for filename, names in [
        ("kgsl_events.c", ["signal_event", "_kgsl_event_worker", "_do_process_group", "_process_event_group", "kgsl_flush_event_group", "kgsl_cancel_events_timestamp", "kgsl_cancel_events", "kgsl_add_event"]),
        ("kgsl_sync.c", ["kgsl_enable_signaling", "kgsl_sync_fence_signal", "kgsl_sync_fence_event_cb", "_add_fence_event", "kgsl_sync_cancel"]),
        ("adreno_drawctxt.c", ["drawctxt_detach_drawobjs", "adreno_drawctxt_invalidate"]),
    ]:
        path = source / filename
        s = path.read_text()
        paths.append(path)
        if filename == "kgsl_sync.c":
            snippets.append(re.search(r"struct kgsl_sync_fence_event_priv \{.*?\n\};", s, re.S)[0])
            assert ".signaled =" not in s.split("static const struct dma_fence_ops kgsl_sync_fence_ops =", 1)[1].split("};", 1)[0]
            ioctl = extract(s, "kgsl_add_fence_event")
            assert "kgsl_sync_fence_signal(kfence, KGSL_EVENT_RETIRED);" in ioctl
            assert "_add_fence_event(device, context, kfence);" in ioctl
        if filename == "adreno_drawctxt.c":
            assert "kgsl_sharedmem_writel" not in extract(s, "wait_for_timestamp_rb")
        snippets.extend(extract(s, n) for n in names)
    header = output / "fence-result-functions.h"
    header.write_text("\n\n".join(snippets) + "\n")
    harness = ROOT / "tests/kgsl_fence_result_test.c"
    cmd = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(cmd, check=True)
    run = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(run.stdout + run.stderr)
    report = dict(exit=run.returncode, output=run.stdout, command=cmd, sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths + [header, harness, Path(__file__)]}, limits="Actual KGSL functions; modeled dma_fence, locks, references, allocation and workqueue. Ordered callback interleavings, not kernel concurrency, weak-memory or GPU retirement proof. ioctl immediate path and wait_rb removal source-checked.")
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    run.check_returncode()
    print(run.stdout, end="")
if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    a = ap.parse_args()
    check(a.source.resolve(), a.output.absolute())
