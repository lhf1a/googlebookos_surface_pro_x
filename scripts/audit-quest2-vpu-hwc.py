#!/usr/bin/env python3
"""Independently join real VPU DMABUF completion, AHB scanout, release and refill."""
import argparse
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import re

def require(value, message):
    if not value:
        raise ValueError(message)

def records(text):
    result = []
    for number, line in enumerate(text.splitlines(), 1):
        if not line.startswith(("VPU_", "FENCE_POINT ")):
            continue
        row = dict(re.findall(r"(\w+)=([^ ]+)", line))
        row.update(tag=line.split()[0], line=number)
        for key, value in list(row.items()):
            if isinstance(value, str) and re.fullmatch(r"-?\d+", value):
                row[key] = int(value)
        result.append(row)
    return result

def audit_ownership(text):
    data = records(text)
    presents = [r for r in data if r["tag"] == "VPU_PRESENT"]
    require(presents and [r["frame"] for r in presents] == list(range(len(presents))),
            "Missing/duplicate/reordered HWC frame")
    allocations = {r["id"]: r for r in data if r["tag"] == "VPU_ALLOCATION"}
    require(len(allocations) == sum(r["tag"] == "VPU_ALLOCATION" for r in data),
            "Duplicate AHB allocation identity")
    frees = [r for r in data if r["tag"] == "VPU_FREE"]
    require(len(frees) == len(allocations) and {r["id"] for r in frees} == set(allocations),
            "Every allocation must be freed exactly once")
    rows = []
    for p in presents:
        a = allocations.get(p["id"])
        require(a and a["slot"] == p["slot"] and a["time_ns"] < p["time_ns"],
                "Presented AHB does not match allocation")
        dq = [r for r in data if r["tag"] == "VPU_EVENT" and r["op"] == "DQBUF" and
              r["inode"] == a["inode"] and r["bytes"] > 0 and r["pts"] == p["pts"] and
              a["time_ns"] < r["time_ns"] < p["time_ns"]]
        require(len(dq) == 1 and not (dq[0]["flags"] & 64), "Missing/ambiguous/error VPU completion")
        ready = [r for r in data if r["tag"] == "VPU_HWC" and r["phase"] == "ready" and
                 r["frame"] == p["frame"] and r["id"] == p["id"]]
        require(len(ready) == 1 and dq[0]["time_ns"] < ready[0]["time_ns"] < p["time_ns"],
                "HWC ready precedes real decoder completion")
        q = [r for r in data if r["tag"] == "VPU_EVENT" and r["op"] == "QBUF" and
             r["inode"] == a["inode"] and a["time_ns"] < r["time_ns"] < dq[0]["time_ns"]]
        require(q, "Completion has no earlier real QBUF")
        release = [r for r in data if r["tag"] == "VPU_RELEASE" and r["released_frame"] == p["frame"]]
        require(len(release) == 1, "Missing/duplicate frame release")
        release = release[0]
        require(release["id"] == p["id"] and release["slot"] == p["slot"] and
                release["time_ns"] > p["time_ns"] and release["replaced_by"] == p["frame"] + 1,
                "Release belongs to a different frame/buffer")
        fence = [r for r in data if r["tag"] == "FENCE_POINT" and r.get("kind") == "vpu_release"
                 and r["frame"] == p["frame"] and r["slot"] == p["slot"] and r["phase"] == "after"]
        require(len(fence) == 1 and fence[0]["status"] == 1 and
                p["time_ns"] < fence[0]["timestamp_ns"] <= release["time_ns"],
                "Release fence is not a completed replacement")
        # Both the application refill and actual kernel QBUF must respect display ownership.
        for r in data:
            same = ((r["tag"] == "VPU_REFILL" and r["id"] == p["id"]) or
                    (r["tag"] == "VPU_EVENT" and r["op"] == "QBUF" and r["inode"] == a["inode"]))
            if same:
                require(not (dq[0]["time_ns"] < r["time_ns"] < release["time_ns"]),
                        "Decoder requeue before display release")
        free = next(r for r in frees if r["id"] == p["id"])
        require(free["inode"] == a["inode"] and free["time_ns"] > release["time_ns"],
                "AHB freed before display release")
        rows.append({"frame": p["frame"], "stage": p["stage"], "slot": p["slot"], "id": p["id"],
                     "inode": a["inode"], "pts": p["pts"], "qbuf_ns": q[-1]["time_ns"],
                     "dqbuf_ns": dq[0]["time_ns"], "present_ns": p["time_ns"],
                     "release_ns": release["time_ns"], "signal_ns": fence[0]["timestamp_ns"],
                     "free_ns": free["time_ns"], "cached": p["cached"]})
    return rows

