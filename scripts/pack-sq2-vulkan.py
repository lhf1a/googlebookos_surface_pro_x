#!/usr/bin/env python3
"""Append a Vulkan-only delta to a hash-verified local initramfs.

Never deploys or changes a disk. Credential-bearing bases and outputs must stay
private. Last-entry audit rejects any change outside the explicit Vulkan delta.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import zlib


def entries(data):
    pos, result = 0, {}
    while pos < len(data):
        if data[pos:pos+6] not in (b'070701', b'070702'):
            assert data[pos] == 0, f'Invalid cpio header at {pos}'
            pos += 1
            continue
        fields = [int(data[pos+6+i*8:pos+14+i*8], 16) for i in range(13)]
        size, length = fields[6], fields[11]
        name = data[pos+110:pos+110+length-1].decode().removeprefix('./')
        begin = (pos+110+length+3) & ~3
        body = data[begin:begin+size]
        assert len(body) == size
        pos = (begin+size+3) & ~3
        if name != 'TRAILER!!!':
            result[name] = (fields[1], body)
    return result


def entry(name, body, mode):
    name = name.encode()+b'\0'
    fields = [1, mode, 0, 0, 1, 0, len(body), 0, 0, 0, 0, len(name), 0]
    header = b'070701'+b''.join(f'{n:08x}'.encode() for n in fields)+name
    return header+b'\0'*(-len(header)%4)+body+b'\0'*(-len(body)%4)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('base', type=Path)
    p.add_argument('driver', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--expected-base-sha256', required=True)
    p.add_argument('--preserve-lab-ssh', action='store_true')
    a = p.parse_args()
    assert a.base.resolve() != a.output.resolve(), 'Never overwrite the baseline'
    assert not a.output.exists(), 'Choose a new output path'
    base = a.base.read_bytes()
    digest = lambda b: hashlib.sha256(b).hexdigest()
    assert digest(base) == a.expected_base_sha256, 'Baseline hash mismatch'
    before = entries(gzip.decompress(base))
    prop_name = 'etc/googlebook-security/build.prop'
    mode, prop = before[prop_name]
    assert prop.count(b'ro.hardware.vulkan=none\n') == 1
    assert b'ro.hardware.egl=freedreno\n' in prop
    assert b'debug.hwui.renderer=skiagl\n' in prop
    driver = a.driver.read_bytes()
    assert driver[:6] == b'\x7fELF\x02\x01' and int.from_bytes(driver[18:20], 'little') == 183
    perms = 'etc/googlebook-security/etc/permissions/'
    delta = {
        prop_name: (mode, prop.replace(b'ro.hardware.vulkan=none\n', b'ro.hardware.vulkan=freedreno\n')),
        'etc/googlebook-security/lib64/hw/vulkan.freedreno.so': (0o100644, driver),
        # Override the donor's 1.4 claim and certification date: neither applies
        # to this physical GPU/driver port. Other inherited features are unchanged.
        perms+'android.hardware.vulkan.version-1_4.xml': (0o100644, b'<permissions/>\n'),
        perms+'android.software.vulkan.deqp.level.xml': (0o100644, b'<permissions/>\n'),
        perms+'android.hardware.vulkan.version-1_3.xml': (0o100644, b'<permissions><feature name="android.hardware.vulkan.version" version="4206592"/></permissions>\n'),
    }
    # The kernel's initramfs unpacker does not create missing parent directories.
    # A file-only last-entry audit can pass while extraction silently loses files.
    parents = {}
    for name in delta:
        parent = Path(name).parent
        while str(parent) != '.':
            key = parent.as_posix()
            if key not in before and key not in delta:
                parents[key] = (0o40755, b'')
            elif key in before:
                assert before[key][0] & 0o170000 == 0o040000, f'Non-directory parent: {key}'
            parent = parent.parent
    delta = dict(sorted(parents.items(), key=lambda item: item[0].count('/'))) | delta
    rest, credential_member = base, None
    while rest:
        dec = zlib.decompressobj(31)
        payload = dec.decompress(rest)
        assert dec.eof
        consumed = len(rest)-len(dec.unused_data)
        files = entries(payload)
        if 'etc/shadow' in files and 'bin/dropbear' in files:
            credential_member = rest[:consumed]
        rest = dec.unused_data
    if credential_member:
        assert a.preserve_lab_ssh, 'Credential-bearing base requires --preserve-lab-ssh'
    elif a.preserve_lab_ssh:
        raise ValueError('No complete local password SSH addon found')
    archive = b''.join(entry(n, body, mode) for n, (mode, body) in delta.items())
    archive += entry('TRAILER!!!', b'', 0)
    archive += b'\0'*(-len(archive)%512)
    output = base+gzip.compress(archive, mtime=0)+(credential_member or b'')
    after = entries(gzip.decompress(output))
    changed = {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)}
    assert changed == set(delta), changed
    for name in ('bin/iptsd', 'bin/audio-framework-boot', 'etc/sq2-audio/audio-hal', 'init', 'etc/shadow', 'bin/dropbear'):
        assert before.get(name) == after.get(name), f'Unexpected change: {name}'
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('xb') as f:
        f.write(output)
    report = {'base_sha256': digest(base), 'output_sha256': digest(output),
              'driver_sha256': digest(driver), 'changed': sorted(changed),
              'password_addon_last': bool(credential_member), 'reboot_validated': False}
    a.output.with_suffix('.audit.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
