#!/usr/bin/env python3
"""Join actual GPU AHB submissions, native sync_file observations and DPU commit waits."""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("dpu_trace",ROOT/"scripts/audit-sq2-dpu-fence-trace.py")
trace=importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)
require=trace.require

def rows(text):
    accepted={"GPU_TARGET","GPU_REUSE","GPU_TARGET_RETIRED","GPU_FRAME","GPU_TARGET_PASS","FENCE","FENCE_POINT"}
    result=[]
    for line in text.splitlines():
        fields=line.split()
        if not fields or fields[0] not in accepted:continue
        row={}
        for field in fields[1:]:
            key,value=field.split("=",1)
            require(key not in row,"Duplicate diagnostic field")
            try:row[key]=int(value)
            except ValueError:row[key]=value
        result.append((fields[0],row))
    return result

def planes(text):
    result=[]
    for match in re.finditer(r"^plane\[(\d+)\]:[^\n]*\n(.*?)(?=^(?:plane|crtc|connector)\[|\Z)",text,re.M|re.S):
        block=match[2]
        crtc=re.search(r"^\tcrtc=(.+)$",block,re.M)
        fb=re.search(r"^\tfb=(\d+)$",block,re.M)
        require(crtc and fb,"Incomplete DRM plane")
        if crtc[1]=="(null)" or not int(fb[1]):continue
        fmt=re.search(r"^\t\tformat=(\S+)",block,re.M)
        modifier=re.search(r"^\t\tmodifier=(0x[0-9a-f]+)$",block,re.M)
        size=re.search(r"^\t\tsize=(\d+)x(\d+)$",block,re.M)
        require(fmt and modifier and size,"Missing framebuffer identity")
        result.append({"plane":int(match[1]),"crtc":crtc[1],"fb":int(fb[1]),"format":fmt[1],
                       "modifier":int(modifier[1],16),"width":int(size[1]),"height":int(size[2])})
    require(len(set(re.findall(r"^msm dev=.+$",text,re.M)))==1,"Ambiguous DRM driver instance")
    return [dict(items) for items in dict.fromkeys(tuple(row.items()) for row in result)]

