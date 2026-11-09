#!/usr/bin/env python3
"""Build a private normal-Android QTI/mapper5 initramfs candidate; never deploy it."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import tempfile
import subprocess
import xml.etree.ElementTree as ET
import zlib

ROOT = Path(__file__).resolve().parents[1]
PREFIX = "etc/sq2-qti-system"
HOOK = b"quest2-hidl-stage > /logs/hidl-stage.txt 2>&1 || fail hidl-stage\n"
NEW_HOOK = b"qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n"
PRIVATE_LIB = "lib64/sq2-qti-display/"
MAPPER_LIBS = "gralloc.kona.so libgralloccore.so libgrallocutils.so libqdMetaData.so libquest2-system-ion.so libadreno_utils.so libgsl.so libq2v77glnext.so libq2v77qgl.so libEGL_adreno.so libGLESv2_adreno.so eglSubDriverAndroid.so mapper.quest2.so".split()
EGL = {"libEGL_adreno.so", "libGLESv2_adreno.so", "eglSubDriverAndroid.so"}
OPTIONAL_EXTENSIONS = ["libsdmextension.so", "libsdm-color.so", "libsnapdragoncolor-manager.so", "libdpps.so"]
BASE_GLES1 = "etc/sq2-graphics-full/libGLESv1_CM_adreno.so"
BASE_GLES1_SHA256 = "396987f605b4f7aa1be5d10cacf0216bce400a3c13bb36f9cf29fac1d9bc5ace"
LEGACY_GRAPHICS_ADAPTERS = "libquest2-vulkan-wrapper.so vulkan-v77-bridge.so libqti-mapper3-bridge.so libquest2-v77-ahb.so libquest2-egl-wrapper.so".split()
LEGACY_MAPPERS = ["android.hardware.graphics.mapper@4.0-impl-qti-display.so", "mapper.minigbm.so", "mapper.qti.so"]
CONTEXT_ADDITIONS = {
    "etc/selinux/vendor_file_contexts": "configs/sq2-qti-system-file-contexts",
    "etc/selinux/vendor_service_contexts": "configs/sq2-qti-system-service-contexts",
    "etc/selinux/vendor_hwservice_contexts": "configs/sq2-qti-system-hwservice-contexts",
}

REPLACEMENTS = {
    "etc/init/hwc3-drm.rc": "configs/sq2-qti-system-composer.rc",
    "etc/init/vendor.qti.hardware.display.allocator-service.rc": "configs/sq2-qti-system-allocator.rc",
    "etc/vintf/manifest/hwc3-drm.xml": "configs/sq2-qti-system-composer.xml",
    "etc/vintf/manifest/vendor.qti.hardware.display.allocator-service.xml": "configs/sq2-qti-system-allocator.xml",
    "etc/vintf/manifest/mapper.qti.xml": "configs/quest2-native-mapper5.xml",
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_hash(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load_module(name):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), ROOT / "scripts" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def scan(path, keep=()):
    """Hash effective newc entries without retaining modules, credentials or other payloads."""
    records, bodies = {}, {}
    with gzip.open(path, "rb") as stream:
        while True:
            first = stream.read(1)
            if not first:
                break
            if first == b"\0":
                continue
            header = first + stream.read(109)
            require(len(header) == 110 and header[:6] in (b"070701", b"070702"), "Invalid newc header")
            fields = tuple(int(header[6 + i * 8:14 + i * 8], 16) for i in range(13))
            size, length = fields[6], fields[11]
            raw_name = stream.read(length)
            require(length > 0 and len(raw_name) == length and raw_name[-1:] == b"\0", "Invalid name")
            name = raw_name[:-1].decode().removeprefix("./")
            require(not name.startswith("/") and ".." not in Path(name).parts, "Unsafe archive path")
            stream.read(-(110 + length) % 4)
            hasher, left, saved, checksum = hashlib.sha256(), size, [], 0
            while left:
                chunk = stream.read(min(left, 1024 * 1024))
                require(bool(chunk), "Truncated cpio payload")
                left -= len(chunk)
                hasher.update(chunk)
                if header[:6] == b"070702":
                    checksum = (checksum + sum(chunk)) & 0xffffffff
                if name in keep:
                    saved.append(chunk)
            stream.read(-size % 4)
            require(header[:6] != b"070702" or checksum == fields[12], "newc checksum mismatch")
            if name != "TRAILER!!!":
                records[name] = (header[:6].decode(), fields, hasher.hexdigest())
                if name in keep:
                    bodies[name] = b"".join(saved)
    return records, bodies


def credential_tail(base):
    """Keep the exact pre-existing private credential gzip member last, without exposing it."""
    cpio = load_module("pack-sq2-vulkan")
    rest, tail = base, b""
    while rest:
        dec = zlib.decompressobj(31)
        data = dec.decompress(rest)
        require(dec.eof, "Truncated gzip member")
        used = len(rest) - len(dec.unused_data)
        files = cpio.entries(data)
        if "etc/shadow" in files and "bin/dropbear" in files:
            tail = rest[:used]
        rest = dec.unused_data
    return tail


def executable_sections(data):
    """ELF64 executable section bytes, independent of moved dynamic/string tables."""
    require(data[:6]==b"\x7fELF\x02\x01", "Expected ELF64 little endian")
    offset=struct.unpack_from("<Q",data,40)[0]
    entry_size,count,strings=struct.unpack_from("<HHH",data,58)
    require(entry_size==64 and count>strings, "Unknown ELF section table")
    headers=[struct.unpack_from("<IIQQQQIIQQ",data,offset+i*entry_size) for i in range(count)]
    names_header=headers[strings];names=data[names_header[4]:names_header[4]+names_header[5]]
    result={}
    for h in headers:
        if h[2]&4:
            name=names[h[0]:].split(b"\0",1)[0].decode()
            result[name]=data[h[4]:h[4]+h[5]]
    require(bool(result), "ELF has no executable sections")
    return result


def patch_codec2_mapper_dependency(path, expected):
    original=Path(path).read_bytes()
    require(digest(original)==expected, "Unknown normal QTI Codec2 input")
    with tempfile.TemporaryDirectory(prefix="sq2-codec2-link-") as directory:
        target=Path(directory)/"libqcodec2_utils.so";target.write_bytes(original)
        before=subprocess.check_output(["patchelf","--print-needed",str(target)],text=True).splitlines()
        require(before.count("libmapperutils.so")==1 and before.count("mapper.qti.so")==1,
                "Unexpected Codec2 mapper dependency contract")
        subprocess.run(["patchelf","--remove-needed","libmapperutils.so",str(target)],check=True)
        result=target.read_bytes()
        after=subprocess.check_output(["patchelf","--print-needed",str(target)],text=True).splitlines()
        require(after==[n for n in before if n!="libmapperutils.so"], "Unexpected Codec2 dependency mutation")
        require(executable_sections(original)==executable_sections(result), "Codec2 executable code changed")
        return result


def stage_script():
    return """#!/bin/busybox sh
