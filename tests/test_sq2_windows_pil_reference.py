#!/usr/bin/env python3
"""Run against the restored authorized reference; reject altered evidence."""
import importlib.util
from pathlib import Path
import struct
import sys
from unittest.mock import patch

script = Path(__file__).resolve().parents[1] / 'scripts/inspect-sq2-windows-pil-reference.py'
spec = importlib.util.spec_from_file_location('pil_reference', script)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
root = Path(sys.argv[1])
pil = (root / 'drivers/oem109.inf/QcSkExt8180.exe').read_bytes()
csrt = next((root / 'acpi').glob('CSRT-*.dat')).read_bytes()


def rejected(fn, expected):
    try:
        fn()
    except ValueError as error:
        assert expected in str(error), str(error)
    else:
        raise AssertionError('Altered evidence accepted: ' + expected)


def edit_table(offset, value):
    data = bytearray(csrt)
    data[offset:offset + len(value)] = value
    data[9] = (data[9] - sum(data)) % 256
    return bytes(data)


result = mod.inspect(pil, csrt)
assert result['hardware_access'] is False
assert len(result['instruction_checks']) == 11
assert len(result['streams']) == 11
assert len(result['startup_hwio']) == 21
rejected(lambda: mod.inspect(pil[:-1], csrt), 'extension hash')
rejected(lambda: mod.inspect(pil, csrt[:-1]), 'CSRT hash')
rejected(lambda: mod.csrt_layout(csrt[:-1]), 'checksum')
rejected(lambda: mod.csrt_layout(edit_table(36, struct.pack('<I', 0))), 'record length')
rejected(lambda: mod.csrt_layout(edit_table(0x5c + 0x58 + 84, struct.pack('<I', 9))),
         'packed stream table')
rejected(lambda: mod.csrt_layout(edit_table(0x5c + 0xaf4, b'\x02\x23')),
         'firmware stream candidate')
rejected(lambda: mod.csrt_layout(edit_table(0x5c + 0x274d + 37, b'\x0d')),
         'Ownership rules')
changed = bytearray(pil)
changed[0x97a8 - 0xc00] ^= 1
# Only the negative test bypasses the input hash to reach the instruction check.
with patch.object(mod, 'PIL_SHA', mod.hashlib.sha256(changed).hexdigest()):
    rejected(lambda: mod.inspect(changed, csrt), 'video_destination12')
changed = bytearray(pil)
changed[0x1a8a0 + 99 * 32 + 20] ^= 4
with patch.object(mod, 'PIL_SHA', mod.hashlib.sha256(changed).hexdigest()):
    rejected(lambda: mod.inspect(changed, csrt), 'HWIO entry changed')
print('PASS pinned input, 21 HWIO entries and 9 altered-evidence rejection cases; no hardware access')
