#!/usr/bin/env python3
"""Extract the pinned Quest 2 vendor image for offline reference only.

Uses read-only debugfs, never mounts an image or writes a device. Regular files
are copied into a new out/ directory; symlinks/special files are inventoried but
not materialized. This output is not a flashable or deployable vendor tree.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import stat
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    cfg = json.loads((ROOT / 'configs/quest2-full-donor-candidate.json').read_text(encoding='utf-8-sig'))
    image, output = args.image.resolve(), args.output.resolve()
    require(image.is_file() and image.stat().st_size == cfg['vendor']['bytes'], 'Wrong vendor image size/type')
    require(digest(image) == cfg['vendor']['manifest_sha256'], 'Wrong vendor image hash')
    require(output.is_relative_to((ROOT / 'out').resolve()), 'Output must be inside workspace out/')
    require(not output.exists(), 'Refusing to overwrite an existing directory')
    # debugfs has its own command parser. Restrict quoted paths, and do not use
    # a shell; image entries are separately restricted to safe single names.
    require(not any(c in str(image) + str(output) for c in '\n\r"\\'), 'Unsupported path characters')

    def debugfs(command):
        result = subprocess.run(['debugfs', '-R', command, str(image)],
                                check=True, capture_output=True, text=True)
        return result.stdout

    entries = []
    pending = ['/']
    visited = set()
    total = 0
    while pending:
        directory = pending.pop()
        require(directory not in visited, 'Directory cycle')
        visited.add(directory)
        listing = debugfs('ls -p "' + directory + '"')
        require(listing.startswith('/'), 'Missing directory listing: ' + directory)
        for line in listing.splitlines():
            if not line:
                continue
            parts = line.split('/')
            require(len(parts) == 8, 'Unexpected debugfs listing')
            _, inode, mode, uid, gid, name, size, _ = parts
            if int(inode) == 0 or name in ('.', '..'):
                continue
            require(bool(re.fullmatch(r'[A-Za-z0-9_.@+,:=\[\]-]+', name)), 'Unsafe entry name: ' + repr(name))
            pathname = directory.rstrip('/') + '/' + name
            mode_value = int(mode, 8)
            if stat.S_ISDIR(mode_value):
                pending.append(pathname)
                continue
            entry = dict(path=pathname, inode=int(inode), mode=mode, uid=int(uid), gid=int(gid),
                         bytes=int(size or '0'), regular=stat.S_ISREG(mode_value))
            entries.append(entry)
            total += entry['bytes'] if entry['regular'] else 0
            require(len(entries) <= 10000 and total <= cfg['vendor']['bytes'], 'Unexpected extraction size')

    output.mkdir(parents=True)
    copied = 0
    for entry in sorted(entries, key=lambda item: item['path']):
        if not entry['regular']:
            entry['note'] = 'Inventoried only; no symlink or special file created'
            continue
        target = output / 'vendor' / entry['path'].lstrip('/')
        require(target.resolve().is_relative_to(output), 'Destination escaped output')
        target.parent.mkdir(parents=True, exist_ok=True)
        debugfs('dump "' + entry['path'] + '" "' + str(target) + '"')
        require(target.is_file() and target.stat().st_size == entry['bytes'], 'Extraction size mismatch: ' + entry['path'])
        entry['sha256'] = digest(target)
        copied += 1
    # Recheck source in case the image was replaced during extraction.
    require(digest(image) == cfg['vendor']['manifest_sha256'], 'Source changed during extraction')
    report = dict(build=cfg['build'], image_sha256=cfg['vendor']['manifest_sha256'],
                  oem_signature_verified=False, reference_only=True,
                  copied_regular_files=copied, copied_bytes=total, entries=entries)
    (output / 'inventory.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'QUEST2_VENDOR_REFERENCE_EXTRACTED regular_files={copied} bytes={total}')
    print('OEM_SIGNATURE_NOT_VERIFIED; reference only; symlinks/special files not materialized')


if __name__ == '__main__':
    main()
