#!/usr/bin/env python3
"""Append the native OMX service/bridge to a verified QTI base; never deploy it."""
import argparse
import gzip
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("qti_system_pack", ROOT / "scripts/pack-sq2-qti-system-display.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)
PREFIX = "etc/sq2-hidl/"
SERVICE = PREFIX + "quest2-omx-service"
BRIDGE = PREFIX + "lib64/libquest2-vidc-bridge.so"
SUMS = PREFIX + "SHA256SUMS"
SERVICE_EXPORTS = {"quest2_omx_capture_layout", "quest2_omx_capture_layout_for_usage", "quest2_omx_validate_native_buffer"}
LAYOUT_V2 = "quest2_omx_capture_layout_for_usage_v2"
BRIDGE_EXPORTS = {"quest2_vidc_node_begin", "quest2_vidc_node_end", "quest2_vidc_node_configure", "quest2_vidc_node_get_layout"}


def read_elf(path, required):
    data = path.read_bytes()
    pack.require(len(data) >= 64 and data[:7] == b"\x7fELF\x02\x01\x01" and
                 int.from_bytes(data[18:20], "little") == 183, "Expected ARM64 ELF: " + str(path))
    symbols = subprocess.check_output(["readelf", "--dyn-syms", "--wide", str(path)], text=True)
    exports = set()
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[3:6] == ["FUNC", "GLOBAL", "DEFAULT"] and fields[6] != "UND":
            exports.add(fields[7].split("@")[0])
    pack.require(required <= exports, "Missing defined native API exports: " + str(sorted(required - exports)))
    pack.require(path.read_bytes() == data, "ELF changed during inspection")
    return data


def service_layout_exports(bridge_data):
    # A v2-only bridge cannot use the original NV12 service's seven-argument ABI.
    legacy = b"quest2_omx_capture_layout_for_usage\0" in bridge_data
    needs_v2 = LAYOUT_V2.encode() + b"\0" in bridge_data and not legacy
    return SERVICE_EXPORTS | ({LAYOUT_V2} if needs_v2 else set())


def checked_sums(data, records):
    entries = {}
    for line in data.decode().splitlines(keepends=True):
        match = re.fullmatch(r"([0-9a-f]{64})  ([^\r\n]+)\n", line)
        pack.require(match is not None, "Invalid HIDL SHA256SUMS line")
        expected, relative = match.groups()
        pack.require(not relative.startswith("/") and all(p not in ("", ".", "..") for p in relative.split("/")),
                     "Unsafe HIDL checksum path")
        pack.require(relative not in entries, "Duplicate HIDL checksum path")
        name = PREFIX + relative
        pack.require(name in records and records[name][1][1] & 0o170000 == 0o100000,
                     "Missing regular HIDL input: " + relative)
        pack.require(records[name][2] == expected, "HIDL checksum mismatch: " + relative)
        entries[relative] = line
    pack.require({SERVICE.removeprefix(PREFIX), BRIDGE.removeprefix(PREFIX)} <= entries.keys(),
                 "Native OMX payload absent from HIDL checksums")
    return entries


def build(base, service, bridge, output, expected_base):
    pack.require(not output.exists(), "Choose a fresh private output directory")
    pack.require(pack.file_hash(base) == expected_base, "Base artifact hash differs")
    before, selected = pack.scan(base, {"init", SUMS})
    init = selected.get("init", b"")
    pack.require(init.count(pack.HOOK) == 1 and init.count(pack.NEW_HOOK) == 1 and
                 init.index(pack.HOOK) < init.index(pack.NEW_HOOK), "Requires HIDL stage before QTI display stage")
    checksums = checked_sums(selected[SUMS], before)
    bridge_data = read_elf(bridge, BRIDGE_EXPORTS)
    service_exports = service_layout_exports(bridge_data)
    delta = {SERVICE: read_elf(service, service_exports), BRIDGE: bridge_data}
    sums = []
    for relative, line in checksums.items():
        name = PREFIX + relative
        sums.append(pack.digest(delta[name]) + line[64:] if name in delta else line)
    delta[SUMS] = "".join(sums).encode()
    pack.require(all(before[n][2] != pack.digest(data) for n, data in delta.items()),
                 "Expected new service, bridge and checksum list")
    cpio = pack.load_module("pack-sq2-vulkan")
    archive = b""
    for name, data in delta.items():
        fields = list(before[name][1])
        pack.require(fields[1] & 0o170000 == 0o100000 and fields[4] == 1, "Expected regular unlinked payload")
        # Preserve mode, ownership, inode and timestamps; only size/name length/checksum change.
        fields[6], fields[11], fields[12] = len(data), len(name.encode()) + 1, 0
        entry = cpio.entry(name, data, fields[1])
        archive += b"070701" + b"".join(f"{value:08x}".encode() for value in fields) + entry[110:]
    archive += cpio.entry("TRAILER!!!", b"", 0)
    archive += b"\0" * (-len(archive) % 512)
    credential = pack.credential_tail(base.read_bytes())
    append = gzip.compress(archive, mtime=0) + credential
    output.mkdir(parents=True)
    image = output / "initramfs.cpio.gz"
    with image.open("xb") as stream, base.open("rb") as baseline:
        shutil.copyfileobj(baseline, stream)
        stream.write(append)
    after, actual = pack.scan(image, set(delta))
    changed = {name for name in before.keys() | after.keys() if before.get(name) != after.get(name)}
    pack.require(changed == set(delta), "Unexpected effective archive changes: " + str(sorted(changed)))
    pack.require(all(actual[name] == data for name, data in delta.items()), "Effective OMX payload differs")
    pack.require(all(after.get(name) == record for name, record in before.items() if name not in delta),
                 "Unrelated bytes or metadata changed")
    checked_sums(actual[SUMS], after)
    pack.require(pack.file_hash(base) == expected_base, "Base changed during packing")
    pack.require(pack.file_hash(service) == pack.digest(delta[SERVICE]) and pack.file_hash(bridge) == pack.digest(delta[BRIDGE]),
                 "Native OMX input changed during packing")
    pack.require(pack.credential_tail(image.read_bytes()) == credential, "Private credential tail changed")
    (output / "native-omx.append.gz").write_bytes(append)
    inputs = {str(path.resolve()): pack.file_hash(path) for path in
              (Path(__file__), ROOT / "scripts/pack-sq2-qti-system-display.py", ROOT / "scripts/pack-sq2-vulkan.py", service, bridge)}
    report = {"schema": 1, "status": "native-omx-candidate-built-pending-normal-boot", "private_artifact": True,
              "base_sha256": expected_base, "output_sha256": pack.file_hash(image), "output_bytes": image.stat().st_size,
              "append_sha256": pack.digest(append), "inputs": inputs,
              "payload_sha256": {name: pack.digest(data) for name, data in delta.items()},
              "required_exports": {SERVICE: sorted(service_exports), BRIDGE: sorted(BRIDGE_EXPORTS)},
              "changed_effective_paths": sorted(changed), "verified_hidl_checksum_entries": len(checksums),
              "original_other_bytes_and_metadata_preserved": True, "credential_tail_preserved_last": bool(credential),
              "init_rc_environment_and_video_flags_unchanged": True, "inherited_display_mapper_closure_unchanged": True,
              "kernel_firmware_credentials_unchanged": True, "efi_files_written": [],
              "deployed": False, "normal_boot_pending": True, "reboot_verified": False, "deployment_ready": False,
              "full_hwc_verified": False, "full_stack_complete": False,
              "limits": ["Normal boot and default-path native OMX require this package's device acceptance",
                         "Inherited HIDL video selector and QTI stage ordering are unchanged",
                         "No secure, HDR, video UBWC, performance, PM or cross-device acceptance"]}
    (output / "BUILD.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("base", "service", "bridge", "output"):
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--expected-base-sha256", required=True)
    args = parser.parse_args()
    report = build(args.base, args.service, args.bridge, args.output, args.expected_base_sha256)
    print(json.dumps({k: report[k] for k in ("status", "output_sha256", "changed_effective_paths", "normal_boot_pending")}, indent=2))


if __name__ == "__main__":
    main()
