#!/usr/bin/env python3
"""Add the Terminal boot hook to an existing lab EL2 image; build only.

Keeps the effective EFI/recovery/video integration of the selected base.
Credential-bearing inputs and outputs must stay private.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cpio', ROOT / 'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpio)


def build(base, expected_sha256, user=None):
    sha = lambda data: hashlib.sha256(data).hexdigest()
    if sha(base) != expected_sha256:
        raise ValueError('Baseline hash mismatch')
    before = cpio.entries(gzip.decompress(base))
    mode, init = before['init']
    anchor = b' el2-kmsg-collect /logs/kernel-live.txt &\n'
    if user is not None and (not user.isascii() or not user.isdecimal()):
        raise ValueError('Expected numeric Android user ID')
    argument = '' if user is None else ' ' + user
    hook = (' el2-terminal-autostart' + argument + ' > /logs/el2-terminal-autostart.txt 2>&1 &\n').encode()
    if init.count(anchor) != 1 or b'el2-terminal-autostart' in init:
        raise ValueError('Expected existing EL2 experiment block without Terminal hook')
    helper = (ROOT / 'probe/virtualization/el2-terminal-autostart').read_bytes().replace(b'\r\n', b'\n')
    delta = {'init': (mode, init.replace(anchor, anchor + hook)),
             'bin/el2-terminal-autostart': (0o100755, helper)}
    if before['bin'][0] & 0o170000 != 0o040000:
        raise ValueError('Missing bin directory')
    for _, body in delta.values():
        subprocess.run(['sh', '-n'], input=body, check=True)
    rest = base
    while rest:
        decoder = zlib.decompressobj(31)
        payload = decoder.decompress(rest)
        if not decoder.eof:
            raise ValueError('Truncated gzip member')
        last = rest[:len(rest) - len(decoder.unused_data)]
        rest = decoder.unused_data
    credentials = cpio.entries(payload)
    if not {'etc/shadow', 'bin/dropbear'} <= credentials.keys() or credentials.keys() & delta.keys():
        raise ValueError('Expected final local SSH credential member')
    archive = b''.join(cpio.entry(n, body, mode) for n, (mode, body) in delta.items())
    archive += cpio.entry('TRAILER!!!', b'', 0)
    archive += b'\0' * (-len(archive) % 512)
    result = base + gzip.compress(archive, mtime=0) + last
    after = cpio.entries(gzip.decompress(result))
    changed = {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)}
    if changed != set(delta) or any(after[n] != value for n, value in delta.items()):
        raise ValueError('Unexpected effective delta')
    return result, dict(base_sha256=sha(base), output_sha256=sha(result),
                        changed=sorted(changed), credential_last_byte_identical=True,
                        passwordless_user=user, hardware_boot_tested=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--expected-base-sha256', required=True)
    parser.add_argument('--passwordless-user', help='explicit lab user; secure locks are refused at runtime')
    args = parser.parse_args()
    result, audit = build(args.base.read_bytes(), args.expected_base_sha256, args.passwordless_user)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('xb') as output:
        output.write(result)
    if args.output.read_bytes() != result:
        raise ValueError('Output readback differs')
    args.output.with_suffix('.audit.json').write_text(json.dumps(audit, indent=2) + '\n')
    print(json.dumps(audit, indent=2))


if __name__ == '__main__':
    main()
