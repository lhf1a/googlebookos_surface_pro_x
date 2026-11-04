#!/usr/bin/env python3
"""Convert an unsigned experimental EFI launcher to debug-stage-only admission."""
import argparse
from pathlib import Path
import struct


def prepare(raw):
    if len(raw) < 64 or raw[:2] != b'MZ':
        raise ValueError('not a PE image')
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    opt = pe + 24
    if opt + 152 > len(raw) or raw[pe:pe+4] != b'PE\0\0':
        raise ValueError('invalid PE header')
    if struct.unpack_from('<H', raw, pe+4)[0] != 0xaa64 or struct.unpack_from('<H', raw, opt)[0] != 0x20b:
        raise ValueError('ARM64 PE32+ required')
    if struct.unpack_from('<II', raw, opt+144) != (0, 0) or struct.unpack_from('<I', raw, opt+64)[0]:
        raise ValueError('signed/checksummed launcher must be rebuilt from source')
    # Existing GNU EFI launchers store a NUL-terminated UTF-16 LoadOptions array.
    begin = 'console=tty0 '.encode('utf-16le')
    if raw.count(begin) != 1:
        raise ValueError('one known LoadOptions array required')
    start = raw.index(begin)
    end = start
    while end + 2 <= len(raw) and raw[end:end+2] != b'\0\0':
        end += 2
    if end + 2 > len(raw):
        raise ValueError('unterminated LoadOptions')
    options = raw[start:end].decode('utf-16le')
    tokens = options.split()
    required = ('suspend.screen_only=1', 'nohibernate', 'suspend.screen_only_autosuspend=1')
    if any(tokens.count(t) != 1 for t in required):
        raise ValueError('expected protected experimental command line')
    allowed = {'suspend.screen_only=1', 'suspend.screen_only_autosuspend=1'}
    if any(t.startswith('suspend.') and t not in allowed for t in tokens):
        raise ValueError('unexpected suspend option')
    old = 'suspend.screen_only_autosuspend=1'
    new = 'suspend.screen_only_test=1'
    updated = options.replace(old, new + ' ' * (len(old)-len(new)))
    result = raw[:start] + updated.encode('utf-16le') + raw[end:]
    assert len(result) == len(raw)
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('launcher', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    candidate = prepare(a.launcher.read_bytes())
    with a.output.open('xb') as f:
        f.write(candidate)
