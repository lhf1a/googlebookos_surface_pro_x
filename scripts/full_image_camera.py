"""Camera payload for baseline/Hybrid full-image builds; no device operations."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from disk_image import module, require
from kernel_baseline import ROOT, policy, validate_kernel_source, validate_build_tree

SENSORS = "qcom_spmi-regulator camcc-sc8180x i2c-qcom-cci v4l2-async v4l2-fwnode v4l2-cci ov5693 ov13858 ov7251".split()
CAMSS = "videobuf2-dma-sg qcom-camss".split()
MODULES = SENSORS + CAMSS
GRAPHICS = {
    "lib64/hw/mapper.quest2.so": "mapper.quest2.so",
    "lib64/sq2-qti-display/mapper.quest2.so": "mapper.quest2.so",
    "bin/hw/vendor.qti.hardware.display.allocator-service": "quest2-allocator5-service",
}
PREFIX = "etc/sq2-qti-system/"
BOOT = b'if [ "$rescue" = 1 ]; then\n'
BOOT_HOOK = b"""if [ "$smoke" = 0 ] && [ "$ssd_smoke" = 0 ] && [ "$rescue" = 0 ]; then
    /bin/spx-camera-boot > /logs/camera-boot.txt 2>&1 || echo SPX_CAMERA_NOT_READY
