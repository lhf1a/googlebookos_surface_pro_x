#!/usr/bin/env python3
"""Bounded metadata-only EFS container triage. Does not decrypt or export NV items."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct

FIELDS = ('image_id', 'header_version', 'image_src', 'image_dest', 'image_size',
          'code_size', 'signature_ptr', 'signature_size', 'cert_chain_ptr', 'cert_chain_size')
TAGS = {b'IMGEFS1': 'FS1', b'IMGEFS2': 'FS2', b'IMGEFSG': 'FSG'}


def entropy(data):
    if not data:
        return 0.0
    return -sum((n/len(data))*math.log2(n/len(data)) for n in Counter(data).values())


def inspect(data):
    if len(data) < 512 or len(data) > 16*1024*1024:
        raise ValueError('Expected bounded EFS reference, 512 bytes to 16 MiB')
    tag = bytes(data[40:47])
    if tag not in TAGS:
        raise ValueError('No supported IMGEFS tag at 0x28; do not guess structure')
    header = dict(zip(FIELDS, struct.unpack_from('<10I', data)))
    if header['header_version'] != 3:
        raise ValueError('Unverified container header version')
    start = 40 + header['image_src']
    end = start + header['image_size']
    if (start < 48 or start % 512 or end != len(data) or not header['code_size'] or
            header['code_size'] + header['signature_size'] + header['cert_chain_size'] != header['image_size']):
        raise ValueError('MBN-v3-shaped geometry does not fit actual file')
    if (header['signature_ptr'] != header['image_dest'] + header['code_size'] or
            header['cert_chain_ptr'] != header['signature_ptr'] + header['signature_size']):
        raise ValueError('Candidate pointer/size geometry mismatch')
    payload = data[start:start+header['code_size']]
    return dict(
        sha256=hashlib.sha256(data).hexdigest(), bytes=len(data), kind=TAGS[tag],
        observed_tag=tag.decode(), tag_offset=40, tag_suffix_hex=data[47:48].hex(),
        interpretation='MBN-v3-shaped header geometry; field semantics not all independently proven',
        header=header,
        layout=[dict(region='candidate fixed header', offset=0, bytes=40),
                dict(region='EFS tag and uninterpreted extension', offset=40, bytes=start-40),
                dict(region='opaque payload', offset=start, bytes=header['code_size'])],
        entropy=dict(full=entropy(data), header_sector=entropy(data[:start]), payload=entropy(payload)),
        payload_sha256=hashlib.sha256(payload).hexdigest(),
        decrypted=False, calibration_items_recovered=False, identity_items_separated=False,
        ready_for_calibration_only_transplant=False,
        limits=['Entropy/REA classification does not identify a cipher, key or compression scheme.',
                'Zero outer signature/certificate lengths do not exclude inner authentication.',
                'This tool never scans or outputs IMEI, subscriber credentials, eSIM data or keys.'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('inputs', type=Path, nargs='+')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    result = [dict(sample=x.name, **inspect(x.read_bytes())) for x in a.inputs]
    with a.output.open('x', encoding='utf-8') as f:
        json.dump(result, f, indent=2)
        f.write('\n')
    for item in result:
        print(f"{item['sample']}: {item['observed_tag']}; header40 + extension{item['layout'][1]['bytes']} + opaque{item['layout'][2]['bytes']}; not decrypted")


if __name__ == '__main__':
    main()
