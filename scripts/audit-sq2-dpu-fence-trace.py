#!/usr/bin/env python3
"""Audit observed Linux DPU fence identities and ordering, without device access.

Kernel tracepoints identify fences by context/seqno, even when their driver names
change after signaling. They do not establish HAL buffer ownership or fence error
status; those require separate evidence.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re

EVENT = re.compile(r"^\s*(.*?)\s+\[(\d+)\]\s+\S+\s+(\d+\.\d+):\s+(dma_fence_\w+): "
                   r"driver=(\S+) timeline=(\S+) context=(\d+) seqno=(\d+)$")

def require(condition, message):
    if not condition:
        raise ValueError(message)

def ns(value):
    sec, fraction = value.split(".")
    require(len(fraction) <= 9, "Unsupported trace timestamp precision")
    return int(sec) * 1000000000 + int(fraction.ljust(9, "0"))

def parse_events(text):
    events = []
    for number, line in enumerate(text.splitlines(), 1):
        if ": dma_fence_" not in line:
            continue
        match = EVENT.match(line)
        require(match is not None, "Unparsed fence event at line " + str(number))
        task, cpu, stamp, event, driver, timeline, context, seqno = match.groups()
        require(task.rsplit("-", 1)[-1].isdigit(), "Missing event task PID")
        events.append({"line": number, "ns": ns(stamp), "pid": int(task.rsplit("-", 1)[-1]),
                       "cpu": int(cpu), "event": event, "driver": driver,
                       "timeline": timeline, "context": int(context), "seqno": int(seqno)})
    require(events, "No fence events captured")
    return events

def analyze_fences(events):
    init = [e for e in events if e["event"] == "dma_fence_init" and
            ((e["driver"] == "msm-dpu" and e["timeline"] == "dpu-video-buffer-release") or
             (e["driver"] == "msm" and e["timeline"].startswith("CRTC:")))]
    require(init and len(init) % 2 == 0, "Incomplete DPU fence creation pairs")
    identities = [(e["context"], e["seqno"]) for e in init]
    require(len(set(identities)) == len(identities), "Duplicate tracked fence initialization")
    grouped = collections.defaultdict(list)
    for e in events:
        grouped[e["context"], e["seqno"]].append(e)
    pairs, rows = [], []
    for offset in range(0, len(init), 2):
        retire, release = init[offset:offset + 2]
        require(retire["timeline"].startswith("CRTC:") and
                release["timeline"] == "dpu-video-buffer-release",
                "Creation adjacency does not form retire/release pair")
        require(retire["pid"] == release["pid"] and retire["ns"] <= release["ns"],
                "Pair not created by the same task in order")
        pair = {}
        for role, first in [("retire", retire), ("release", release)]:
            related = grouped[first["context"], first["seqno"]]
            lifecycle = {}
            for event in ["dma_fence_init", "dma_fence_signaled", "dma_fence_destroy"]:
                found = [e for e in related if e["event"] == event]
                require(len(found) <= 1, "Duplicate lifecycle event for tracked identity")
                if found:
                    lifecycle[event] = found[0]
            require(lifecycle.get("dma_fence_init") == first, "Identity collision before init")
            signal, destroy = lifecycle.get("dma_fence_signaled"), lifecycle.get("dma_fence_destroy")
            require(signal is None or signal["ns"] >= first["ns"], "Signal before initialization")
            require(destroy is None or (signal is not None and destroy["ns"] >= signal["ns"]),
                    "Destruction observed before signal")
            row = {"role": role, "context": first["context"], "seqno": first["seqno"],
                   "driver_at_init": first["driver"], "timeline_at_init": first["timeline"],
                   "events": {k: {"ns": e["ns"], "line": e["line"],
                                  "driver_at_event": e["driver"], "timeline_at_event": e["timeline"]}
                              for k, e in lifecycle.items()}}
            pair[role] = row
            rows.append(row)
        require("dma_fence_signaled" in pair["retire"]["events"],
                "Retire created in this capture has no observed signal")
        pairs.append(pair)
    require(len({p["retire"]["context"] for p in pairs}) == 1 and
            len({p["release"]["context"] for p in pairs}) == 1 and
            pairs[0]["retire"]["context"] != pairs[0]["release"]["context"],
            "Capture has ambiguous display timelines")
    for index, pair in enumerate(pairs):
        signal = pair["release"]["events"].get("dma_fence_signaled")
        own = pair["retire"]["events"]["dma_fence_signaled"]
        if signal:
            require(signal["ns"] >= own["ns"], "Release signaled before own presentation")
        if index + 1 < len(pairs):
            nxt = pairs[index + 1]["retire"]["events"]
            require(signal is not None, "Earlier release remains unsignaled after next presentation")
            require(nxt["dma_fence_init"]["ns"] <= signal["ns"] <= nxt["dma_fence_signaled"]["ns"],
                    "Release signal does not lie in the next observed commit interval")
    return {"creation_adjacent_same_task_pairs": len(pairs), "pairs": pairs,
            "tracked_retire_created": len(pairs), "tracked_release_created": len(pairs),
            "tracked_signaled": sum("dma_fence_signaled" in row["events"] for row in rows),
            "tracked_destroyed": sum("dma_fence_destroy" in row["events"] for row in rows),
            "pending_release_at_trace_end": [row for row in rows if row["role"] == "release" and
                                              "dma_fence_signaled" not in row["events"]],
            "signaled_not_destroyed_in_observation": [row for row in rows if
                    "dma_fence_signaled" in row["events"] and "dma_fence_destroy" not in row["events"]],
            "next_observed_commit_release_order_verified": True,
            "pairing_scope": "Creation-adjacent fences from the same ioctl task and one CRTC context; not a HAL frame/buffer identity map."}

def parse_stats(text):
    chunks = re.split(r"^TRACE_CPU_STATS=(.+)\n", text, flags=re.M)
    require(len(chunks) >= 3, "Per-CPU trace statistics absent")
    result = {}
    for index in range(1, len(chunks), 2):
        name, body = chunks[index:index + 2]
        fields = {}
        for key in ["entries", "overrun", "commit overrun", "dropped events"]:
            match = re.search(r"^" + re.escape(key) + r": (\d+)$", body, re.M)
            require(match is not None, "Missing trace statistic " + key)
            fields[key] = int(match[1])
        require(all(fields[k] == 0 for k in ["overrun", "commit overrun", "dropped events"]),
                "Trace loss/overrun observed")
        require(name not in result, "Duplicate CPU statistics")
        result[name] = fields
    return result

def analyze_display_waits(text, events):
    """Join trigger call stacks and actual GPU signals to display-commit waits."""
    lines = text.splitlines()
    starts = [e for e in events if e["event"] == "dma_fence_wait_start"]
    stacks = {}
    header = re.compile(r"^\s*(.*?)\s+\[(\d+)\]\s+\S+\s+(\d+\.\d+): <stack trace>$")
    for index, line in enumerate(lines):
        match = header.match(line)
        if not match:
            continue
        task, cpu, stamp = match.groups()
        pid = int(task.rsplit("-", 1)[-1])
        prior = [e for e in starts if e["pid"] == pid and e["cpu"] == int(cpu) and
                 e["line"] < index + 1 and e["line"] not in stacks]
        require(prior, "Stack trigger has no matching wait_start")
        start = prior[-1]
        require(ns(stamp) >= start["ns"], "Stack timestamp precedes wait")
        calls = []
        for following in lines[index + 1:]:
            if not following.strip().startswith("=> "):
                break
            calls.append(following.strip()[3:])
        require(calls, "Empty wait trigger stack")
        stacks[start["line"]] = calls
    pending = collections.defaultdict(list)
    completed = []
    unmatched_end = 0
    for event in events:
        key = event["pid"], event["context"], event["seqno"]
        if event["event"] == "dma_fence_wait_start":
            pending[key].append(event)
        elif event["event"] == "dma_fence_wait_end":
            if not pending[key]:
                unmatched_end += 1
                continue
            start = pending[key].pop()
            calls = stacks.get(start["line"], [])
            names = {x.split("+", 1)[0] for x in calls}
            if not {"drm_atomic_helper_wait_for_fences", "commit_tail", "commit_work"} <= names:
                continue
            require(event["ns"] >= start["ns"], "Display wait end precedes start")
            matching = [e for e in events if (e["context"], e["seqno"]) ==
                        (start["context"], start["seqno"])]
            inits = [e for e in matching if e["event"] == "dma_fence_init"]
            signals = [e for e in matching if e["event"] == "dma_fence_signaled"]
            require(len(inits) <= 1 and len(signals) <= 1, "Ambiguous waited GPU fence identity")
            signal = signals[0] if signals else None
            gpu = bool(inits and inits[0]["driver"] == "drm_sched" and
                       inits[0]["timeline"].startswith("ring"))
            actually_pending = bool(gpu and signal and start["ns"] < signal["ns"] <= event["ns"])
            completed.append({"pid": start["pid"], "context": start["context"], "seqno": start["seqno"],
                              "wait_start_ns": start["ns"], "wait_end_ns": event["ns"],
                              "observed_signal_ns": signal["ns"] if signal else None,
                              "GPU_scheduler_identity_observed": gpu,
                              "pending_GPU_fence_signaled_during_display_wait": actually_pending,
                              "call_stack": calls, "start_line": start["line"], "end_line": event["line"]})
    return {"display_commit_waits": completed,
            "pending_GPU_display_waits": [x for x in completed if x["pending_GPU_fence_signaled_during_display_wait"]],
            "wait_end_without_start_in_observation": unmatched_end,
            "wait_start_without_end_in_observation": sum(len(v) for v in pending.values()),
            "scope": "Kernel display commit waits on GPU scheduler fences; does not identify every HAL acquire buffer or fence error status."}

def audit(capture, require_display_wait_stacks=False):
    read = lambda name: (capture / name).read_text()
    text = read("fence-trace.txt")
    stats = parse_stats(read("trace-stats.txt"))
    count = re.search(r"entries-in-buffer/entries-written: (\d+)/(\d+)", text)
    require(count is not None and int(count[1]) == int(count[2]) ==
            sum(s["entries"] for s in stats.values()), "Trace entry accounting differs")
    require(read("global-trace-before.txt") == read("global-trace-after.txt"),
            "Global tracing controls changed")
    require(read("runner.exit").strip() == "0" and read("window.exit").strip() == "0",
            "Window/runner did not finish successfully")
    require("WAKELOCK_RELEASED" in read("wake.txt") and "status=0" in read("wake.txt"),
            "Trial holder release absent")
    events = parse_events(text)
    lifecycle = analyze_fences(events)
    display_waits = analyze_display_waits(text, events)
    if require_display_wait_stacks:
        require(display_waits["pending_GPU_display_waits"],
                "No pending GPU wait proven in the actual display commit path")
    markers = {}
    for line in text.splitlines():
        match = re.search(r"(\d+\.\d+): tracing_mark_write: SQ2_SYNC phase=(\S+)", line)
        if match:
            require(match[2] not in markers, "Duplicate phase marker")
            markers[match[2]] = ns(match[1])
    for key in ["WINDOW_PLAYER_START", "WINDOW_HOLD", "WINDOW_PROCESS_EXIT"]:
        require(key in markers, "Missing diagnostic marker " + key)
    require(markers["WINDOW_PLAYER_START"] < markers["WINDOW_HOLD"] < markers["WINDOW_PROCESS_EXIT"],
            "Diagnostic phase order differs")
    held = [p for p in lifecycle["pairs"] if
            markers["WINDOW_HOLD"] <= p["release"]["events"]["dma_fence_init"]["ns"] <
            markers["WINDOW_PROCESS_EXIT"]]
    return {"status": "observed_DPU_fence_identity_and_commit_order_pass",
            "trace_entries": int(count[1]), "trace_loss_observed": False,
            "global_trace_controls_unchanged": True, "cpu_statistics": stats,
            "lifecycle": lifecycle, "phase_markers_ns": markers,
            "display_acquire_waits": display_waits,
            "GPU_acquire_display_wait_callsite_verified": bool(display_waits["pending_GPU_display_waits"]),
            "commits_created_during_held_frame": held,
            "buffer_frame_identity_map_verified": False,
            "fence_error_status_verified": False,
            "full_fence_lifecycle_verified": False, "full_hwc_verified": False,
            "full_stack_complete": False,
            "limits": ["Tracepoints report fence identity and signal/destroy ordering, not fence error status.",
                       "Unobserved destruction may be a legitimate retained reference; a finite trace is not a leak test.",
                       "The final release may remain pending for the standing scanout.",
                       "HAL frame/buffer ownership, reuse and all acquire fences still need direct validation."],
            "evidence_sha256": {n: hashlib.sha256((capture / n).read_bytes()).hexdigest() for n in
                ["fence-trace.txt", "trace-stats.txt", "global-trace-before.txt",
                 "global-trace-after.txt", "runner.exit", "window.exit", "wake.txt"]}}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--require-display-wait-stacks", action="store_true")
    args = parser.parse_args()
    result = audit(args.capture, args.require_display_wait_stacks)
    result["auditor_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: result[k] for k in ["status", "trace_entries", "full_fence_lifecycle_verified"]}, indent=2))

if __name__ == "__main__":
    main()
