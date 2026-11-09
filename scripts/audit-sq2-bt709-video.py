#!/usr/bin/env python3
"""Audit a normal OMX/MediaCodec BT.709 native Surface experiment offline."""
import argparse
import hashlib
import json
from pathlib import Path
import re

def require(ok, reason):
    if not ok:
        raise ValueError(reason)

def integer(text, name):
    value = re.search(r"(?<![\w-])" + re.escape(name) + r": int32\((-?\d+)\)", text)
    require(value, "Format field absent: " + name)
    return int(value[1])

def audit_frames(capture, name, reference, surface):
    text = (capture / (name + ".txt")).read_text()
    require((capture / (name + ".exit")).read_text().strip() == "0", name + " failed")
    require("COLOR standard=1 present=1 transfer=3 present=1 range=2 present=1" in text,
            "Missing actual OMX VUI -> MediaCodec BT.709 limited metadata")
    require("TRANSPORT sent=31 frames=30 eos=1 failed=0 stop=0 delete=0" in text,
            "Decode lifecycle incomplete")
    current, frames = None, []
    for line in text.splitlines():
        if line.startswith("FORMAT "):
            current = line
        found = re.fullmatch(r"OUTPUT pts=(\d+) size=(\d+) offset=(\d+) flags=(\d+) capacity=(\d+)", line)
        if found and int(found[2]):
            require(current is not None, "Output before format")
            frames.append((tuple(map(int, found.groups())), current))
    require(len(frames) == 30 and [f[0][0] for f in frames] == [i * 33333 for i in range(30)],
            "Frame count/PTS mismatch")
    normalized = []
    for index, (entry, fmt) in enumerate(frames):
        require(integer(fmt, "color-format") == (0x7fa30c04 if surface else 21),
                "Unexpected native/linear format")
        raw = (capture / (name + "-frames") / ("sq2-ndk-frame-%d.bin" % index)).read_bytes()
        if surface:
            require(len(raw) == 115200, "ImageReader I420 size mismatch")
            uv = bytearray(38400)
            uv[0::2] = raw[76800:96000];uv[1::2] = raw[96000:]
            frame = raw[:76800] + uv
        else:
            stride, scan = integer(fmt, "stride"), integer(fmt, "slice-height")
            require(stride >= 320 and scan >= 240 and len(raw) == entry[1] and
                    len(raw) >= stride * scan * 3 // 2, "Output layout out of bounds")
            frame = b"".join(raw[y*stride:y*stride+320] for y in range(240))
            frame += b"".join(raw[stride*scan+y*stride:stride*scan+y*stride+320] for y in range(120))
        require(frame == reference[index*115200:(index+1)*115200], "Decoded pixel mismatch")
        normalized.append(frame)
    require(b"".join(normalized) == reference, "Reference frame length differs")
    if surface:
        images = re.findall(r"IMAGE index=(\d+).*?timestamp_ns=(\d+)", text)
        require([(int(i), int(t)) for i,t in images] == [(i, i*33333000) for i in range(30)],
                "ImageReader timestamps mismatch")
        require("using-sw-renderer: int32(1)" not in text, "Unexpected software renderer")
    return {"frames": 30, "pixels_exact": True, "pts_exact": True, "native_surface": surface}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("capture", "reference", "composer-build", "display-build", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    capture = args.capture
    reference = args.reference.read_bytes()
    require(len(reference) == 30 * 115200, "Expected a 30-frame 320x240 NV12 reference")
    phases = {n: audit_frames(capture, "bt709-" + n, reference, n == "surface")
              for n in ("bytebuffer", "surface")}
    text = (capture / "bt709-window.txt").read_text()
    require((capture / "bt709-window.exit").read_text().strip() == "0" and
            "NATIVE_VIDEO_DECODE_RETURN result=0 strict_pass=true" in text and
            "NATIVE_VIDEO_SURFACE_RELEASED" in text, "Physical window did not finish cleanly")
    require("COLOR standard=1 present=1 transfer=3 present=1 range=2 present=1" in text,
            "Physical window lost VUI metadata")
    releases = re.findall(r"WINDOW_RELEASE index=(\d+) pts_us=(\d+) target_ns=(\d+) result=(-?\d+)", text)
    callbacks = re.findall(r"WINDOW_CALLBACK index=(\d+) pts_us=(\d+)", text)
    require(len(releases) == len(callbacks) == 30 and all(int(v[3]) == 0 for v in releases),
            "Physical window submission/callback mismatch")
    require([(int(v[0]), int(v[1])) for v in releases] ==
            [(int(v[0]), int(v[1])) for v in callbacks] == [(i, i*33333) for i in range(30)],
            "Physical window PTS mismatch")
    sf = (capture / "sf-bt709.txt").read_text()
    require("dataspace:0x10c10000" in sf and "composition: Device/Device" in sf,
            "HWC did not consume BT.709 as DEVICE")
    drm = (capture / "drm-bt709.txt").read_text().split("DRM_DEBUG_PATH=")[1]
    active = [p for p in re.split(r"(?m)^plane\[", drm)[1:] if
              re.search(r"(?m)^\s+crtc=crtc-\d+$", p.split("crtc[")[0])]
    nv12 = [p for p in active if "format=NV12 " in p]
    require(len(nv12) == 1 and "color-encoding=ITU-R BT.709 YCbCr" in nv12[0] and
            "color-range=YCbCr limited range" in nv12[0], "Actual NV12 KMS color properties disagree")
    after = (capture / "drm-bt709-after.txt").read_text().split("DRM_DEBUG_PATH=")[1]
    require(not any("format=NV12 " in p and re.search(r"(?m)^\s+crtc=crtc-\d+$", p.split("crtc[")[0])
                    for p in re.split(r"(?m)^plane\[", after)[1:]), "Video plane still active after close")
    crc = (capture / "bt709-crc.txt").read_text()
    require((capture / "bt709-crc.exit").read_text().strip() == "0" and
            "samples=720 requested=720" in crc and "CRC_OBSERVER_RESTORED source=none" in crc,
            "CRC observer failed")
    require("NORMAL_GROUP_EXIT=0" in (capture / "runner.txt").read_text() and
            "RESTORE_COMPLETE" in (capture / "runner.txt").read_text(), "Outer runner incomplete")
    for filename in ("normal-runner.exit",):
        require((capture / filename).read_text().strip() == "0", "Group failed: " + filename)
    runtime = dict((Path(name).name, digest) for digest, name in
                   (line.split() for line in (capture / "runtime-hashes.txt").read_text().splitlines()))
    for directory in (args.display_build, args.composer_build):
        build = json.loads((directory / "BUILD.json").read_text())
        for name, digest in build["sha256"].items():
            if name in runtime:
                require(runtime[name] == digest, "Runtime library does not match paired build")
    composer = args.composer_build / "vendor.qti.hardware.display.composer-service"
    require((capture / "composer-hash.txt").read_text().split()[0] ==
            hashlib.sha256(composer.read_bytes()).hexdigest(), "Wrong running composer")
    result = {"status": "bt709-normal-native-surface-device-verified", "phases": phases,
              "physical_window_outputs": 30, "physical_window_callbacks": 30,
              "actual_kms_encoding": "BT.709 limited", "crc_samples": 720,
              "reference_sha256": hashlib.sha256(reference).hexdigest(),
              "strict_dpu_pixels_verified": False, "full_hwc_verified": False, "full_stack_complete": False}
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))

if __name__ == "__main__":
    main()
