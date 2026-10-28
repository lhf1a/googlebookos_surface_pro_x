#!/usr/bin/env python3
"""Audit pinned Quest video donor offline; never load libraries or change device."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'out/quest2-gpu/vendor-reference-v77'


def main():
    inventory = json.loads((BASE / 'inventory.json').read_text())
    cfg = json.loads((ROOT / 'configs/quest2-full-donor-candidate.json').read_text())
    assert inventory['image_sha256'] == cfg['vendor']['manifest_sha256']
    assert inventory['build'] == cfg['build']
    selected = {}
    for entry in inventory['entries']:
        name = entry['path']
        if not entry['regular'] or not re.search(r'codec|Omx|media\.c2|media\.omx|venus|vpu', name, re.I):
            continue
        path = BASE / 'vendor' / name.lstrip('/')
        data = path.read_bytes()
        assert len(data) == entry['bytes']
        assert hashlib.sha256(data).hexdigest() == entry['sha256'], name
        item = {'sha256': entry['sha256'], 'bytes': len(data)}
        if data.startswith(b'\x7fELF'):
            item['elf_bits'] = {1: 32, 2: 64}[data[4]]
            dynamic = subprocess.check_output(['readelf', '-dW', str(path)], text=True)
            item['needed'] = re.findall(r'\(NEEDED\).*\[(.*?)\]', dynamic)
            strings = subprocess.check_output(['strings', '-a', str(path)], text=True, errors='replace')
            item['interface_strings'] = sorted(set(s for s in strings.splitlines() if
                re.search(r'/dev/|VIDIOC_|V4L2_CID_|HFI_|UBWC|ION_IOC|dma_heap', s)))
        selected[name] = item
    output = ROOT / 'out/quest2-gpu/video-reference-audit.json'
    output.write_text(json.dumps({'build': cfg['build'], 'image_sha256': inventory['image_sha256'],
        'status': 'offline-only-not-loaded', 'files': selected}, indent=2) + '\n')
    print('VIDEO_REFERENCE_AUDITED', len(selected))
    for name, item in selected.items():
        if 'elf_bits' in item and re.search(r'OmxVdec|qcodec2_v4l2|bin/hw', name):
            print(name, item['elf_bits'], 'bit', 'NEEDED', ', '.join(item['needed']))


if __name__ == '__main__':
    main()
