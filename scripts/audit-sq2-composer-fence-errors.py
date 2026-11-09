#!/usr/bin/env python3
"""Audit controlled real kernel sync_file errors separately from GPU/VPU producer evidence."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
def module(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/("scripts/"+name+".py"))
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result
trace=module("audit-sq2-dpu-fence-trace")
ownership=module("audit-sq2-composer-ownership")
require=trace.require

def audit(capture,composer_build):
    read=lambda name:(capture/name).read_text()
    text=read("probe.txt")
    require(read("runner.exit").strip()==read("probe.exit").strip()=="0","Diagnostic failed")
    require("RESTORE_COMPLETE" in read("device.txt") and "WAKELOCK_RELEASED" in read("wake.txt"),
            "Restoration incomplete")
    own=ownership.audit(text)
    groups={}
    accepted={"CONTROLLED_ACQUIRE","CONTROLLED_SUBMIT_RESULT","INVALID_ACQUIRE_RESULT","FENCE_ERRORS_GROUP","SHORT_CLIENT_TARGET_RESPONSE"}
    for line in text.splitlines():
        parts=line.split()
        if not parts or parts[0] not in accepted:continue
        row={}
        for part in parts[1:]:
            key,value=part.split("=",1);require(key not in row,"Duplicate field")
            try:row[key]=int(value)
            except ValueError:row[key]=value
        groups.setdefault(parts[0],[]).append(row)
    require(groups.get("INVALID_ACQUIRE_RESULT")==[{
        "frame":1,"execute":0,"command_errors":1,"presents":0,"rejected":1}],
        "Invalid FD was not rejected without presentation")
    errors=re.findall(r"^COMMAND_ERROR location=(\d+) error=(-?\d+)$",text,re.M)
    require(len(errors)==1 and int(errors[0][1])==4,"Expected exactly one BAD_PARAMETER")
    short=groups.get("SHORT_CLIENT_TARGET_RESPONSE",[])
    require(short==[{"version":v,"actual":4,"recovery":1} for v in [21,22,23]],
            "Missing short client-target rejection/recovery")
    before,after=read("invalid-before-drm.txt"),read("invalid-after-drm.txt")
    require(before==after and len(re.findall(r"^\t\tformat=NV12 ",before,re.M))==1,
            "Rejected acquire changed the active DRM state or no NV12 baseline")
    require(groups.get("FENCE_ERRORS_GROUP")==[{
        "invalid_sync_fd":1,"signaled_error":1,"pending_signal":1,"pending_error":1,
        "real_kernel_sw_sync":1,"full_stack_complete":0}],"Missing error matrix completion")
    results=groups.get("CONTROLLED_SUBMIT_RESULT",[])
    require(len(results)==3 and [x["frame"] for x in results]==[2,3,4] and all(
        x["execute"]==0 and x["command_errors"]==0 and x["presents"]==x["accepted"]==1 for x in results),
        "Valid sync_file cases failed or were silently retried")
    observations=groups.get("CONTROLLED_ACQUIRE",[])
    require(len(observations)==9 and {(x["frame"],x["phase"]) for x in observations}==
        {(f,p) for f in [2,3,4] for p in ["created","submitted","completed"]},"Missing input fence observations")
    obs={(x["frame"],x["phase"]):x for x in observations}
    for frame in [2,3,4]:
        for phase in ["created","submitted","completed"]:
            row=obs[frame,phase]
            expected=0 if phase=="created" or phase=="submitted" and frame!=2 else (1 if frame==3 else -2)
            require(row["aggregate"]==row["point_status"]==expected,"Input sync_file error/status changed")
            require(row["driver"]=="sw_sync" if not expected else row["driver"] in
                    ["sw_sync","detached-driver"],"Wrong test-fence provider")
            require((row["timestamp_ns"]>0)==bool(expected),"Missing/false input signal timestamp")
    kernel=read("fence-trace.txt");events=trace.parse_events(kernel)
    stats=trace.parse_stats(read("trace-stats.txt"))
    count=re.search(r"entries-in-buffer/entries-written: (\d+)/(\d+)",kernel)
    require(count and int(count[1])==int(count[2])==sum(x["entries"] for x in stats.values()),
            "Trace accounting incomplete")
    require(read("global-trace-before.txt")==read("global-trace-after.txt"),"Global trace controls changed")
    waits=trace.analyze_display_waits(kernel,events)["display_commit_waits"]
    markers={}
    for match in re.finditer(r"(\d+\.\d+): tracing_mark_write: SQ2_ACQUIRE_TEST phase=(\S+) frame=(\d+) pid=(\d+) time_ns=(\d+)",kernel):
        stamp,phase,frame,pid,_=match.groups();key=int(frame),phase
        require(key not in markers,"Duplicate case marker");markers[key]={"ns":trace.ns(stamp),"pid":int(pid)}
    require((1,"invalid-submit") in markers and (1,"invalid-rejected") in markers,"Missing invalid input markers")
    previous=markers[1,"invalid-rejected"]["ns"];mapped=[]
    for frame in [2,3,4]:
        require((frame,"submit") in markers and (frame,"present-complete") in markers,"Missing case timing")
        start,end=markers[frame,"submit"],markers[frame,"present-complete"]
        init=[x for x in events if x["event"]=="dma_fence_init" and x["driver"]=="sw_sync" and
              x["pid"]==start["pid"] and previous<=x["ns"]<=start["ns"]]
        require(len(init)==1,"Ambiguous case-to-kernel fence identity")
        source=init[0];identity=source["context"],source["seqno"]
        signal=[x for x in events if x["event"]=="dma_fence_signaled" and (x["context"],x["seqno"])==identity]
        require(len(signal)==1,"Missing real kernel input signal")
        selected=[x for x in waits if (x["context"],x["seqno"])==identity]
        if frame==2:
            require(signal[0]["ns"]<=start["ns"] and len(selected)<=1,"Already-error input ordering differs")
            if selected:
                require(start["ns"]<=selected[0]["wait_start_ns"]<=selected[0]["wait_end_ns"]<=end["ns"] and
                        signal[0]["ns"]<=selected[0]["wait_start_ns"],
                        "Already-error input was not completed before the DPU wait")
        else:
            require(len(selected)==1,"Missing actual DPU commit wait")
            wait=selected[0]
            require(start["ns"]<=wait["wait_start_ns"]<signal[0]["ns"]<=wait["wait_end_ns"]<=end["ns"],
                    "Pending input did not signal/error during the DPU wait")
            require(wait["wait_start_ns"]<=obs[frame,"completed"]["timestamp_ns"]<=wait["wait_end_ns"],
                    "sync_file timestamp not inside observed wait")
            begin="producer-signal" if frame==3 else "producer-error"
            require((frame,begin) in markers and (frame,"producer-complete") in markers and
                    markers[frame,begin]["ns"]<=signal[0]["ns"]<=markers[frame,"producer-complete"]["ns"],
                    "Kernel signal not associated with controlled producer")
        mapped.append({"frame":frame,"context":identity[0],"seqno":identity[1],
            "input_error":frame!=3,"point_status":obs[frame,"completed"]["point_status"],
            "real_display_wait":selected})
        previous=end["ns"]
    require(read("sw-sync-final-info.txt").strip()=="objs:\n--------------\nfences:\n--------------",
            "Test timelines/fences remain")
    build=json.loads((composer_build/"BUILD.json").read_text())
    require(build.get("acquire_fence_validation") is True,"Wrong source build contract")
    expected=build["sha256"]["vendor.qti.hardware.display.composer-service"]
    require(read("composer-hash.txt").split()[0]==expected,"Wrong actual composer executable")
    runtime={Path(path).name:digest for digest,path in (line.split() for line in read("runtime-hashes.txt").splitlines())}
    for filename in ["libqservice.so","libqdutils.so","libgpu_tonemapper.so","libhistogram.so"]:
        require(runtime.get(filename)==build["sha256"][filename],"Wrong paired composer library")
    return {"status":"controlled-kernel-sync-file-errors-and-recovery-verified","invalid_fd_rejected_before_mutation":True,
        "unchanged_drm_on_invalid_input":True,"kernel_cases":mapped,"ownership_sub_audit":own,
        "scope":"Standard sw_sync test fences on CPU-initialized linear NV12, not GPU/VPU fault injection",
        "diagnostic_provider_resident_until_normal_reboot":True,"strict_dpu_pixels_verified":False,
        "full_fence_lifecycle_verified":False,"full_hwc_verified":False,"full_stack_complete":False,
        "evidence_sha256":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in capture.iterdir() if p.is_file() and p.suffix in [".txt",".exit"]}}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ["capture","composer-build","output"]:parser.add_argument("--"+key,type=Path,required=True)
    args=parser.parse_args();result=audit(args.capture,args.composer_build)
    args.output.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({"status":result["status"],"kernel_input_cases":len(result["kernel_cases"]),
                      "invalid_input_rejected":True,"full_stack_complete":False}))
if __name__=="__main__":main()
