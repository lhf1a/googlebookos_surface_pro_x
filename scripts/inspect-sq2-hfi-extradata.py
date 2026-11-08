#!/usr/bin/env python3
'''Inspect bounded firmware metadata records, using msm_vidc.h field order.
Values are raw bitstream units, not invented/default HDR values.
'''
import argparse
import json
from pathlib import Path
import struct

VUI_FIELDS = ['video_signal_present','video_format','bit_depth_y','bit_depth_c',
              'full_range','color_description_present','primaries','transfer','matrix',
              'chroma_location_present','chroma_format','separate_color_plane',
              'chroma_top','chroma_bottom']
MASTER_FIELDS = ['primary_x0','primary_x1','primary_x2','primary_y0','primary_y1',
                 'primary_y2','white_x','white_y','max_luminance','min_luminance']
LAYOUT = {0x7f100006: ('vui', VUI_FIELDS),
          0x15: ('mastering', MASTER_FIELDS),
          0x16: ('content_light', ['max_content','max_average'])}

def inspect(data):
    records = []
    offset = 0
    while offset + 20 <= len(data):
        size, version, port, kind, used = struct.unpack_from('<5I', data, offset)
        if size < 20 or size % 4 or size > len(data)-offset or used > size-20:
            raise ValueError(f'invalid record bounds at {offset}: size={size}, payload={used}')
        record = dict(offset=offset, size=size, version=version, port=port, type=hex(kind),
                      payload_bytes=used)
        if kind == 0:
            if used:
                raise ValueError('terminator has payload')
            records.append(record)
            return dict(records=records, consumed=offset+size, terminated=True)
        payload = data[offset+20:offset+20+used]
        if kind in LAYOUT:
            name, fields = LAYOUT[kind]
            if len(payload) < 4*len(fields):
                raise ValueError(f'truncated {name}')
            record['name'] = name
            record['values'] = dict(zip(fields, struct.unpack_from('<'+'I'*len(fields), payload)))
        else:
            record['payload_hex'] = payload.hex()
        records.append(record)
        offset += size
    raise ValueError('missing extradata terminator')

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    args = p.parse_args()
    if args.input.stat().st_size > 1024*1024:
        p.error('diagnostic input exceeds 1 MiB')
    print(json.dumps(inspect(args.input.read_bytes()), indent=2))

if __name__=='__main__':
    main()
