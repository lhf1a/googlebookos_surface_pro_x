#!/usr/bin/env python3
"""Check pinned Surface Windows video initialization, without executing it.

Raw driver/ACPI binaries must remain private. Instruction checks establish
specific host operations, not Android runtime compatibility or VPU DMA.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

DRIVER_SHA = '14369cb4acec038f94e76bc42cb65db6bc63031c18fc0d632d7d77e1c5b2b112'
IORT_SHA = 'acfe9033f58667c795a4874eb77b9a0e475154448130fc1290fdbd4bcf0713bd'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inspect(driver, iort):
    data = driver.read_bytes()
    require(hashlib.sha256(data).hexdigest() == DRIVER_SHA, 'Unrecognized driver hash')
    require(data[:2] == b'MZ', 'Expected PE executable')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    require(data[pe:pe+4] == b'PE\0\0', 'Invalid PE signature')
    machine, count = struct.unpack_from('<HH', data, pe+4)
    optional_size = struct.unpack_from('<H', data, pe+20)[0]
    require(machine == 0xaa64 and 0 < count <= 32, 'Expected ARM64 PE')
    require(struct.unpack_from('<H', data, pe+24)[0] == 0x20b, 'Expected PE32+')
    base = struct.unpack_from('<Q', data, pe+48)[0]
    sections = []
    for index in range(count):
        offset = pe+24+optional_size+40*index
        virtual_size, rva, size, raw = struct.unpack_from('<4I', data, offset+8)
        require(raw+size <= len(data), 'Section outside file')
        sections.append((rva, raw, size, virtual_size))

    def word(va):
        for rva, raw, size, _ in sections:
            if base+rva <= va and va+4 <= base+rva+size:
                return struct.unpack_from('<I', data, raw+va-base-rva)[0]
        raise ValueError('Instruction address outside file')

    def checked(va, expected):
        actual = [word(va+4*i) for i in range(len(expected))]
        require(actual == expected, 'Instruction mismatch at '+hex(va))
        return {'va': hex(va), 'words': [f'{x:08x}' for x in actual]}

    def branch(va, target):
        op = word(va)
        require(op & 0xfc000000 == 0x94000000, 'Expected ARM64 BL')
        imm = op & 0x3ffffff
        if imm & 0x2000000:
            imm -= 0x4000000
        require(va+4*imm == target, 'Unexpected branch target')
        return {'from': hex(va), 'to': hex(target), 'word': f'{op:08x}'}

    findings = {
        'queue_initializer_call': branch(0x1402f5d54, 0x140301e68),
        'table_layout': {
            'header_bytes': 24, 'queue_header_bytes': 56, 'table_bytes': 192,
            'queues': 3,
            'evidence': [checked(0x140301ed0, [0x52801809, 0xd280030e, 0xd280070f,
                                             0x5280000d, 0x2900251f, 0x52800069,
                                             0x29022509, 0x52800038, 0x29013d0e])],
            'meaning': 'Writes version0/size192 at table+0, qhdr0=24/stride56 at +8, count3/active3 at +16.'},
        'ring_base': {
            'evidence': [checked(0x140301f04, [0xb9412e68, 0x1b0d7d2a, 0x0b0a390a,
                                             0x11030149, 0xb9000989]),
                         checked(0x140301f30, [0xf9409a68, 0x9b0f21a9, 0x9100612a,
                                             0xf900018a, 0xb9000158])],
            'meaning': 'Ring device address includes table192; queue header pointer is table+24+index56. Ring size remains an engine field, not a constant established here.'},
        'shared_region_size': {
            'bytes': 16*1024*1024,
            'evidence': [checked(0x1402f61cc, [0x52a02015]),
                         checked(0x1402f6330, [0xf9782a68, 0xb82a4915]),
                         checked(0x1402f68c8, [0x000d2068, 0x000a0068])],
            'meaning': 'Engine type6 selects a0068; other engines select d2068; register receives 01000000.'},
        'shared_region_base_expression': {
            'evidence': [checked(0x1402f61ec, [0xf9405e68, 0xf9782a6a, 0x2944a508,
                                             0x0b080129, 0x4b15012b, 0xb82c494b]),
                         checked(0x1402f68c0, [0x000d2064, 0x000a0064])],
            'meaning': 'UC base is u32(object+36)+u32(object+40)-01000000, object=*(engine+184). Association with ACPI page-table start/extent remains an inference until field parsing is traced.'},
        'control_start': {
            'value': 1,
            'evidence': [checked(0x1402f6398, [0xf9782a68, 0x52800029, 0xb82a4909]),
                         checked(0x1402f68d0, [0x000d2048, 0x000a0048,
                                             0x000d204c, 0x000a004c])],
            'meaning': 'CTRL_INIT receives1; next code polls CTRL_STATUS. No claim about all other register writes or driver versions.'}}

    table = iort.read_bytes()
    require(hashlib.sha256(table).hexdigest() == IORT_SHA, 'Unrecognized IORT hash')
    require(table[:4] == b'IORT' and len(table) >= 48, 'Invalid IORT')
    require(struct.unpack_from('<I', table, 4)[0] == len(table) and sum(table) % 256 == 0,
            'IORT length/checksum failure')
    count, pos = struct.unpack_from('<II', table, 36)
    nodes = {}
    for _ in range(count):
        require(pos+16 <= len(table), 'Truncated IORT node')
        kind, length, revision, _identifier, nmap, mapoff = struct.unpack_from('<BH BIII', table, pos)
        require(length >= 16 and pos+length <= len(table), 'Invalid IORT node length')
        require(not nmap or (mapoff >= 16 and mapoff+20*nmap <= length), 'Invalid mapping range')
        node = {'kind': kind, 'mappings': []}
        if kind == 1:
            require(length > 29, 'Truncated named component')
            end = table.index(0, pos+29, pos+length)
            node['name'] = table[pos+29:end].decode('ascii')
        elif kind == 3:
            require(length >= 60, 'Truncated SMMUv2 node')
            node['base'] = struct.unpack_from('<Q', table, pos+16)[0]
        for i in range(nmap):
            node['mappings'].append(struct.unpack_from('<5I', table, pos+mapoff+20*i))
        nodes[pos] = node
        pos += length
    require(pos == len(table), 'Trailing IORT data')
    gpu = next(n for n in nodes.values() if n.get('name') == '\\_SB.GPU0')
    selected = []
    for inp, ids, output, reference, flags in gpu['mappings']:
        if inp >> 24 != 4:
            continue
        require(reference in nodes and nodes[reference].get('base') == 0x15000000,
                'Unexpected mapping provider')
        require(ids == 0 and flags == 0, 'Unexpected video-associated mapping span')
        selected.append({'input': hex(inp), 'sid': hex(output), 'smmu': '0x15000000'})
    expected = [(0x04030000, 0x2300), (0x04030001, 0x2320), (0x04030002, 0x2340),
                (0x04090000, 0x2301), (0x04090001, 0x2305), (0x040a0000, 0x2303),
                (0x040a0001, 0x2323), (0x040b0000, 0x2304), (0x040b0001, 0x2324),
                (0x040b0002, 0x2344)]
    require([(int(x['input'], 16), int(x['sid'], 16)) for x in selected] == expected,
            'IORT video-associated mappings changed')
    return {'status': 'pinned-offline-reference-not-runtime-proof',
            'driver_sha256': DRIVER_SHA, 'iort_sha256': IORT_SHA,
            'instruction_findings': findings, 'sq1_iort_mappings': selected,
            'limits': ['SQ1 IORT is not live SQ2 stream identification',
                       'engine-number to IORT input encoding remains an inference',
                       'no binary execution, firmware loading, device access or decoded frames',
                       'do not silently change production SID, secure ranges or firmware']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--iort', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.driver, args.iort)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print('WINDOWS_VIDEO_REFERENCE_OK findings=6 iort_mappings=10 NO_DEVICE_ACCESS')


if __name__ == '__main__':
    main()