def audit(capture,allow_inherited=False):
    read=lambda n:(capture/n).read_text()
    require(read("probe.exit").strip()==read("runner.exit").strip()=="0","Diagnostic failed")
    require("RESTORE_COMPLETE" in read("runner.txt") and "WAKELOCK_RELEASED" in read("wake.txt")
            and "status=0" in read("wake.txt"),"Restoration/holder release missing")
    records=rows(read("probe.txt"))
    get=lambda kind:[x for name,x in records if name==kind]
    targets=get("GPU_TARGET")
    require(len(targets)==3 and {x["slot"] for x in targets}=={0,1,2},"Missing target slots")
    ids={x["slot"]:x["id"] for x in targets}
    require(len(set(ids.values()))==3,"Target IDs are not distinct")
    require(len({(x["width"],x["height"]) for x in targets})==1,"Target dimensions differ")
    width,height=targets[0]["width"],targets[0]["height"]
    require(width>0 and height>0,"Invalid target dimensions")
    frames=get("GPU_FRAME")
    require(len(frames)==30 and [x["frame"] for x in frames]==list(range(30)),"Missing GPU frames")
    for x in frames:
        require(x["slot"]==x["frame"]%3 and x["id"]==ids[x["slot"]] and
                x["cached"]==int(x["frame"]>=3) and x["pixels"]==width*height and x["exact"]==1,
                "GPU buffer/cache/pixel attribution differs")
    retire=get("GPU_TARGET_RETIRED")
    require(len(retire)==30 and [x["frame"] for x in retire]==list(range(30)),"Missing target retirements")
    retired={x["frame"]:x for x in retire}
    for x in retire:
        require(x["replaced_by"]==x["frame"]+1 and x["slot"]==x["frame"]%3 and x["id"]==ids[x["slot"]],
                "Target retirement belongs to wrong frame")
    reuse=get("GPU_REUSE")
    require(len(reuse)==27 and [x["frame"] for x in reuse]==list(range(3,30)),"Missing target reuse")
    for x in reuse:
        require(x["previous"]==x["frame"]-3 and x["slot"]==x["frame"]%3 and x["id"]==ids[x["slot"]] and
                retired[x["previous"]]["time_ns"]<=x["time_ns"],"GPU reused a live scanout target")
    expected={"frames":30,"targets":3,"reuses":27,"native_acquires":30,"gpu_pixels_exact":1,"final_flush":1,"full_stack_complete":0}
    require(get("GPU_TARGET_PASS")==[expected],"Missing GPU completion")
    fences,points=get("FENCE"),get("FENCE_POINT")
    require(all(x["status"]>=0 for x in fences+points),"Negative fence status observed")
    acquired={}
    for kind,phases in (("gpu_acquire",("exported","completed")),("gpu_present",("before","after"))):
        for phase in phases:
            selected=[x for x in fences if x["kind"]==kind and x["phase"]==phase]
            require(len(selected)==30 and [x["frame"] for x in selected]==list(range(30)),"Missing GPU fence observations")
            require(all(x["slot"]==x["frame"]%3 and x["points"]==1 for x in selected),"Ambiguous GPU fence points")
            pp=[x for x in points if x["kind"]==kind and x["phase"]==phase]
            require(len(pp)==30 and [x["frame"] for x in pp]==list(range(30)),"Missing GPU sync_file points")
            if phase in ("completed","after"):
                require(all(x["status"]==1 for x in selected) and
                        all(x["status"]==1 and x["timestamp_ns"]>0 for x in pp),"GPU/display fence did not signal")
            if kind=="gpu_acquire" and phase=="exported":acquired={x["frame"]:x for x in selected}
    flush=[x for x in fences if x["kind"]=="gpu_flush" and x["phase"]=="after"]
    require(len(flush)==1 and flush[0]["status"]==1,"Final GPU target was not flushed")
    text=read("fence-trace.txt")
    stats=trace.parse_stats(read("trace-stats.txt"))
    accounting=re.search(r"entries-in-buffer/entries-written: (\d+)/(\d+)",text)
    require(accounting and int(accounting[1])==int(accounting[2])==sum(s["entries"] for s in stats.values()),"Trace accounting differs")
    require(read("global-trace-before.txt")==read("global-trace-after.txt"),"Global tracing controls changed")
    events=trace.parse_events(text)
    waits=trace.analyze_display_waits(text,events)["display_commit_waits"]
    markers={}
    for line in text.splitlines():
        match=re.search(r"(\d+\.\d+): tracing_mark_write: SQ2_GPU phase=(\w+) frame=(\d+) slot=(\d+) id=(\d+) pid=(\d+) time_ns=(\d+)",line)
        if not match:continue
        stamp,phase,frame,slot,identity,pid,app_ns=match.groups()
        key=(int(frame),phase)
        require(key not in markers,"Duplicate GPU marker")
        markers[key]={"ns":trace.ns(stamp),"slot":int(slot),"id":int(identity),"pid":int(pid),"app_ns":int(app_ns)}
    phases=("render_begin","render_exported","present_begin","present_complete")
    require(set(markers)=={(f,p) for f in range(30) for p in phases},"Missing GPU phase markers")
    frame_map=[]
    init=[x for x in events if x["event"]=="dma_fence_init"]
    signals={(x["context"],x["seqno"]):x for x in events if x["event"]=="dma_fence_signaled"}
    for frame in range(30):
        mm=[markers[frame,phase] for phase in phases]
        require(all(x["slot"]==frame%3 and x["id"]==ids[frame%3] for x in mm),"Marker references wrong AHB")
        require([x["ns"] for x in mm]==sorted(x["ns"] for x in mm),"GPU marker order differs")
        ww=[x for x in waits if mm[2]["ns"]<=x["wait_start_ns"]<=x["wait_end_ns"]<=mm[3]["ns"]]
        require(len(ww)==1 and ww[0]["GPU_scheduler_identity_observed"],"Ambiguous/missing display wait for frame")
        wait=ww[0]
        source=[x for x in init if (x["context"],x["seqno"])==(wait["context"],wait["seqno"])]
        require(len(source)==1 and source[0]["pid"]==mm[0]["pid"] and
                mm[0]["ns"]<=source[0]["ns"]<=mm[1]["ns"],"Waited fence was not produced by this frame")
        release=[x for x in init if x["driver"]=="msm-dpu" and x["timeline"]=="dpu-video-buffer-release" and
                 mm[2]["ns"]<=x["ns"]<=mm[3]["ns"]]
        require(len(release)==1,"Missing/ambiguous DPU release generation")
        release=release[0];signal=signals.get((release["context"],release["seqno"]))
        require(signal is not None,"GPU target release did not signal")
        require(signal["ns"]<=retired[frame]["time_ns"],"Target retired before true DPU release")
        if frame<29:
            nxt=[markers[frame+1,p] for p in ("present_begin","present_complete")]
            require(nxt[0]["ns"]<=signal["ns"]<=nxt[1]["ns"],"DPU release outside replacement commit")
        else:
            require(signal["ns"]<=flush[0]["time_ns"],"Final release not completed by flush")
        frame_map.append({"frame":frame,"slot":frame%3,"ahb_id":ids[frame%3],
                          "acquire_context":wait["context"],"acquire_seqno":wait["seqno"],
                          "acquire_pending_at_export":acquired[frame]["status"]==0,
                          "GPU_signal_during_kernel_display_wait":wait["pending_GPU_fence_signaled_during_display_wait"],
                          "release_context":release["context"],"release_seqno":release["seqno"],
                          "release_signal_ns":signal["ns"],"retired_ns":retired[frame]["time_ns"],
                          "kernel_wait_stack":wait["call_stack"]})
    require(len({(x["acquire_context"],x["acquire_seqno"]) for x in frame_map})==30,"Duplicate GPU acquire identity")
    pending=sum(x["GPU_signal_during_kernel_display_wait"] for x in frame_map)
    require(pending>0,"No actual pending GPU display wait proven")
    active=planes(read("gpu-drm.txt"))
    actual=[x for x in active if x["format"]=="AB24" and x["modifier"]==0x0500000000000001 and
            (x["width"],x["height"])==(width,height)]
    require(len(actual)==1,"Missing unique physical UBWC client target")
    extra=[x for x in active if x["crtc"]==actual[0]["crtc"] and x["plane"]!=actual[0]["plane"]]
    if not allow_inherited:require(not extra,"Inherited/unrequested plane remains on target CRTC")
    require("GPU_TARGET" in read("gpu-hwc.txt") and "RGBA_8888_UBWC" in read("gpu-hwc.txt"),"HWC did not select UBWC GPU target")
    return {"status":"GPU_acquire_buffer_ownership_and_handoff_verified" if not extra else "GPU_acquire_verified_inherited_plane_remaining",
            "frames":30,"targets":3,"cached_reuses":27,"native_acquires":30,
            "GPU_readback_pixels_exact":30*width*height,"strict_DPU_pixels_verified":False,
            "exported_acquires_pending":sum(x["acquire_pending_at_export"] for x in frame_map),
            "kernel_GPU_display_waits":30,"pending_GPU_signals_during_display_wait":pending,
            "frame_buffer_fence_identity_map":frame_map,"physical_target":actual[0],"extra_planes_on_target_crtc":extra,
            "inherited_plane_handoff_verified":not extra,"trace_entries":int(accounting[1]),
            "trace_loss_observed":False,"global_trace_controls_unchanged":True,
            "fence_error_injection_verified":False,"full_fence_lifecycle_verified":False,
            "full_hwc_verified":False,"full_stack_complete":False,
            "limits":["GPU readback is not a DPU pixel readback or panel latency measurement.",
                      "Physical RGB UBWC client targets; decoder-produced NV12 acquire ownership remains separate.",
                      "No fence-error injection, HDR/secure, PM/performance or cross-device acceptance."]}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture",type=Path)
    parser.add_argument("--composer-build",type=Path,required=True)
    parser.add_argument("--display-build",type=Path,required=True)
    parser.add_argument("--allow-inherited-plane",action="store_true")
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    result=audit(args.capture,args.allow_inherited_plane)
    build=json.loads((args.composer_build/"BUILD.json").read_text())
    digest=build["sha256"]["vendor.qti.hardware.display.composer-service"]
    require((args.capture/"composer-hash.txt").read_text().split()[0]==digest,"Wrong actual composer")
    result["composer_sha256"]=digest
    display=json.loads((args.display_build/"BUILD.json").read_text())
    expected={name:digest for report in (display,build) for name,digest in report["sha256"].items() if name.endswith(".so")}
    hash_file=args.capture/"runtime-hashes.txt"
    if hash_file.exists():
        hashes={Path(line.split()[1]).name:line.split()[0] for line in hash_file.read_text().splitlines()}
        require(all(hashes.get(name)==digest for name,digest in expected.items()),"Actual display libraries do not match paired builds")
        result["runtime_library_hashes_verified"]=True
        result["display_library_sha256"]=expected
    else:
        require(args.allow_inherited_plane,"Actual display library hashes absent")
        result["runtime_library_hashes_verified"]=False
    result["evidence_sha256"]={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in args.capture.iterdir()
                              if f.is_file() and f.suffix in (".txt",".exit",".sh")}
    args.output.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({k:result[k] for k in ("status","frames","pending_GPU_signals_during_display_wait",
                                         "inherited_plane_handoff_verified")},indent=2))
if __name__=="__main__":main()
