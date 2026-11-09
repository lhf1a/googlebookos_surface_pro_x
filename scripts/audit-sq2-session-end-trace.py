#!/usr/bin/env python3
"""Audit actual HFI END replies separately from earlier per-session errors."""
import argparse
import json
from pathlib import Path
import re


def fields(line):
    return {k: int(v, 0) for k, v in re.findall(r"(\w+)=(0x[0-9a-f]+|-?\d+)\b", line)}


def audit(capture, expectation):
    trace = (capture / "end-trace.txt").read_text()
    assert (capture / "global-trace-before.txt").read_bytes() == (capture / "global-trace-after.txt").read_bytes()
    losses = re.findall(r"^(?:overrun|commit overrun|dropped events): (\d+)",
                        (capture / "trace-stats.txt").read_text(), re.M)
    assert losses and all(int(x) == 0 for x in losses), "Trace loss"
    assert (capture / "runner.exit").read_text().strip() == "0"
    assert "END_TRACE_CLEANUP" in (capture / "runner.txt").read_text()
    pending, last, calls = {}, {}, []
    for line in trace.splitlines():
        match = re.match(r"\s*.*-(\d+)\s+\[\d+\]\s+\S+\s+[\d.]+: (begin|reply|finish|quarantine): ", line)
        if not match:
            continue
        tid, kind = int(match[1]), match[2]
        data = fields(line)
        if kind == "begin":
            assert tid not in pending, "Overlapping deinit on one thread"
            pending[tid] = {"entry": data, "reply": None}
        elif kind == "reply":
            targets = [x for x in pending.values() if x["entry"]["inst"] == data["inst"]]
            assert len(targets) == 1 and targets[0]["reply"] is None, "Unpaired END reply"
            assert data["size"] == 16 and data["type"] == 0x20007, "Wrong END packet"
            targets[0]["reply"] = data
        elif kind == "finish":
            call = pending.pop(tid)
            value = data["retval"] & 0xffffffff
            call["return"] = value - 2**32 if value & 2**31 else value
            calls.append(call)
            last[call["entry"]["inst"]] = call
        elif data["inst"] in last:
            # This function is called even when it returns false. Its name and
            # error argument alone are not evidence that quarantine happened.
            last[data["inst"]]["confirmed_after"] = data["confirmed"]
            last[data["inst"]]["prior_after"] = data["prior"]
    assert not pending and calls, "Incomplete deinit trace"
    retained = [x for x in calls if x["entry"]["prior"] == 1 and x["reply"] is not None
                and x["entry"]["sys_error"] == 0 and x["reply"]["sys_error"] == 0
                and x["reply"]["reply_error"] == 0 and x["reply"]["prior"] == 1]
    rejected = [x for x in retained if x["return"] == -5 and x.get("confirmed_after") == 0]
    confirmed = [x for x in retained if x["return"] == 0 and x.get("confirmed_after") == 1 and x.get("prior_after") == 1]
    if expectation == "baseline":
        assert not retained and all(x["return"] == 0 for x in calls)
    elif expectation == "retained-error-rejected":
        assert rejected, "No prior-error rejection of a successful END"
    else:
        assert confirmed and not rejected, "Successful END still rejected or missing"
    states = [fields(x) for x in (capture / "runner.txt").read_text().splitlines()
              if "sys_error=" in x and "vb2_bytes=" in x]
    assert states, "Missing final driver state"
    if expectation != "retained-error-rejected":
        assert all(states[-1][x] == 0 for x in ["error", "sys_error", "hfi_error", "insts", "vb2_bytes"])
    return {"expectation": expectation, "expectation_observed": True, "final_driver_state": states[-1],
            "deinit_calls": len(calls), "actual_END_replies": sum(x["reply"] is not None for x in calls),
            "retained_error_successful_END_replies": len(retained),
            "retained_error_END_rejections": len(rejected),
            "retained_error_END_confirmations": len(confirmed), "calls": calls,
            "global_trace_preserved": True, "trace_loss": 0,
            "scope": "Raw successful END confirmation only; a passing negative observation is not successful malformed decoding or a fix for earlier decode/PM timeouts.",
            "full_stack_complete": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--expect", choices=["baseline", "retained-error-rejected", "retained-error-confirmed"], required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.capture, args.expect)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "calls"}, indent=2))


if __name__ == "__main__":
    main()
