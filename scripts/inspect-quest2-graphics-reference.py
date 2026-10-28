#!/usr/bin/env python3
"""Hash-check the matched firmware stack and inventory its ELF dependencies.

Requires ARM64 binutils; no target execution. Raw outputs remain under out/.
This is not a loader/deployment script or an OEM signature validator.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = ROOT / 'out/quest2-gpu/q2-v77/lib64'
    output = ROOT / 'out/quest2-gpu/firmware-reference'
    files = {}
    for config in ('quest2-gles-v77-inputs.json', 'quest2-v77-graphics-reference.json'):
        files.update(json.loads((ROOT / 'configs' / config).read_text(encoding='utf-8-sig'))['files'])
    # Check all inputs before producing any report or using fixed addresses.
    for name, digest in files.items():
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != digest:
            raise ValueError('Unexpected firmware library: ' + name)
    output.mkdir(parents=True, exist_ok=True)
    report = {'build': '51584880050400150', 'oem_signature_verified': False, 'libraries': {}}
    for name, digest in files.items():
        dynamic = subprocess.check_output(['aarch64-linux-gnu-readelf', '-d', str(source / name)], text=True)
        report['libraries'][name] = {
            'sha256': digest,
            'needed': re.findall(r'\(NEEDED\).*\[(.*?)\]', dynamic),
            'soname': re.findall(r'\(SONAME\).*\[(.*?)\]', dynamic),
        }
    # Addresses apply only to the exact pinned binaries; preserve raw evidence
    # privately, while describing the interpretation and limits in the docs.
    ranges = {
        'hw/android.hardware.graphics.mapper@3.0-impl-qti-display.so':
            [(0xd500, 0xd5d0), (0xe2b0, 0xe358), (0xea80, 0xeb1c)],
        'libqdMetaData.so': [(0x44d0, 0x4940)],
        'libgrallocutils.so': [(0xb480, 0xb590), (0xff60, 0x100d0)],
        'libadreno_utils.so': [(0x10eb8, 0x10edc)],
        'libgsl.so': [(0x20f260, 0x20f858)],
    }
    for name, spans in ranges.items():
        for start, end in spans:
            text = subprocess.check_output([
                'aarch64-linux-gnu-objdump', '-d', f'--start-address={start}',
                f'--stop-address={end}', str(source / name)], text=True)
            (output / f'{Path(name).name}-{start:x}.disasm').write_text(text)
    (output / 'inventory.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'QUEST2_MATCHED_GRAPHICS_REFERENCE_OK libraries={len(files)}')


if __name__ == '__main__':
    main()
