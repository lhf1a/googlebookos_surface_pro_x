#!/usr/bin/env python3
"""Reconstruct only the pinned v77 vendor image from downloaded OTA ranges.

No networking, mounting or device writes. Output must be a new .img under out/.
Operation and partition hashes are checked, not the OEM signature or full ZIP.
"""
import argparse
import hashlib
import importlib
import json
import lzma
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
fields = importlib.import_module('inspect-quest2-payload').fields


def require(ok, message):
    if not ok:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('vendor_range', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    cfg = json.loads((ROOT / 'configs/quest2-full-donor-candidate.json').read_text(encoding='utf-8-sig'))
    vendor = cfg['vendor']
    output = args.output.resolve()
    require(output.is_relative_to((ROOT / 'out').resolve()) and output.suffix == '.img',
            'Output must be a new .img inside the workspace out directory')
    require(not output.exists(), 'Refusing to overwrite an existing image')
    with args.prefix.open('rb') as source:
        source.seek(cfg['payload_offset'])
        magic, version, count, signature = struct.unpack('>4sQQI', source.read(24))
        require(magic == b'CrAU' and version == 2 and count == cfg['manifest_bytes'], 'Unexpected payload header')
        data_offset = cfg['payload_offset'] + 24 + count + signature
        require(data_offset == cfg['payload_data_absolute_offset'], 'Unexpected payload data offset')
        manifest_data = source.read(count)
        require(len(manifest_data) == count, 'Truncated manifest')
    manifest = fields(manifest_data)
    require(manifest.get(12, [0]) == [0], 'Incremental payloads are not supported')
    block = manifest[3][0]
    require(block == 4096, 'Unexpected block size')
    parts = [fields(raw) for raw in manifest[13]]
    parts = [p for p in parts if p[1] == [b'vendor']]
    require(len(parts) == 1 and not parts[0].get(6), 'Expected one full vendor partition')
    part = parts[0]
    info = fields(part[7][0])
    require(info[1] == [vendor['bytes']] and info[2][0].hex() == vendor['manifest_sha256'],
            'Unexpected vendor identity')
    require(len(part[8]) == vendor['operations'], 'Unexpected operation count')
    require(args.vendor_range.stat().st_size == vendor['compressed_bytes'], 'Wrong vendor range length')
    packed_data = args.vendor_range.read_bytes()
    operations = []
    covered = bytearray(vendor['bytes'] // block)
    for raw in part[8]:
        op = fields(raw)
        require(op[1] == [8], 'Only REPLACE_XZ operations are supported')
        start = data_offset + op[2][0] - vendor['archive_range_inclusive'][0]
        length = op[3][0]
        require(0 <= start <= len(packed_data) and 0 < length <= len(packed_data) - start,
                'Operation outside downloaded range')
        packed = packed_data[start:start + length]
        require(hashlib.sha256(packed).digest() == op[8][0], 'Compressed operation hash mismatch')
        extents = []
        for raw_extent in op[6]:
            extent = fields(raw_extent)
            first, blocks = extent[1][0], extent[2][0]
            require(0 <= first <= len(covered) and 0 < blocks <= len(covered) - first,
                    'Destination extent out of bounds')
            require(not any(covered[first:first + blocks]), 'Overlapping destination extents')
            covered[first:first + blocks] = b'\x01' * blocks
            extents.append((first * block, blocks * block))
        operations.append((packed, extents))
    require(all(covered), 'Vendor image has uncovered blocks')
    output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation; this tool never overwrites the source or an image.
    with output.open('xb') as target:
        try:
            target.truncate(vendor['bytes'])
            for packed, extents in operations:
                size = sum(length for _, length in extents)
                decoder = lzma.LZMADecompressor()
                data = decoder.decompress(packed, max_length=size + 1)
                require(len(data) == size and decoder.eof and not decoder.unused_data,
                        'Unexpected decompressed operation size/stream')
                position = 0
                for offset, length in extents:
                    target.seek(offset)
                    target.write(data[position:position + length])
                    position += length
            target.flush()
        except Exception:
            # Leave a failed file for inspection; never report it as verified.
            raise
    with output.open('rb') as image:
        digest = hashlib.file_digest(image, 'sha256').hexdigest()
    require(digest == vendor['manifest_sha256'], 'Final partition hash mismatch')
    print('QUEST2_VENDOR_REBUILT operations=131 sha256=' + digest)
    print('OEM_SIGNATURE_NOT_VERIFIED; this image is a reference, not a Surface flash image')


if __name__ == '__main__':
    main()
