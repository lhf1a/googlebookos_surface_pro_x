#!/usr/bin/env python3
"""Check the actual HWSCHED restart/replay/owner transition functions."""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def extract(s, name):
    pat = r"^(?:static )?(?:inline )?(?:int|void|bool|struct cmd_list_obj \*)\s*" + re.escape(name) + r"\([^;{]*\)\s*\n\{.*?^\}"
    m = re.findall(pat, s, re.M | re.S)
    if len(m) != 1:
        raise ValueError("Expected actual function " + name)
    return m[0]
def check(source, output):
    output.mkdir(parents=True, exist_ok=False)
    snippets, paths = [], []
    for filename, names in [
        ("adreno_a6xx_hwsched.c", ["a6xx_hwsched_restart"]),
        ("adreno_hwsched.c", ["adreno_hwsched_set_fault", "retire_cmdobj", "adreno_hwsched_init_replay", "adreno_hwsched_complete_replay", "get_fault_cmdobj", "reset_and_snapshot", "adreno_hwsched_recovery", "adreno_hwsched_mark_drawobj"]),
    ]:
        p = source / filename
        s = p.read_text()
        paths.append(p)
        if filename == "adreno_hwsched.c":
            assert "force_retire_timestamp" not in s
            assert "transition_error" in extract(s, "hwsched_sendcmd")
            assert "transition_error" in extract(s, "adreno_hwsched_queue_cmds")
        snippets.extend(extract(s, n) for n in names)
    header = output / "hwsched-recovery-functions.h"
    header.write_text("\n\n".join(snippets) + "\n")
    harness = ROOT / "tests/kgsl_hwsched_recovery_test.c"
    cmd = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(cmd, check=True)
    run = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(run.stdout + run.stderr)
    report = dict(exit=run.returncode, output=run.stdout, command=cmd, sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths + [source / "adreno_hwsched.h", header, harness, Path(__file__)]}, limits="Actual KGSL recovery/replay/restart/retire/mark functions; hardware reset/boot/HFI submission, timestamps, refs, locks, context invalidation and events are models. Ordered fault injection, not actual GMU/CP/preemption retirement, kernel concurrency or safe unbind proof. Normal enqueue/send guards source-checked.")
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    run.check_returncode()
    print(run.stdout, end="")
if __name__ == "__main__":
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source",type=Path,required=True)
    ap.add_argument("--output",type=Path,required=True)
    a=ap.parse_args(); check(a.source.resolve(),a.output.absolute())
