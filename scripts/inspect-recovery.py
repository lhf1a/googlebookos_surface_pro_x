#!/usr/bin/env python3
"""Read-only GPT/Android boot inspection. Accepts regular image files only."""
import argparse
import hashlib
import json
from pathlib import Path
import stat
import struct
import zlib


def inspect(path):
    path = Path(path)
    info = path.stat()
    if not stat.S_ISREG(info.st_mode):
        raise ValueError('Only a regular disk-image file is accepted')
    total = info.st_size
    if total % 512 or total < 17408:
        raise ValueError('Invalid 512-byte-sector image size')
    with path.open('rb') as f:
        def read(offset, length):
            if offset < 0 or length < 0 or offset + length > total:
                raise ValueError('Read outside image')
            f.seek(offset)
            data = f.read(length)
            if len(data) != length:
                raise ValueError('Short image read')
            return data

        def header(lba):
            h = read(lba * 512, 512)
            if h[:8] != b'EFI PART':
                raise ValueError('Missing GPT header')
            hs, crc = struct.unpack_from('<II', h, 12)
            if not 92 <= hs <= 512:
                raise ValueError('Invalid GPT header size')
            checked = bytearray(h[:hs])
            checked[16:20] = bytes(4)
            if zlib.crc32(checked) != crc:
                raise ValueError('GPT header CRC mismatch')
            here, other, first, last = struct.unpack_from('<QQQQ', h, 24)
            if here != lba or not 1 < first <= last < total // 512:
                raise ValueError('Invalid GPT geometry')
            table_lba, count, es, tc = struct.unpack_from('<QIII', h, 72)
            if not 128 <= es <= 4096 or es % 128 or not 0 < count <= 16384:
                raise ValueError('Invalid GPT entry geometry')
            table = read(table_lba * 512, count * es)
            if zlib.crc32(table) != tc:
                raise ValueError('GPT table CRC mismatch')
            return h, table, es, count, other, first, last

        h, table, es, count, backup, usable_first, usable_last = header(1)
        bh, bt, bes, bc, back_pointer, bf, bl = header(backup)
        if backup != total // 512 - 1 or back_pointer != 1 or bt != table or (bes, bc, bf, bl) != (es, count, usable_first, usable_last) or h[56:72] != bh[56:72]:
            raise ValueError('Primary and backup GPT disagree')
        partitions = []
        ranges = []
        for i in range(count):
            e = table[i * es:(i + 1) * es]
            if not any(e[:16]):
                continue
            start, end = struct.unpack_from('<QQ', e, 32)
            if not usable_first <= start <= end <= usable_last:
                raise ValueError('Partition outside usable space')
            if any(start <= b and a <= end for a, b in ranges):
                raise ValueError('Overlapping partitions')
            ranges.append((start, end))
            name = e[56:128].decode('utf-16le').rstrip('\0')
            size = (end - start + 1) * 512
            entry = dict(index=i + 1, name=name, offset=start * 512, bytes=size)
            prefix = read(start * 512, min(size, 4096))
            if prefix.startswith(b'ANDROID!'):
                version = struct.unpack_from('<I', prefix, 40)[0]
                entry['android_boot_header_version'] = version
                if version in (3, 4):
                    ks, rs = struct.unpack_from('<II', prefix, 8)
                    if 4096 + ((ks + 4095) // 4096 * 4096) + rs > size:
                        raise ValueError('Android boot payload exceeds partition')
                    entry.update(kernel_bytes=ks, ramdisk_bytes=rs)
                    if ks:
                        k = read(start * 512 + 4096, min(ks, 64))
                        entry['arm64_image_magic'] = k[56:60].hex() if len(k) >= 60 else None
            elif prefix.startswith(b'VNDRBOOT'):
                entry['vendor_boot_header_version'] = struct.unpack_from('<I', prefix, 8)[0]
            partitions.append(entry)
    return dict(image_bytes=total, gpt_verified=True, partitions=partitions)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(inspect(args.image), indent=2) + '\n'
    if args.output:
        with args.output.open('x', encoding='utf-8') as out:
            out.write(result)
    else:
        print(result, end='')
