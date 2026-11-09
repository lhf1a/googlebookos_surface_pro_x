#!/usr/bin/env python3
"""Audit real composer HAL ownership records; FD numbers/inodes are not fence identities."""
import argparse
from collections import Counter
import json
from pathlib import Path

def require(condition, message):
    if not condition:
        raise ValueError(message)

def records(text):
    result = []
    accepted = {"BUFFER_ID", "BUFFER_WRITE", "REUSE_READY", "FRAME_PRESENT", "RELEASE_ASSOC",
                "FENCE", "FENCE_POINT", "MALFORMED_RESPONSE", "PROTOCOL_RECOVERY",
                "OWNERSHIP_PASS", "HAL_FENCE_RESULT"}
    for line in text.splitlines():
        fields = line.split()
        if not fields or fields[0] not in accepted:
            continue
        values = {}
        for field in fields[1:]:
            require("=" in field, "Malformed diagnostic field")
            key, value = field.split("=", 1)
            require(key not in values, "Duplicate diagnostic field")
            try: values[key] = int(value)
            except ValueError: values[key] = value
        result.append((fields[0], values))
    return result

def audit(text, fixed=True):
    rows = records(text)
    def get(kind):
        return [record for event, record in rows if event == kind]
    ids = get("BUFFER_ID")
    require(len(ids) == 3 and {x["slot"] for x in ids} == {0, 1, 2}, "Missing three buffer slots")
    by_slot = {x["slot"]: x["id"] for x in ids}
    require(len(set(by_slot.values())) == 3 and all(by_slot.values()), "Buffer IDs are not distinct")
    frames = get("FRAME_PRESENT")
    require(len(frames) == 30 and [x["frame"] for x in frames] == list(range(30)), "Missing/reordered presents")
    for x in frames:
        require(x["slot"] == x["frame"] % 3 and x["cached"] == int(x["frame"] >= 3),
                "Incorrect slot/cache attribution")
    require(len({x["layer"] for x in frames}) == 1, "Layer changed during ownership test")
    assoc = get("RELEASE_ASSOC")
    require(len(assoc) == 30 and [x["released"] for x in assoc] == list(range(30)), "Missing release associations")
    for x in assoc:
        require(x["presented"] == x["released"] + 1 and x["slot"] == x["released"] % 3
                and x["id"] == by_slot[x["slot"]], "Release associated with wrong frame/buffer")
    fences, points = get("FENCE"), get("FENCE_POINT")
    require(fences and points and all(x["status"] >= 0 for x in fences + points), "Fence error status")
    for x in fences:
        require(0 < x["points"] < 128, "Invalid sync_file fence count")
    for kind, count in (("release", 30), ("present", 30), ("bootstrap", 1), ("replacement", 1), ("flush", 1)):
        for phase in ("before", "after"):
            selected = [x for x in fences if x["kind"] == kind and x["phase"] == phase]
            expected = list(range(30)) if count == 30 else [0 if kind == "bootstrap" else 30]
            require(len(selected) == count and sorted(x["frame"] for x in selected) == expected,
                    f"Missing/duplicate {kind} {phase} fences")
            if phase == "after":
                require(all(x["status"] == 1 for x in selected), "Fence was not signaled")
            point_rows = [x for x in points if x["kind"] == kind and x["phase"] == phase]
            require(Counter((x["frame"], x["slot"]) for x in point_rows) ==
                    Counter({(x["frame"], x["slot"]): x["points"] for x in selected}),
                    "Missing fence point information")
            if phase == "after":
                require(all(x["status"] == 1 and x["timestamp_ns"] > 0 for x in point_rows),
                        "Fence point not signaled")
    returned = [x for x in fences if x["kind"] == "release" and x["phase"] == "returned"]
    require(len(returned) == 30 and [x["frame"] for x in returned] == list(range(30)), "Missing returned release fences")
    require(any(x["driver"] == "msm-dpu" and x["object"] == "dpu-video-buffer-release"
                for x in points if x["kind"] == "release"), "No real DPU release point observed")
    releases = {(x["frame"], x["slot"]): x for x in fences if x["kind"] == "release" and x["phase"] == "after"}
    reuses = get("REUSE_READY")
    require(len(reuses) == 27 and [x["frame"] for x in reuses] == list(range(3, 30)), "Missing cached buffer reuses")
    writes = get("BUFFER_WRITE")
    require(len(writes) == 31 and Counter(x["frame"] for x in writes) == Counter({0: 2, **{i: 1 for i in range(1, 30)}}),
            "Unexpected CPU writes/bootstrap")
    for reuse in reuses:
        frame, slot = reuse["frame"], reuse["slot"]
        require(slot == frame % 3 and reuse["previous"] == frame - 3, "Reuse references wrong submission")
        released = releases[(frame - 3, slot)]
        write = next(x for x in writes if x["frame"] == frame and x["slot"] == slot)
        require(released["time_ns"] <= reuse["time_ns"] <= write["time_ns"], "CPU write preceded release wait")
    ownership = get("OWNERSHIP_PASS")
    require(ownership == [{"frames": 30, "slots": 3, "cached_submissions": 27, "reuses": 27, "final_flush": 1}],
            "Missing ownership completion")
    protocol = get("MALFORMED_RESPONSE")
    if fixed:
        require(len(protocol) == 12 and {(x["version"], x["scenario"]) for x in protocol} ==
                {(v, s) for v in (21, 22, 23) for s in range(4)}, "Missing malformed command matrix")
        require(all(x["actual"] == x["expected"] == 4 and not x["output_errors"] for x in protocol),
                "Malformed command incorrectly reported")
        recovery = get("PROTOCOL_RECOVERY")
        require(len(recovery) == 12 and all(x["success"] == 1 for x in recovery) and
                {(x["version"], x["scenario"]) for x in recovery} ==
                {(v, s) for v in (21, 22, 23) for s in range(4)}, "Queue did not recover")
        for phase in ("before", "after"):
            actual = [x for x in fences if x["kind"] == "recovery" and x["phase"] == phase]
            require(len(actual) == 12 and {(x["frame"], x["slot"]) for x in actual} ==
                    {(v, s) for v in (21, 22, 23) for s in range(4)}, "Missing recovery present fences")
            if phase == "after":
                require(all(x["status"] == 1 for x in actual), "Recovery present did not signal")
    else:
        require(protocol == [{"actual": 0, "expected": 4, "output_errors": 0}], "Baseline defect was not reproduced")
    require(get("HAL_FENCE_RESULT") == [{"ownership": 1, "malformed_error": int(fixed), "full_stack_complete": 0}],
            "Missing final diagnostic result")
    return {"ownership_verified": True, "nv12_device_frames": 30, "stable_buffer_ids": 3,
            "prior_frame_release_fences": 30, "safe_cached_reuses": 27,
            "replacement_and_empty_flush_verified": True,
            "malformed_matrix_cases": len(protocol), "malformed_errors_fixed": fixed,
            "sync_file_negative_statuses": 0,
            "limits": ["CPU-written linear BT.601 limited NV12 320x240; one DEVICE layer",
                       "No per-buffer GPU/decoder acquire identity or fence-error injection",
                       "No strict DPU pixel, latency, performance, HDR, secure or cross-device acceptance"],
            "full_fence_lifecycle_verified": False, "full_hwc_verified": False, "full_stack_complete": False}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--baseline", action="store_true")
    parser.add_argument("--composer-build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = audit((args.capture / "probe.txt").read_text(), not args.baseline)
    expected_exit = 3 if args.baseline else 0
    for name in ("probe.exit", "runner.exit"):
        require(int((args.capture / name).read_text()) == expected_exit, f"Unexpected {name}")
    manifest = json.loads((args.composer_build / "BUILD.json").read_text())
    digest = manifest["sha256"]["vendor.qti.hardware.display.composer-service"]
    require((args.capture / "composer-hash.txt").read_text().split()[0] == digest, "Wrong actual composer executable")
    result["observed_composer_sha256"] = digest
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))

if __name__ == "__main__":
    main()