# Called once before original Android init; modifies only the vendor RAM overlay.
set -eu
source=/etc/sq2-qti-system
vendor=/newroot/vendor
system_ext=/newroot/system_ext
hidl_stage=/newroot/debug_ramdisk/sq2-qti-hidl-bin
grep -q '^overlay /newroot/vendor overlay ' /proc/mounts
(cd "$source"; sha256sum -c SHA256SUMS)
# A second invocation is accepted only if the entire installed payload still matches.
if ! (cd "$vendor"; sha256sum -c "$source/ORIGINAL_SHA256SUMS"); then
    (cd "$vendor"; sha256sum -c "$source/INSTALLED_SHA256SUMS")
fi
video_rc="$vendor/etc/init/quest2-hidl.rc"
if [ -e "$video_rc" ]; then
    cmp -s "$video_rc" "$source/hidl-support/original-video.rc" ||
        cmp -s "$video_rc" "$source/hidl-support/video-services.rc"
fi
# Display requires the same real HIDL manager even when video=0 or its experiment flag is absent.
if ! cmp -s "$system_ext/bin/hwservicemanager" "$source/hidl-support/hwservicemanager"; then
    mkdir -p "$hidl_stage/lower" "$hidl_stage/upper" "$hidl_stage/work"
    mount --bind "$system_ext/bin" "$hidl_stage/lower"
    mount -t overlay overlay -o lowerdir=$hidl_stage/lower,upperdir=$hidl_stage/upper,workdir=$hidl_stage/work "$system_ext/bin"
    cp -p "$source/hidl-support/hwservicemanager" "$system_ext/bin/hwservicemanager"