fi
"""
STAGE = b'qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n'
STAGE_HOOK = b'spx-camera-stage > /logs/camera-stage.txt 2>&1 || echo SPX_CAMERA_STAGE_FAILED\n'


def payload_paths():
    return ({name+".ko": "etc/spx-camera/modules/"+name+".ko" for name in MODULES}
            | {"provider": "etc/spx-camera/userspace/provider"}
            | {binary: PREFIX+"root/"+relative for relative, binary in GRAPHICS.items()})


def install_targets():
    return ({"etc/spx-camera/modules/"+name+".ko": name+".ko" for name in MODULES}
            | {"etc/spx-camera/userspace/provider": "provider"}
            | {PREFIX+"root/"+relative: binary for relative, binary in GRAPHICS.items()})


def sha(data):
    return hashlib.sha256(data).hexdigest()


def arm64(data, name):
    require(data[:6] == b"\x7fELF\x02\x01" and int.from_bytes(data[18:20], "little") == 183,
            "Camera input is not ARM64 ELF: "+name)


def import_inputs(initramfs, output):
    """Import only shared camera binaries; never copy identity, media or calibration."""
    from full_image import records_from_gzip
    _, bodies = records_from_gzip(initramfs)
    files = {name: bodies[path] for name, path in payload_paths().items()}
    require(bodies[PREFIX+"root/lib64/hw/mapper.quest2.so"] ==
            bodies[PREFIX+"root/lib64/sq2-qti-display/mapper.quest2.so"], "Mapper copies differ")
    return write_inputs(files, output)


def write_inputs(files, output, source_only=False):
    expected = set(payload_paths())
    if source_only: expected = {n for n in expected if not n.endswith('.ko')}
    require(set(files) == expected, "Incomplete camera input inventory")
    for name, data in files.items():
        arm64(data, name)
    output.mkdir(parents=True, exist_ok=False)
    for name, data in files.items():
        (output/name).write_bytes(data)
    manifest = {"schema": 2 if source_only else 1, "kernel_release": policy()["kernel_release"],
                "modules_require_source_build": source_only,
                "files": {n: sha(d) for n, d in files.items()}}
    (output/"CAMERA.json").write_text(json.dumps(manifest, indent=2)+"\n")
    load(output, source_build=source_only)
    return manifest


def load(inputs, source_build=False):
    manifest = json.loads((inputs/"CAMERA.json").read_text())
    require(manifest["schema"] in (1,2), "Unknown camera input schema")
    source_only = manifest['schema'] == 2
    require(not source_only or (source_build and manifest.get('modules_require_source_build') is True),
            "Source-only camera inputs require source kernel/module compilation")
    require(manifest["kernel_release"] == policy()["kernel_release"], "Camera kernel ABI mismatch")
    expected = set(payload_paths())
    if source_only: expected = {n for n in expected if not n.endswith('.ko')}
    require(set(manifest["files"]) == expected, "Incomplete camera input inventory")
    files = {}
    for name, expected in manifest["files"].items():
        data = (inputs/name).read_bytes()
        arm64(data, name)
        require(sha(data) == expected, "Camera input checksum mismatch: "+name)
        if name.endswith(".ko"):
            magic = subprocess.check_output(["modinfo", "-F", "vermagic", str(inputs/name)], text=True)
            require(magic.split()[0] == manifest["kernel_release"], "Camera module ABI mismatch: "+name)
        files[name] = data
    return files


def rebuild_modules(files, source, build, output, cross, run):
    """Source-kernel builds must not reuse same-release modules from an older build."""
    validate_kernel_source(source)
    validate_build_tree(build)
    sensors, camss = output/"camera-sensors", output/"camera-camss"
    for script, dest in (("build-spx-camera-probe.sh", sensors), ("build-spx-camss.sh", camss)):
        run(["env", "CROSS_COMPILE="+cross, "sh", ROOT/"scripts"/script, source, build, dest])
    for names, directory in ((SENSORS, sensors), (CAMSS, camss)):
        for name in names:
            files[name+".ko"] = (directory/(name+".ko")).read_bytes()


def prepare_dtb(data):
    import libfdt
    f = libfdt.Fdt(data)
    node = f.path_offset("/soc@0/isp@acb3000", quiet=(libfdt.NOTFOUND,))
    if node >= 0:
        require(b"qcom,sc8180x-camss" in bytes(f.getprop(node, "compatible")).split(b"\0"),
                "Unexpected existing camera receiver")
        # Already prepared inputs retain their resource references and reserved memory.
        for path in ("/soc@0/cci@ac4a000/i2c-bus@0/camera@10",
                     "/soc@0/cci@ac4a000/i2c-bus@1/camera@36"):
            require(f.path_offset(path, quiet=(libfdt.NOTFOUND,)) >= 0, "Incomplete camera DT")
        return data
    for name in ("prepare-spx-camera-test-dtb.py", "prepare-spx-camera-v4l2-dtb.py", "prepare-spx-camss-dtb.py"):
        data = module("camera_"+name.replace("-", "_"), name).prepare(data)
    return data


def install(records, bodies, files, new_record):
    init = bodies["init"]
    if b"/bin/spx-camera-boot " not in init:
        require(init.count(BOOT) == 1, "Missing camera module boot anchor")
        init = init.replace(BOOT, BOOT_HOOK+BOOT)
    if b"spx-camera-stage >" not in init:
        require(init.count(STAGE) == 1, "Camera requires QTI display stage")
        init = init.replace(STAGE, STAGE+STAGE_HOOK)
    bodies["init"] = init
    for target, binary in install_targets().items():
        if target.startswith(PREFIX):
            require(target in bodies, "QTI camera buffer target missing: "+target)
        bodies[target] = files[binary]
        records[target] = new_record(0o100755 if binary in ("provider", "quest2-allocator5-service") else 0o100644)
    require(bodies[PREFIX+"root/lib64/hw/mapper.quest2.so"] ==
            bodies[PREFIX+"root/lib64/sq2-qti-display/mapper.quest2.so"], "Mapper copies differ")
    for name in ("spx-camera-boot", "spx-camera-stage"):
        bodies["bin/"+name] = (ROOT/"probe/camera"/name).read_bytes()
        records["bin/"+name] = new_record(0o100755)
    profiles = "etc/spx-camera/userspace/media_profiles_spx.xml"
    bodies[profiles] = (ROOT/"probe/camera/media_profiles_spx.xml").read_bytes()
    records[profiles] = new_record(0o100644)
    graphics_targets = {PREFIX+"root/"+p for p in GRAPHICS}
    for name in (PREFIX+"SHA256SUMS", PREFIX+"root/SHA256SUMS", PREFIX+"INSTALLED_SHA256SUMS"):
        if name not in bodies:
            continue
        lines = []
        for line in bodies[name].decode().splitlines():
            digest, relative = line.split(None, 1)
            if name == PREFIX+"INSTALLED_SHA256SUMS":
                target = PREFIX+"root/"+relative
            else:
                target = str(Path(name).parent/relative)
            if target in graphics_targets:
                digest = sha(bodies[target])
            lines.append(digest+"  "+relative)
        bodies[name] = ("\n".join(lines)+"\n").encode()
    return {"provider_sha256": sha(files["provider"]), "formats": ["JPEG", "PRIVATE", "YUV420_888"],
            "video": ["480p", "720p", "1080p"], "ir_enabled": True}


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--import-initramfs", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    print(json.dumps(import_inputs(a.import_initramfs, a.output), indent=2))
