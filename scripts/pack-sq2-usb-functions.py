#!/usr/bin/env python3
"""Build a USB-only addon for the pinned SQ2 runtime+caps; never deploy/reboot.

The boot flag sq2.usb=experimental enables the paired storage/audio stage.
Keep credential-bearing output in ignored local storage. Never restart vold:
this port's vold.rc requests a board reboot when that service exits abnormally.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import zlib

R = Path(__file__).resolve().parents[1]
BASE_SHA = '8b6c1f9a83aee85cf86aca4342147ee50bd43fb37e17d8f3a9750a759761484b'
CAPS_SHA = 'fb1831a9da0faf395cc72658e09f175afb3e99aa16f25ce371b13be7540e93ff'
HAL_SHA = '774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea'
sha = lambda b: hashlib.sha256(b).hexdigest()


def build(base, caps):
    if sha(base) != BASE_SHA or sha(caps) != CAPS_SHA:
        raise ValueError('Only the verified SQ2 runtime-v1 + caps inputs are accepted')
    spec = importlib.util.spec_from_file_location('cpio', R/'scripts/pack-sq2-vulkan.py')
    c = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(c)
    before = c.entries(gzip.decompress(base + caps))
    if sha(before['etc/sq2-audio/audio-hal'][1]) != HAL_SHA:
        raise ValueError('Microphone-corrected stock transport HAL required')
    init = before['init'][1]
    anchor = b'resource-monitor-start > /logs/resource-monitor.txt'
    if init.count(anchor) != 1 or b'usb-functions-stage >' in init:
        raise ValueError('Unexpected init hook context')
    source = lambda p: (R/p).read_bytes().replace(b'\r\n', b'\n')
    files = {
        'storage.fstab': source('configs/sq2-usb-storage.fstab'),
        'audio_policy_configuration.xml': source('configs/sq2-usb-audio-policy.xml'),
        'manifest.xml': source('configs/sq2-usb-audio-manifest.xml'),
    }
    files['SHA256SUMS'] = ''.join(sha(b)+'  '+n+'\n' for n, b in files.items()).encode()
    delta = {
        'init': (before['init'][0], init.replace(anchor,
            b'usb-functions-stage > /logs/usb-functions-stage.txt 2>&1 || echo SQ2_USB_STAGE_FAILED_SEE_RAM_LOG\n'+anchor)),
        'bin/usb-functions-stage': (0o100755, source('probe/test2/usb-functions-stage')),
        'etc/sq2-usb': (0o40755, b''),
    }
    delta.update({'etc/sq2-usb/'+n: (0o100644, b) for n, b in files.items()})
    rest, credentials = base + caps, None
    while rest:
        d = zlib.decompressobj(31)
        entries = c.entries(d.decompress(rest))
        if not d.eof:
            raise ValueError('Incomplete gzip member')
        member = rest[:len(rest)-len(d.unused_data)]
        if 'etc/shadow' in entries and 'bin/dropbear' in entries:
            credentials = member
        rest = d.unused_data
    if credentials is None:
        raise ValueError('Missing private SSH final member')
    raw = b''.join(c.entry(n, b, m) for n, (m, b) in delta.items())+c.entry('TRAILER!!!', b'', 0)
    raw += b'\0' * (-len(raw) % 512)
    result = gzip.compress(raw, mtime=0) + credentials
    after = c.entries(gzip.decompress(base + caps + result))
    changed = {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)}
    if changed != set(delta) or any(after[n] != before[n] for n in before.keys()-set(delta)):
        raise ValueError('Final-entry change whitelist failed')
    return result, dict(base_sha256=BASE_SHA, caps_sha256=CAPS_SHA,
                        addon_sha256=sha(result), bytes=len(result), changed=sorted(changed),
                        private_ssh_last=True, original_hal_preserved=True,
                        hardware_tested_by_this_build=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('base', type=Path)
    p.add_argument('caps', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    if a.output.exists() or a.output.with_suffix('.audit.json').exists():
        raise ValueError('Output/audit exists; choose a fresh destination')
    result, report = build(a.base.read_bytes(), a.caps.read_bytes())
    with a.output.open('xb') as f:
        f.write(result)
    if a.output.read_bytes() != result:
        raise OSError('Output readback mismatch')
    a.output.with_suffix('.audit.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
