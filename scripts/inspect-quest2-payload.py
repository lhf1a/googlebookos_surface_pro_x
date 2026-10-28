#!/usr/bin/env python3
"""Read-only payload v2 inventory from an OTA prefix; never writes an image.

Field numbers follow AOSP update_engine/update_metadata.proto. This inspects
untrusted metadata, not signature authenticity or downloaded operation data.
"""
import argparse
import collections
import json
import struct

def fields(data):
    pos = 0
    def varint():
        nonlocal pos
        value = 0
        for shift in range(0, 70, 7):
            if pos >= len(data): raise ValueError('Truncated varint')
            b = data[pos]; pos += 1
            value |= (b & 127) << shift
            if b < 128: return value
        raise ValueError('Oversize varint')
    result = collections.defaultdict(list)
    while pos < len(data):
        key = varint(); number, wire = key >> 3, key & 7
        if not number: raise ValueError('Invalid field zero')
        if wire == 0: value = varint()
        else:
            count = varint() if wire == 2 else {1:8, 5:4}.get(wire)
            if count is None or count > len(data)-pos: raise ValueError('Invalid wire length')
            value = data[pos:pos+count]; pos += count
        result[number].append(value)
    return result

def main():
    p=argparse.ArgumentParser()
    p.add_argument('prefix');p.add_argument('--payload-offset',type=int,required=True)
    args=p.parse_args()
    if args.payload_offset<0: p.error('negative offset')
    with open(args.prefix,'rb') as f:
        f.seek(args.payload_offset)
        magic,version,size,sigsize=struct.unpack('>4sQQI',f.read(24))
        if magic!=b'CrAU' or version!=2 or size>16*1024*1024: raise ValueError('Unsupported payload')
        data=f.read(size)
        if len(data)!=size: raise ValueError('Incomplete manifest')
    m=fields(data); partitions=[]
    for raw in m[13]:
        part=fields(raw); ops=[fields(op) for op in part[8]]
        info=fields(part[7][0])
        partitions.append(dict(name=part[1][0].decode(), size=info[1][0],
            sha256=info[2][0].hex(), operations=len(ops),
            operation_types=dict(collections.Counter(o[1][0] for o in ops)),
            compressed_bytes=sum(o.get(3,[0])[0] for o in ops),
            requires_old_partition=bool(part.get(6))))
    print(json.dumps(dict(metadata_authenticated=False,
        data_offset=args.payload_offset+24+size+sigsize,
        minor_version=m.get(12,[0])[0],partitions=partitions),indent=2))

if __name__=='__main__': main()