fi
if ! cmp -s "$system_ext/etc/vintf/manifest.xml" "$source/hidl-support/framework-manifest.xml"; then
    mount --bind "$source/hidl-support/framework-manifest.xml" "$system_ext/etc/vintf/manifest.xml"
fi
# Move only the common manager declaration; retain the video service verbatim when enabled.
if [ -e "$video_rc" ]; then
    cp -p "$source/hidl-support/video-services.rc" "$video_rc"
fi
# No private Binder context, new Power HAL owner or user-data changes.
while IFS= read -r name; do
    test -n "$name"
    target="$vendor/$name"
    test ! -d "$target"
    mkdir -p "${target%/*}"
    rm -f "$target"
    cp -p "$source/root/$name" "$target"
done < "$source/INSTALL_FILES"
while read -r name destination; do
    test -n "$name" && test -n "$destination"
    target="$vendor/$name"
    test ! -d "$target"
    mkdir -p "${target%/*}"
    rm -f "$target"
    ln -s "$destination" "$target"
done < "$source/SYMLINKS"
# Optional C++ SDM extensions are not ABI-matched to the source implementation.
for name in libsdmextension.so libsdm-color.so libsnapdragoncolor-manager.so libdpps.so; do
    test ! -d "$vendor/lib64/$name"
    rm -f "$vendor/lib64/$name"
done
# These are the three old mapper implementations observed in the normal image.
# Keep HIDL interface libraries and unrelated hardware modules.
for name in android.hardware.graphics.mapper@4.0-impl-qti-display.so mapper.minigbm.so mapper.qti.so; do
    test ! -d "$vendor/lib64/hw/$name"
    rm -f "$vendor/lib64/hw/$name"
done
# Retire only the audited synthetic-handle adapters after native EGL/Vulkan replace their selectors.
# GLES1 is installed explicitly and shares the native GSL closure; interface libraries remain.
for name in libquest2-vulkan-wrapper.so vulkan-v77-bridge.so libqti-mapper3-bridge.so libquest2-v77-ahb.so libquest2-egl-wrapper.so; do
    test ! -d "$vendor/lib64/egl/$name"
    rm -f "$vendor/lib64/egl/$name"
