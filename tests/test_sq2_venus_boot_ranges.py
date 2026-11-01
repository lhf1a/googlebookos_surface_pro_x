#!/usr/bin/env python3
"""Check the pinned firmware's early ARM MMU construction against no-TZ ranges."""
from pathlib import Path
import hashlib
import struct
import sys

fw = Path(sys.argv[1]).read_bytes()
assert hashlib.sha256(fw).hexdigest() == '4e93d34504da96320775476bbd509aa06f985b48fb781d4436c8eb812b929928'
code = fw[0x4000:]
# r4=CPA_END, r5=NONPIX_END, r6=NONPIX_START, r7=FW_END, in MiB.
assert struct.unpack_from('<4I', code, 0x118) == (0xe00e102c, 0xe00e1030, 0xe00e1034, 0xe00e1024)
assert struct.unpack_from('<12I', code, 0x128) == (
    0xe51f100c,0xe5914000,0xe1a04a24,0xe51f101c,0xe5915000,0xe1a05a25,
    0xe51f102c,0xe5916000,0xe1a06a26,0xe51f103c,0xe5917000,0xe1a07a27)
# Decode the actual threshold comparisons and conditional descriptor choices.
words = struct.unpack_from('<15I', code, 0x180)
assert words == (0x4a00000c,0xe1510004,0x5afffff7,0xe1510005,0x51a02009,
                 0x5afffff4,0xe1510006,0x51a02008,0x5afffff1,0xe1510007,
                 0x51a02009,0x5affffee,0xe1a02008,0xeaffffec,0xe3c33b03)
assert struct.unpack_from('<3I', code, 0x15c) == (0xe3a02012,0xe3828ede,0xe3c29003)

def table(cpa_end, nonpixel_end, nonpixel_start, firmware_end):
    r = dict(zip(range(4,8), (cpa_end, nonpixel_end, nonpixel_start, firmware_end)))
    r[8], r[9], descriptor = 0xdf2, 0x10, 0xdf2
    entries = {}
    for section in range(4095, -1, -1):
        entries[section] = (section << 20) | descriptor
        next_section = section - 1
        if next_section < 0:
            break
        # This follows the pinned ARM compare/movpl/bpl chain, including
        # the retained descriptor above CPA_END.
        if next_section >= r[4]:
            continue
        if next_section >= r[5]:
            descriptor = r[9]
        elif next_section >= r[6]:
            descriptor = r[8]
        elif next_section >= r[7]:
            descriptor = r[9]
        else:
            descriptor = r[8]
    entries[0] &= ~0xc00
    return entries

intermediate = table(5, 0, 0, 5)
linux_no_tz = table(5, 5, 5, 5)
normal_protection = table(600, 600, 16, 5)
assert all(intermediate[i] & 3 == 0 for i in range(5))
assert all(linux_no_tz[i] & 3 == 2 for i in range(5))
assert all(normal_protection[i] & 3 == 2 for i in range(5))
assert linux_no_tz[0xbf0] & 3 == 2
print('BOOT_RANGES_PASS: Windows intermediate faults all five firmware sections; Linux no-TZ and successful PAS protection keep them valid')
