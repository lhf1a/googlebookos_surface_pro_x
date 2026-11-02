#!/usr/bin/env python3
"""Strict local IORT decoder; never reads physical memory or changes hardware."""
from pathlib import Path
import argparse
import hashlib
import json
import struct

def decode(data):
    if len(data) < 48 or len(data) > 65536 or data[:4] != b'IORT':
        raise ValueError('invalid IORT signature or size')
    if struct.unpack_from('<I', data, 4)[0] != len(data) or sum(data) % 256:
        raise ValueError('IORT length/checksum mismatch')
    count, pos = struct.unpack_from('<II', data, 36)
    if not 0 < count <= 4096 or pos < 48:
        raise ValueError('invalid node count/start')
    nodes = {}
    for unused in range(count):
        if pos + 16 > len(data): raise ValueError('truncated node header')
        typ, length, rev, ident, nm, mo = struct.unpack_from('<BHBIII', data, pos)
        if length < 16 or pos + length > len(data): raise ValueError('node bounds')
        if nm > 1024 or (nm and (mo < 16 or mo + nm * 20 > length)):
            raise ValueError('mapping bounds')
        n = dict(offset=pos, type=typ, length=length, revision=rev, identifier=ident, mappings=[])
        if typ == 1:
            start, end = pos + 29, pos + (mo if nm else length)
            if start >= end: raise ValueError('named component bounds')
            try: n['name'] = data[start:data.index(b'\0', start, end)].decode('ascii')
            except (ValueError, UnicodeDecodeError) as exc: raise ValueError('component name') from exc
        if typ in (3, 4):
            if length < 24: raise ValueError('SMMU node bounds')
            n['smmu_base'] = hex(struct.unpack_from('<Q', data, pos + 16)[0])
        for j in range(nm):
            ib, ic, ob, ref, flags = struct.unpack_from('<5I', data, pos + mo + j * 20)
            if flags & ~1: raise ValueError('unknown mapping flags')
            if not flags and (ib + ic > 0xffffffff or ob + ic > 0xffffffff):
                raise ValueError('mapping range overflow')
            n['mappings'].append(dict(input_base=hex(ib), id_count=ic, output_base=hex(ob),
                output_end=hex(ob if flags & 1 else ob + ic), single_mapping=bool(flags & 1),
                output_reference=ref, flags=flags))
        nodes[pos] = n
        pos += length
    for n in nodes.values():
        for m in n['mappings']:
            target = nodes.get(m['output_reference'])
            if target is None: raise ValueError('mapping reference is not a node')
            m['target_type'] = target['type']
            m['target_smmu_base'] = target.get('smmu_base')
    selected = [n for n in nodes.values() if n['type'] == 1 and
                any(s in n['name'].upper() for s in ['IPA', 'GSI'])]
    return dict(sha256=hashlib.sha256(data).hexdigest(), bytes=len(data), checksum_valid=True,
                node_count=count, selected=selected, ownership_inferred=False)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('iort', type=Path)
    parser.add_argument('--compare', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    d = args.iort.read_bytes()
    result = decode(d)
    result['source'] = str(args.iort.resolve())
    if args.compare:
        other = args.compare.read_bytes()
        decode(other)
        result['byte_identical_to_comparison'] = d == other
        result['comparison_sha256'] = hashlib.sha256(other).hexdigest()
    text = json.dumps(result, indent=2) + '\n'
    if args.output: args.output.write_text(text)
    print(text, end='')

if __name__ == '__main__': main()