done
(cd "$vendor"; sha256sum -c "$source/INSTALLED_SHA256SUMS")
echo QTI_SYSTEM_DISPLAY_STAGED_NORMAL_BINDER_MAPPER5
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("base", "display-build", "composer-build", "mapper-build", "vulkan-build", "runtime-stage", "android-contract", "codec2-library", "output"):
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--expected-base-sha256", required=True)
    args = parser.parse_args()
    require(not args.output.exists(), "Choose a fresh private output directory")
    require(file_hash(args.base) == args.expected_base_sha256, "Base artifact hash differs")
    hidl_names = {"etc/sq2-hidl/" + name for name in
                  ("hwservicemanager", "framework-manifest.xml", "service.rc", "SHA256SUMS")}
    before, selected = scan(args.base, {"init", BASE_GLES1} | hidl_names)
    hidl_hashes = dict((name, expected) for expected, name in
                       (line.split() for line in selected["etc/sq2-hidl/SHA256SUMS"].decode().splitlines()))
    for name in ("hwservicemanager", "framework-manifest.xml", "service.rc"):
        require(digest(selected["etc/sq2-hidl/" + name]) == hidl_hashes[name], "Base HIDL input differs: " + name)
    original_video_rc = selected["etc/sq2-hidl/service.rc"]
    manager, video = original_video_rc.split(b"\n\nservice sq2-omx ", 1)
    require(manager.startswith(b"service hwservicemanager /system_ext/bin/hwservicemanager\n"), "Unknown shared HIDL manager declaration")
    video = b"service sq2-omx " + video
    require(b"service hwservicemanager" not in video, "Duplicate HIDL manager in video service")
    hidl_support = {
        "hwservicemanager": (0o100755, selected["etc/sq2-hidl/hwservicemanager"]),
        "framework-manifest.xml": (0o100644, selected["etc/sq2-hidl/framework-manifest.xml"]),
        "original-video.rc": (0o100644, original_video_rc),
        "video-services.rc": (0o100644, video),
    }
    init = selected["init"]
    require(init.count(HOOK) == 1 and NEW_HOOK not in init, "Unexpected HIDL/graphics boot hook")
    require(b"qualcomm-full-start >" in init, "Requires current full Qualcomm base")
    inputs = {str(Path(__file__).resolve()): file_hash(Path(__file__)),
              str(ROOT / "scripts/pack-sq2-vulkan.py"): file_hash(ROOT / "scripts/pack-sq2-vulkan.py")}
    payload, links = {}, {}

    def put(name, data, mode=0o100644, origin=None):
        require(not name.startswith("/") and ".." not in Path(name).parts, "Unsafe vendor path")
        if name in payload:
            require(payload[name] == (mode, data), "Conflicting dependency: " + name)
        payload[name] = (mode, data)
        if origin:
            inputs[str(Path(origin).resolve())] = digest(data)

    def read_verified(path, expected):
        data = path.read_bytes()
        require(digest(data) == expected, "Input artifact hash differs: " + str(path))
        inputs[str(path.resolve())] = expected
        return data

    def elf(data, name):
        require(data[:6] == b"\x7fELF\x02\x01" and int.from_bytes(data[18:20], "little") == 183,
                "Expected ARM64 ELF: " + name)

    builds = {}
    for role, directory in (("display", args.display_build), ("composer", args.composer_build), ("mapper", args.mapper_build)):
        path = directory / "BUILD.json"
        manifest = json.loads(path.read_text())
        inputs[str(path.resolve())] = file_hash(path)
        builds[role] = manifest
    require(Path(builds["composer"]["source_display_build"]).resolve() == args.display_build.resolve(),
            "Composer and display builds are not paired")
    require(builds["composer"].get("empty_video_flush") and builds["display"].get("dpu_flush_fence"),
            "Require real empty-video flush and present fence fixes")
    for role, directory in (("display", args.display_build), ("composer", args.composer_build)):
        for name, expected in builds[role]["sha256"].items():
            if not name.endswith(".so"):
                continue
            data = read_verified(directory / name, expected)
            elf(data, name)
            put(PRIVATE_LIB + name, data)
    service = "vendor.qti.hardware.display.composer-service"
    put("bin/hw/" + service, read_verified(args.composer_build / service, builds["composer"]["sha256"][service]), 0o100755)
    manifest = json.loads((args.runtime_stage / "STAGE.json").read_text())
    inputs[str((args.runtime_stage / "STAGE.json").resolve())] = file_hash(args.runtime_stage / "STAGE.json")
    donors = {name: expected for name, expected in manifest["sha256"].items() if name.startswith("donor-lib64/") and name.endswith(".so")}
    require(bool(donors), "No tested donor interface closure")
    for name, expected in donors.items():
        data = read_verified(args.runtime_stage / name, expected)
        elf(data, name)
        put(PRIVATE_LIB + Path(name).name, data)
    for name in MAPPER_LIBS:
        data = read_verified(args.mapper_build / name, builds["mapper"]["sha256"][name])
        elf(data, name)
        put(PRIVATE_LIB + name, data)
        if name == "mapper.quest2.so" or name == "gralloc.kona.so":
            put("lib64/hw/" + name, data)
        elif name in EGL:
            put("lib64/egl/" + name, data)
        else:
            put("lib64/" + name, data)
            # Existing EGL $ORIGIN dependencies must also use the native-handle closure.
            links["lib64/egl/" + name] = "../" + name
    # Preserve the exact existing GLES1 driver without depending on the old qcom selector.
    gles1 = selected[BASE_GLES1]
    require(digest(gles1) == BASE_GLES1_SHA256, "Unknown inherited GLES1 input")
    elf(gles1, BASE_GLES1)
    put("lib64/egl/libGLESv1_CM_adreno.so", gles1)
    put("lib64/egl/qgl_config.txt", read_verified(args.mapper_build / "qgl_config.txt", builds["mapper"]["sha256"]["qgl_config.txt"]))
    put("lib64/qgl_config.txt", payload["lib64/egl/qgl_config.txt"][1])
    for target, source in {"lib64/gralloc.kona.so": "hw/gralloc.kona.so", "lib64/hw/gralloc.default.so": "gralloc.kona.so",
                           "lib64/egl/libEGL_freedreno.so": "libEGL_adreno.so", "lib64/egl/libGLESv2_freedreno.so": "libGLESv2_adreno.so",
                           "lib64/egl/libGLESv1_CM_freedreno.so": "libGLESv1_CM_adreno.so"}.items():
        links[target] = source
    vulkan_manifest = args.vulkan_build / "BUILD.json"
    vulkan = json.loads(vulkan_manifest.read_text())
    inputs[str(vulkan_manifest.resolve())] = file_hash(vulkan_manifest)
    require(vulkan.get("system_layout") and vulkan.get("memory_policy") and
            vulkan.get("native_ahb_and_gralloc_imports") and vulkan.get("old_host_cached_scope_preserved"),
            "Require the native-handle Vulkan system layout with real memory policy")
    require(not vulkan.get("synthetic_handle_adapter_included", True) and
            not vulkan.get("old_mapper_implementation_included", True), "Legacy Vulkan handle adapter included")
    require(vulkan.get("config_directory") == "/vendor/lib64/" and
            vulkan.get("hal_entry") == "vulkan.qti-policy.so", "Unexpected Vulkan HAL/config location")
    native_vulkan_files = {
        "lib64/hw/vulkan.freedreno.so": vulkan["hal_entry"],
        "lib64/vulkan.qti-native.so": "vulkan.qti-native.so",
        "lib64/libquest2-native-vulkan-memory.so": "libquest2-native-vulkan-memory.so",
    }
    for target, name in native_vulkan_files.items():
        data = read_verified(args.vulkan_build / name, vulkan["sha256"][name])
        elf(data, name)
        needed = subprocess.check_output(["patchelf", "--print-needed", str(args.vulkan_build / name)], text=True).splitlines()
        require("libqti-mapper3-bridge.so" not in needed, "Vulkan still depends on synthetic handle bridge")
        put(target, data)
    for name in MAPPER_LIBS:
        require(vulkan["sha256"][name] == builds["mapper"]["sha256"][name], "Vulkan/native graphics closure differs: " + name)
    require(vulkan["sha256"]["qgl_config.txt"] == builds["mapper"]["sha256"]["qgl_config.txt"], "Vulkan config differs")
    allocator = "quest2-allocator5-service"
    put("bin/hw/vendor.qti.hardware.display.allocator-service", read_verified(args.mapper_build / allocator,
        builds["mapper"]["sha256"][allocator]), 0o100755)
    companion=builds["mapper"].get("qti_mapper_extension_companion",{})
    require(companion.get("file")=="mapper.qti.so" and companion.get("framework_hal") is False and
            companion.get("backend")=="mapper.quest2.so" and companion.get("single_view_only"),
            "Require the real native mapper5 QTI extension companion")
    companion_data=read_verified(args.mapper_build/"mapper.qti.so",builds["mapper"]["sha256"]["mapper.qti.so"])
    elf(companion_data,"mapper.qti.so")
    put("lib64/mapper.qti.so",companion_data)
    compatibility_file=ROOT/"configs/quest2-qti-mapper-extension-reference.json"
    compatibility=json.loads(compatibility_file.read_text())["codec2_compatibility"]
    inputs[str(compatibility_file)]=file_hash(compatibility_file)
    symbols=subprocess.check_output(["readelf","--dyn-syms","--wide",str(args.mapper_build/"mapper.qti.so")],text=True)
    exported={fields[7] for line in symbols.splitlines() if len(fields:=line.split())>=8 and fields[6]!="UND"}
    require(set(compatibility["required_companion_exports"]).issubset(exported), "Missing real QTI companion exports")
    imported={fields[7] for line in symbols.splitlines() if len(fields:=line.split())>=8 and fields[6]=="UND"}
    companion_needed=subprocess.check_output(["patchelf","--print-needed",str(args.mapper_build/"mapper.qti.so")],text=True).splitlines()
    require("mapper.quest2.so" not in companion_needed and "AIMapper_loadIMapper" not in imported,
            "Companion would create a different default-namespace mapper registry")
    read_verified(args.codec2_library,compatibility["input_sha256"])
    codec2_data=patch_codec2_mapper_dependency(args.codec2_library,compatibility["input_sha256"])
    put("lib64/libqcodec2_utils.so",codec2_data)
    contracts = json.loads((args.android_contract / "FETCH.json").read_text())["sha256"]
    original_sums = {"lib64/libqcodec2_utils.so":compatibility["input_sha256"]}
    for target, source in REPLACEMENTS.items():
        contract_path = "vendor/" + target
        expected = contracts[contract_path]
        read_verified(args.android_contract / contract_path, expected)
        original_sums[target] = expected
        data = (ROOT / source).read_bytes()
        if target.endswith(".xml"):
            ET.fromstring(data)
        put(target, data, origin=ROOT / source)
    for target, source in CONTEXT_ADDITIONS.items():
        contract_path = "vendor/" + target
        expected = contracts[contract_path]
        current = read_verified(args.android_contract / contract_path, expected)
        addition = (ROOT / source).read_bytes()
        # No wildcard service mapping and no duplicate exact context expression.
        expressions = {line.split()[0] for line in current.splitlines() if line and not line.startswith(b"#")}
        for line in addition.splitlines():
            if line and not line.startswith(b"#"):
                require(line.split()[0] not in expressions, "Context already exists: " + target)
        inputs[str((ROOT / source).resolve())] = digest(addition)
        original_sums[target] = expected
        put(target, current.rstrip() + b"\n\n" + addition)
    linker = args.android_contract / "linkerconfig/ld.config.txt"
    read_verified(linker, contracts["linkerconfig/ld.config.txt"])
    require("namespace.default.isolated = true" in linker.read_text().split("[vendor]\n")[1].split("\n[")[0],
            "Unknown normal vendor linker contract")
    for name in OPTIONAL_EXTENSIONS:
        require(PRIVATE_LIB + name not in payload, "Unmatched optional C++ extension included")
    require(not any("vidc" in n or "omx" in n for n in payload), "Video service boundary changed")
    put("etc/init/sq2-qti-hwservicemanager.rc", manager + b"\n")
    args.output.mkdir(parents=True)
    tree = args.output / "payload"
    for name, (mode, data) in payload.items():
        target = tree / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        target.chmod(mode & 0o777)
    for name, target in links.items():
        path = tree / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.symlink_to(target)
    for name, (mode, data) in hidl_support.items():
        path = args.output / "hidl-support" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        path.chmod(mode & 0o777)
    original = "".join(expected + "  " + name + "\n" for name, expected in sorted(original_sums.items()))
    installed_sums = "".join(digest(data) + "  " + name + "\n" for name, (_, data) in sorted(payload.items()))
    sums = installed_sums.replace("  ", "  root/")
    sums += "".join(digest(data) + "  hidl-support/" + name + "\n" for name, (_, data) in sorted(hidl_support.items()))
    install = "".join(name + "\n" for name in sorted(payload))
    aliases = "".join(name + " " + target + "\n" for name, target in sorted(links.items()))
    helper = stage_script()
    helper_file = args.output / "qti-system-display-stage"
    helper_file.write_text(helper)
    helper_file.chmod(0o755)
    subprocess.run(["sh", "-n", str(helper_file)], check=True)
    cpio = load_module("pack-sq2-vulkan")
    delta = {"init": (before["init"][1][1], init.replace(HOOK, HOOK + NEW_HOOK)),
             "bin/qti-system-display-stage": (0o100755, helper.encode())}
    for directory in sorted({PREFIX, PREFIX + "/root", *(PREFIX + "/root/" + str(p.parent) for p in map(Path, payload) if str(p.parent) != ".")}):
        parts = Path(directory).parts
        for i in range(2, len(parts) + 1):
            delta.setdefault("/".join(parts[:i]), (0o40755, b""))
    for name, item in payload.items():
        delta[PREFIX + "/root/" + name] = item
    delta[PREFIX + "/hidl-support"] = (0o40755, b"")
    for name, item in hidl_support.items():
        delta[PREFIX + "/hidl-support/" + name] = item
    # Links are installed explicitly rather than unpacked on top of an existing HAL symlink.
    for name, data in {"SHA256SUMS": sums, "ORIGINAL_SHA256SUMS": original, "INSTALL_FILES": install,
                       "SYMLINKS": aliases, "INSTALLED_SHA256SUMS": installed_sums}.items():
        delta[PREFIX + "/" + name] = (0o100644, data.encode())
    archive = b"".join(cpio.entry(n, data, mode) for n, (mode, data) in delta.items()) + cpio.entry("TRAILER!!!", b"", 0)
    archive += b"\0" * (-len(archive) % 512)
    append = gzip.compress(archive, mtime=0) + credential_tail(args.base.read_bytes())
    (args.output / "display.append.gz").write_bytes(append)
    output = args.output / "initramfs.cpio.gz"
    with output.open("wb") as stream, args.base.open("rb") as baseline:
        shutil.copyfileobj(baseline, stream)
        stream.write(append)
    after, bodies = scan(output, {"init"})
    require(bodies["init"] == delta["init"][1], "Effective init hook mismatch")
    require(all(after.get(n) == entry for n, entry in before.items() if n != "init"), "Existing payload/metadata changed")
    for name, (mode, data) in delta.items():
        require(after[name][1][1] == mode and after[name][2] == digest(data), "Effective delta mismatch: " + name)
    require(file_hash(args.base) == args.expected_base_sha256, "Base changed during packing")
    changed = sorted(n for n in before.keys() | after.keys() if before.get(n) != after.get(n))
    require(set(changed) == set(delta), "Unexpected effective archive change")
    # Record exact ELF closure and expected search order, without claiming runtime relocation proof.
    dependencies = {}
    for name, (_, data) in payload.items():
        if data.startswith(b"\x7fELF"):
            dependencies[name] = subprocess.check_output(["patchelf", "--print-needed", str(tree / name)], text=True).splitlines()
            require(not set(dependencies[name]).intersection(LEGACY_GRAPHICS_ADAPTERS),
                    "Payload still needs a retired synthetic-handle adapter: " + name)
    report = {"schema": 1, "status": "normal-android-display-candidate-built-not-deployed", "private_artifact": True,
              "deployed": False, "reboot_verified": False, "deployment_ready": False,
              "base_sha256": args.expected_base_sha256, "output_sha256": file_hash(output),
              "append_sha256": digest(append), "inputs": inputs, "vendor_files": {n: digest(d) for n, (_, d) in payload.items()},
              "symlinks": links, "changed_effective_paths": changed, "original_non_init_entries_preserved": True,
              "kernel_firmware_credentials_unchanged": True, "efi_files_written": [], "fallback_untouched": True,
              "binder": "normal /dev/binder + /dev/hwbinder; no private managers or Power HAL instance",
              "normal_services": ["hwservicemanager", "vendor.hwcomposer", "vendor.graphics.allocator"],
              "native_vulkan_memory_policy": True,
              "qti_mapper_extension_companion": {"path":"lib64/mapper.qti.so","framework_hal":False,
                  "backend":"lib64/hw/mapper.quest2.so","single_view_only":True,
                  "loader":"android_load_sphal_library by SONAME; no direct mapper DT_NEEDED",
                  "metadata_state":"shared explicit-set bits for matched types; newer Snap types unsupported"},
              "codec2_mapper_dependency_fix": {"path":"lib64/libqcodec2_utils.so",
                  "input":str(args.codec2_library.resolve()),
                  "original_sha256":compatibility["input_sha256"],"patched_sha256":digest(codec2_data),
                  "removed_needed":"libmapperutils.so","executable_sections_unchanged":True,
                  "old_mapper_and_snap_backend_restored":False,"real_codec2_video_qualified":False},
              "native_vulkan_files": {target: vulkan["sha256"][name] for target, name in native_vulkan_files.items()},
              "native_vulkan_input_build": str(args.vulkan_build.resolve()),
              "hidl_manager_independent_of_video_flags": True,
              "hidl_support_sha256": {n: digest(d) for n, (_, d) in hidl_support.items()},
              "video_rc_manager_split_only_if_original_video_service_exists": True,
              "hal_versions": {"composer": "HIDL 2.4 + QTI 3.0", "allocator": "AIDL 2", "mapper": "native 5.0/quest2"},
              "service_library_path": "/vendor/lib64/sq2-qti-display:/system/lib64:/apex/com.android.i18n/lib64:/apex/com.android.tethering/lib64:/vendor/lib64",
              "elf_needed": dependencies, "linker_config_modified": False,
              "compiled_selinux_policy_modified": False, "selinux_contexts_modified": sorted(CONTEXT_ADDITIONS),
              "gles1_from_base": {"path": BASE_GLES1, "sha256": BASE_GLES1_SHA256,
                                  "installed_independent_of_old_qcom_selector": True},
              "removed_legacy_graphics_adapters": ["lib64/egl/" + n for n in LEGACY_GRAPHICS_ADAPTERS],
              "removed_legacy_mapper_implementations": ["lib64/hw/" + n for n in LEGACY_MAPPERS],
              "limits": ["Native Vulkan production paths and the normal Android desktop still require this package's real boot validation",
                         "Normal init linker resolution, SELinux domains and full Android desktop require runtime validation",
                         "Inherited development policy is not an enforcing-domain acceptance claim; getenforce alone does not prove domain enforcement",
                         "QService uses the existing vendor_qdisplay_service type on system Binder; service_manager add/find enforcement remains unverified",
                         "Inherited GLES1 binary has no legacy mapper dependency, but GLES1 rendering has not been tested with the native closure",
                         "Existing AVC-only software-output OMX boundary and persistent power/user settings are unchanged",
                         "No secure/HDR/overlay/performance/PM or cross-device acceptance"]}
    (args.output / "BUILD.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report[k] for k in ("status", "output_sha256", "deployment_ready", "normal_services", "hal_versions")}, indent=2))


if __name__ == "__main__":
    main()
