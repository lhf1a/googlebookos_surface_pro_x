#!/usr/bin/env python3
"""Stage only pinned GPU libraries for private GLES tests; no system writes."""
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]

def main():
    source = ROOT / 'out/quest2-gpu/q2-v77/lib64'
    output = ROOT / 'out/quest2-gpu/gles-v77'
    manifest = json.loads((ROOT / 'configs/quest2-gles-v77-inputs.json').read_text(encoding='utf-8-sig'))
    # Check every input before creating any staged copy.
    for name, digest in manifest['files'].items():
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != digest:
            raise SystemExit(f'Unexpected donor hash: {name}')
    output.mkdir(parents=True, exist_ok=True)
    sums = []
    for name, digest in manifest['files'].items():
        target = output / Path(name).name
        shutil.copyfile(source / name, target)
        sums.append(f'{digest}  {target.name}')
    (output / 'SHA256SUMS').write_text('\n'.join(sums) + '\n')
    print('QUEST2_GLES_DONOR_STAGED')

if __name__ == '__main__':
    main()
