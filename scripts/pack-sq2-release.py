#!/usr/bin/env python3
"""Compact the accepted private initramfs and remove retired diagnostic payloads."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import posixpath
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("qti_pack", ROOT / "scripts/pack-sq2-qti-system-display.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)
REMOVED = {
    "bin/" + name for name in (
        "boot-kmsg collect-audio-crash collect-base collect-framework collect-ghost-touch "
        "collect-probe el2-kmsg-collect gpu-probe input-events iptsd-dump kvm-arm64-witness "
        "native-monitor run-tests save-probe sq2-audio-framework-test summarize-tests "
        "test-gpu test-input test-power test-speaker-readiness test-sq2-pm-stage test-wifi "
        "trial-touch-threshold uinput-fixture chrome-graphics-cache lab-keep-awake"
    ).split()
} | {
    "etc/sq2-boot-log.enabled", "etc/sq2-video-native/quest2-kmsg-collector",
    "etc/sq2-video-native/run-eos.sh", "etc/sq2-video-native/v4l2-decode-test",
    "etc/sq2-video-native/v4l2-query",
}


def validate_native_pair(service_data, bridge, omx=None):
    spec = importlib.util.spec_from_file_location("native_pack", ROOT / "scripts/pack-sq2-native-omx.py")
    native = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(native)
    data = native.read_elf(bridge, native.BRIDGE_EXPORTS)
    if omx is not None:
        native.read_elf(omx, {"get_omx_component_factory_fn"})
    with tempfile.TemporaryDirectory() as temp:
        service = Path(temp) / "quest2-omx-service"
        service.write_bytes(service_data)
        native.read_elf(service, native.service_layout_exports(data))
    return data


def clean_init(body):
    text = body.decode()
    start = text.index("logdir=/media/esp/logs/")
    end = text.index("# Normal-boot", start)
    # Preserve the existing passwordless-user startup; it is not a diagnostic.
    login = text[start:end]
    marker = 'if grep -qw "sq2.el2=1" /proc/cmdline; then'
    login = login[login.index(marker):] if marker in login else ""
    text = text[:start] + login + text[end:]
    text = text.replace('if [ "$debug" = 1 ]; then native-monitor > /logs/monitor.txt 2>&1 & fi\n', '')
    subprocess.run(["sh", "-n"], input=text.encode(), check=True)
    return text.encode()


def encode(name, body, record):
    magic, original, _ = record
    fields = list(original)
    raw_name = name.encode() + b"\0"
    fields[6], fields[11] = len(body), len(raw_name)
    fields[12] = sum(body) & 0xffffffff if magic == "070702" else 0
    header = magic.encode() + b"".join(f"{v:08x}".encode() for v in fields) + raw_name
    return header + b"\0" * (-len(header) % 4) + body + b"\0" * (-len(body) % 4)


def build(base, output, bridge, modules, expected, omx=None):
    if output.exists() or base.resolve() == output.resolve():
        raise ValueError("Use a fresh output file")
    if pack.file_hash(base) != expected:
        raise ValueError("Accepted parent hash mismatch")
    records, _ = pack.scan(base)
    _, bodies = pack.scan(base, records)
    for name, (_, fields, _) in records.items():
        if fields[4] > 1 and fields[1] & 0o170000 == 0o100000:
            raise ValueError("Unresolved hardlink: " + name)
    original = dict(bodies)
    for name in REMOVED:
        bodies.pop(name, None)
    bodies["init"] = clean_init(bodies["init"])
    terminal = bodies["bin/el2-terminal-stage"]
    witness = b'case " $(cat /proc/cmdline) " in\n *\' sq2.el2=experimental \'*|*\' sq2.debug=1 \'*) /bin/kvm-arm64-witness;;\nesac\n'
    if terminal.count(witness) != 1:
        raise ValueError("Unknown EL2 terminal stage")
    bodies["bin/el2-terminal-stage"] = terminal.replace(witness, b"")
    replacements = {"etc/sq2-hidl/lib64/libquest2-vidc-bridge.so":
                    validate_native_pair(bodies["etc/sq2-hidl/quest2-omx-service"], bridge, omx)}
    if omx is not None:
        replacements["etc/sq2-hidl/lib64/libOmxVdec.so"] = omx.read_bytes()
    for name in ("venus-core.ko", "venus-dec.ko", "venus-enc.ko"):
        matches = [path for path in bodies if Path(path).name == name]
        if not matches:
            raise ValueError("Missing paired module: " + name)
        for path in matches:
            replacements[path] = (modules / name).read_bytes()
    for name, data in replacements.items():
        if data[:6] != b"\x7fELF\x02\x01" or int.from_bytes(data[18:20], "little") != 183:
            raise ValueError("Expected ARM64 ELF: " + name)
        if name not in bodies:
            raise ValueError("Unknown runtime entry: " + name)
        bodies[name] = data
    for name, body in list(bodies.items()):
        if not (name.endswith("SHA256SUMS") or name.endswith(".sha256")) or name.endswith("ORIGINAL_SHA256SUMS"):
            continue
        lines = []
        for line in body.decode().splitlines():
            parts = line.split(None, 1)
            if len(parts) == 2:
                target = posixpath.normpath(posixpath.join(posixpath.dirname(name), parts[1].lstrip("*"))).lstrip("/")
                if name == "etc/sq2-qti-system/INSTALLED_SHA256SUMS":
                    target = "etc/sq2-qti-system/root/" + parts[1].lstrip("*/")
                if target in REMOVED:
                    continue
                if target in replacements:
                    line = pack.digest(bodies[target]) + "  " + parts[1].lstrip("*")
            lines.append(line)
        bodies[name] = ("\n".join(lines) + "\n").encode()
    trailer = ("070701", [0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 11, 0], "")
    raw = b"".join(encode(n, body, records[n]) for n, body in bodies.items())
    raw += encode("TRAILER!!!", b"", trailer)
    raw += b"\0" * (-len(raw) % 512)
    # The private identity member stays byte-exact and last.
    tail = pack.credential_tail(base.read_bytes())
    if not tail:
        raise ValueError("Missing original private identity member")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(gzip.compress(raw, mtime=0) + tail)
    after, result = pack.scan(output, bodies)
    if result != bodies or set(after) != set(bodies):
        raise ValueError("Effective package mismatch")
    protected = [n for n in bodies if n.startswith(("etc/dropbear/", "lib/firmware/"))]
    protected += ["etc/shadow", "etc/sq2-audio/audio-hal"]
    for name in protected:
        if bodies[name] != original[name]:
            raise ValueError("Protected runtime/identity changed: " + name)
    report = {"parent_sha256": expected, "sha256": pack.file_hash(output),
              "removed": sorted(set(original) - set(bodies)),
              "changed": {n: pack.digest(v) for n, v in bodies.items() if v != original[n]},
              "unchanged_entries": sum(v == original[n] for n, v in bodies.items()),
              "private_identity_present": True, "device_written": False}
    output.with_suffix(".audit.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("base", "output", "bridge", "modules"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--expected-base-sha256", required=True)
    p.add_argument("--omx", type=Path, help="Explicit separately validated OMX replacement")
    a = p.parse_args()
    print(json.dumps(build(a.base, a.output, a.bridge, a.modules, a.expected_base_sha256, a.omx), indent=2))


if __name__ == "__main__":
    main()