def snapshot_kind(planes, count, boundaries):
    if count in boundaries:
        if not planes:
            return "empty-flush"
        if len(planes) == 1 and "format=AB24 " in planes[0] and "modifier=0x0" in planes[0]:
            return "bootstrap-replacement"
    require(len(planes) == 1 and "format=NV12 " in planes[0] and "modifier=0x0" in planes[0],
            "Snapshot is not sole linear NV12 scanout or an observed flush/bootstrap boundary")
    return "video"

def trace_join(text, rows, events):
    data = records(text)
    result = []
    for row in rows:
        marks = {r["phase"]: r for r in data if r["tag"] == "VPU_HWC" and
                 r["frame"] == row["frame"] and r["id"] == row["id"]}
        require(set(marks) == {"ready", "present_begin", "present_complete"}, "Frame markers incomplete")
        created = [e for e in events if e["event"] == "dma_fence_init" and
                   e["timeline"] == "dpu-video-buffer-release" and
                   marks["present_begin"]["time_ns"] <= e["ns"] <= marks["present_complete"]["time_ns"]]
        require(len(created) == 1, "Frame has ambiguous kernel DPU release identity")
        identity = created[0]
        signaled = [e for e in events if e["event"] == "dma_fence_signaled" and
                    (e["context"], e["seqno"]) == (identity["context"], identity["seqno"])]
        require(len(signaled) == 1 and abs(signaled[0]["ns"] - row["signal_ns"]) < 1000000 and
                signaled[0]["ns"] <= row["release_ns"], "Kernel signal disagrees with actual sync_file")
        result.append({"frame": row["frame"], "context": identity["context"], "seqno": identity["seqno"],
                       "trace_signal_ns": signaled[0]["ns"], "sync_file_signal_ns": row["signal_ns"]})
    require(len({(r["context"], r["seqno"]) for r in result}) == len(rows),
            "Kernel release identity reused for different frames")
    return result

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("capture", "build", "composer-build", "regular-fixture", "drc-fixture",
                 "regular-reference", "drc-reference", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--dataspace", choices=("bt601", "bt709"), default="bt601")
    args = parser.parse_args()
    capture = args.capture
    spec = importlib.util.spec_from_file_location("fences", Path(__file__).with_name("audit-sq2-dpu-fence-trace.py"))
    fences = importlib.util.module_from_spec(spec);spec.loader.exec_module(fences)
    trace = (capture / "fence-trace.txt").read_text()
    events = fences.parse_events(trace)
    stats = fences.parse_stats((capture / "trace-stats.txt").read_text())
    require((capture / "global-trace-before.txt").read_bytes() ==
            (capture / "global-trace-after.txt").read_bytes(), "Global trace state changed")
    build = json.loads((args.build / "BUILD.json").read_text())
    for name, digest in build["outputs"].items():
        require(sha(capture / name) == digest, "Wrong diagnostic binary: " + name)
    expected_composer = args.composer_build / "vendor.qti.hardware.display.composer-service"
    require((capture / "composer-hash.txt").read_text().split()[0] == sha(expected_composer),
            "Runtime composer not the specified build")
    for line in (capture / "SHA256SUMS").read_text().splitlines():
        digest, name = line.split("  ")
        require(sha(capture / name) == digest, "Runtime payload hash mismatch: " + name)
    require((capture / "probe.exit").read_text().strip() == "0" and
            "RESTORE_COMPLETE" in (capture / "runner.txt").read_text(), "Runner failed/incomplete")
    reports = {}
    for name in ("regular", "drc"):
        text = (capture / (name + ".txt")).read_text()
        require((capture / (name + ".exit")).read_text().strip() == "0" and
                "RESULT exit=0 frames=30 linear_nv12=30 output_eos=1" in text and
                "CLEANUP buffers_and_states=1 handle_released=1" in text, "Decode lifecycle failed")
        rows = audit_ownership(text)
        require(len(rows) == 30, "Expected 30 actual frames")
        fixture = json.loads(getattr(args, name + "_fixture").read_text())
        pts = fixture["expected_output_pts_us"] if name == "regular" else fixture["pts_us"]
        require([r["pts"] for r in rows] == pts, "Unexpected decoder/HWC PTS")
        table = list(csv.DictReader((capture / (name + "-frames/frames.tsv")).open(), delimiter="\t"))
        require(len(table) == len(rows) and [int(r["pts_us"]) for r in table] == pts, "Frame table disagrees")
        dims = [(int(t["width"]), int(t["height"])) for t in table]
        expected_dims = [(320, 240)] * 30 if name == "regular" else [tuple(d) for d in fixture["dimensions"]]
        require(dims == expected_dims, "DRC geometry mismatch")
        decoded = b"".join((capture / (name + "-frames") / ("frame-%06d.nv12" % i)).read_bytes()
                           for i in range(len(rows)))
        reference = getattr(args, name + "_reference")
        require(decoded == reference.read_bytes(), "Decoded visible pixels differ from independent FFmpeg")
        mappings = trace_join(text, rows, events)
        snapshots, transitions, hwc_transitions = [], [], []
        boundaries = {r["frames_total"] for r in records(text) if r["tag"] == "VPU_HWC_STAGE_END"}
        observed_dims = set()
        for path in sorted((capture / (name + "-snapshots")).glob("*-drm.txt")):
            state = path.read_text().split("DRM_DEBUG_PATH=")[1]
            planes = [v for v in re.split(r"(?m)^plane\[", state)[1:] if
                      re.search(r"(?m)^\s+crtc=crtc-\d+$", v.split("crtc[")[0])]
            kind = snapshot_kind(planes, int(path.name.split("-")[0]), boundaries)
            if kind != "video":
                transitions.append({"file": path.name, "kind": kind})
                continue
            dim = re.search(r"src-pos=(\d+)\.\d+x(\d+)\.\d+\+", planes[0])
            require(dim is not None and ("color-encoding=ITU-R BT." +
                    ("709" if args.dataspace == "bt709" else "601") + " YCbCr") in planes[0] and
                    "color-range=YCbCr limited range" in planes[0], "Unexpected physical color/geometry")
            observed_dims.add(tuple(map(int, dim.groups())))
            hwc = path.with_name(path.name.replace("-drm.txt", "-hwc.txt")).read_text()
            if "Layers added or removed but not reflected to SDM" in hwc:
                require("composition:" not in hwc, "Transitional HWC dump contains unexpected layers")
                hwc_transitions.append(path.name.replace("-drm.txt", "-hwc.txt"))
            elif hwc.count("composition: Client/Client") == 1 and "composition: Device/" not in hwc:
                # The following Binder dump can cross the final held frame into its CLIENT replacement.
                count = int(path.name.split("-")[0])
                require(count in boundaries or count + 1 in boundaries,
                        "CLIENT HWC dump outside an adjacent observed DRC/EOS replacement")
                hwc_transitions.append(path.name.replace("-drm.txt", "-hwc.txt"))
            else:
                require(hwc.count("composition: Device/Device") == 1 and "composition: Client" not in hwc,
                        "HWC snapshot is not DEVICE-only")
            snapshots.append(path.name)
        require(set(expected_dims) <= observed_dims, "Missing actual KMS observation for a resolution")
        crc = (capture / (name + "-crc.txt")).read_text()
        require((capture / (name + "-crc.exit")).read_text().strip() == "0" and
                "CRC_OBSERVER_RESTORED source=none" in crc and
                "samples=600 requested=600" in crc, "CRC observation failed")
        reports[name] = {"frames": len(rows), "cached_reuses": sum(r["cached"] for r in rows),
                         "pixels_exact_bytes": len(decoded), "reference_sha256": sha(reference),
                         "rows": rows, "kernel_release_identities": mappings,
                         "kms_snapshots": snapshots, "flush_transition_snapshots": transitions,
                         "sequential_hwc_transition_snapshots": hwc_transitions,
                         "snapshot_scope": "Sequential KMS/HWC observations; no claim of atomic per-frame sampling.",
                         "observed_dimensions": sorted(observed_dims),
                         "physical_pixel_exact_verified": False}
    report = {"status": "verified-controlled-vpu-hwc-lifecycle", "videos": reports,
              "trace_cpu_stats": stats, "vpu_acquire_contract": "Completed V4L2 DQBUF; no fabricated acquire fence",
              "dataspace_source": "Explicit known test fixture; automatic OMX VUI propagation is not established.",
              "expected_dataspace": args.dataspace,
              "scope": "Single direct OMX/HIDL-composer diagnostic session, CPU/GPU reads validate the same AHB; no pixel upload. Fixed100ms observation pacing is not a performance result.",
              "full_fence_lifecycle_verified": False, "full_hwc_verified": False, "full_stack_complete": False}
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": report["status"], "frames": sum(v["frames"] for v in reports.values())}))

if __name__ == "__main__":
    main()
