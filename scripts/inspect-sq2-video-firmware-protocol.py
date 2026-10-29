#!/usr/bin/env python3
"""Inspect pinned SQ2 firmware protocol instructions without executing it.

This records bounded static evidence, never a claim of successful VPU DMA.
Firmware and raw disassembly stay outside Git. No device/network access.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = '4e93d34504da96320775476bbd509aa06f985b48fb781d4436c8eb812b929928'


def inspect(path):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise ValueError('Only the pinned original SQ2 qcvss8180 firmware is accepted')
    if data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Expected ELF32 little-endian version 1')
    hdr = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    if (hdr[2], hdr[5], hdr[9], hdr[10]) != (40, 52, 32, 5):
        raise ValueError('Unexpected ARM program header layout')
    segments = [struct.unpack_from('<8I', data, hdr[5] + i * hdr[9])
                for i in range(hdr[10])]

    def read(va, size):
        for typ, off, addr, _phys, filesz, _memsz, _flags, _align in segments:
            if typ == 1 and addr <= va and va + size <= addr + filesz:
                return data[off + va - addr:off + va - addr + size]
        raise ValueError('Unmapped file-backed firmware VA: ' + hex(va))

    def word(va):
        return struct.unpack('<I', read(va, 4))[0]

    def checked_words(va, expected):
        actual = [word(va + 4 * i) for i in range(len(expected))]
        if actual != expected:
            raise ValueError('Instruction mismatch at ' + hex(va))
        return {'virtual_address': hex(va),
                'words': [f'{value:08x}' for value in actual]}

    def branch(va, target):
        op = word(va)
        if op & 0xff000000 != 0xeb000000:
            raise ValueError('Expected ARM BL at ' + hex(va))
        delta = op & 0xffffff
        if delta & 0x800000:
            delta -= 0x1000000
        if va + 8 + delta * 4 != target:
            raise ValueError('Unexpected ARM BL target')
        return {'from': hex(va), 'to': hex(target), 'word': f'{op:08x}'}

    # Named by the firmware's own error-path function-name literal.
    assert read(0xead0f, len(b'HFI_QueueGetQHdr')) == b'HFI_QueueGetQHdr'
    assert word(0x2f57c) == 0xead0f
    findings = {
        'queue_header_offset_is_loaded_from_table': {
            'interpretation': 'LDR r5,[r7,#8]; ADD r0,r7,r5. Queue header starts at the advertised qhdr0_offset, not a fixed 24 bytes.',
            'evidence': [checked_words(0x2f490, [0xe5975008]),
                         checked_words(0x2f4b0, [0xe0870005])],
            'limit': 'Does not prove that any actual device mapping was read successfully.'
        },
        'queue_header_stride_is_56': {
            'interpretation': 'Queue search advances by 0x38 bytes; direct index path computes index*7*8.',
            'evidence': [checked_words(0x2f4c8, [0xe2800038]),
                         checked_words(0x2f434, [0xe5901008, 0xe0800001,
                                               0xe0641184, 0xe0800181])]
        },
        'CPU_and_DSP_queue_table_mailboxes': {
            'interpretation': 'CPU getter reads firmware-mapped E00D2054; DSP getter reads E00D2034.',
            'evidence': [checked_words(0x25760, [0xe59f0004, 0xe5900000,
                                               0xe12fff1e, 0xe00d2054]),
                         checked_words(0x25770, [0xe59f0004, 0xe5900000,
                                               0xe12fff1e, 0xe00d2034])]
        },
        'H2A_interrupt_bit15': {
            'interpretation': 'Firmware ORs 0x8000 into E00DF010 and its handler writes 0x8000 to E00DF01C.',
            'evidence': [checked_words(0xd630, [0xe5901000, 0xe3811902, 0xe5801000]),
                         checked_words(0xd650, [0xe00df010]),
                         checked_words(0x2b144, [0xe3a01902, 0xe5801000]),
                         checked_words(0x2b16c, [0xe00df01c])],
            'limit': 'Confirms the firmware-side bit convention; does not prove host notification reached this handler.'
        },
        'empty_command_queue_writes_receive_request': {
            'interpretation': 'After equal read/write indices, the empty-queue path writes 1 to queue-header offset 0x20 (rx_req). A host-initialized zero is an observable queue-write marker.',
            'evidence': [checked_words(0x2f08c, [0xe5957034, 0xe1520007,
                                               0x1a000005, 0xe3a00001,
                                               0xe5850020])],
            'limit': 'A possible observation point, not an observation already obtained on the device.'
        },
        'control_ready_is_not_queue_DMA_proof': {
            'interpretation': 'One initialization path calls the control-status bit0 setter before waiting for QTBL_INFO and looking up queue headers.',
            'evidence': [checked_words(0x2571c, [0xe59f000c, 0xe5901000,
                                               0xe3811001, 0xe5801000,
                                               0xe12fff1e, 0xe00d204c]),
                         checked_words(0x25700, [0xe59f0010, 0xe5900000,
                                               0xe20000ff, 0xe3500001]),
                         checked_words(0x25718, [0xe00d2050])],
            'calls_in_that_path': [branch(0x29b04, 0x2571c),
                                  branch(0x29bf4, 0x25700),
                                  branch(0x29c1c, 0x2f454)],
            'limit': 'Static path order, not an observed runtime instruction trace.'
        }
    }
    return {'status': 'static-instruction-evidence-only-no-runtime-DMA-proof',
            'firmware_sha256': EXPECTED,
            'version': 'VIDEO.IR.1.2-00042-PROD-1', 'findings': findings}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path,
                        default=ROOT / 'out/test2/firmware/qcom/venus-5.2/venus.mbn')
    parser.add_argument('--output', type=Path,
                        default=ROOT / 'out/video-decode/firmware-analysis/protocol.json')
    args = parser.parse_args()
    report = inspect(args.firmware)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print('SQ2_FIRMWARE_PROTOCOL_STATIC', len(report['findings']), 'findings; no device access')
    for name in report['findings']:
        print(name)


if __name__ == '__main__':
    main()
