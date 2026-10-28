#!/usr/bin/env python3
"""Stage only pinned GPU libraries for private GLES tests; no system writes."""
import hashlib
import argparse
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gsl-dmabuf', action='store_true',
                        help='Experimental v819.1 GSL with v77 GLES, separate private directory')
    args = parser.parse_args()
    source = ROOT / 'out/quest2-gpu/q2-v77/lib64'
    output = ROOT / ('out/quest2-gpu/gles-v819-dmabuf' if args.gsl_dmabuf else 'out/quest2-gpu/gles-v77')
    manifest = json.loads((ROOT / 'configs/quest2-gles-v77-inputs.json').read_text(encoding='utf-8-sig'))
    # Check every input before creating any staged copy.
    for name, digest in manifest['files'].items():
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != digest:
            raise SystemExit(f'Unexpected donor hash: {name}')
    replacements = {}
    if args.gsl_dmabuf:
        for name, digest in manifest['dmabuf_gsl_candidate'].items():
            path = ROOT / 'out/quest2-gpu/runtime' / name
            if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise SystemExit(f'Unexpected alternate GSL input hash: {name}')
            replacements['libgsl.so' if name == 'notgsl.so' else name] = (path, digest)
        probe_dir = ROOT / 'out/quest2-gpu/gles-v77'
        for line in (probe_dir / 'PROBE_SHA256SUMS').read_text().splitlines():
            digest, name = line.split()
            if Path(name).name != name or hashlib.sha256((probe_dir / name).read_bytes()).hexdigest() != digest:
                raise SystemExit('Build the current GLES probe before staging the alternate GSL')
    output.mkdir(parents=True, exist_ok=True)
    sums = []
    for name, digest in manifest['files'].items():
        target = output / Path(name).name
        if target.name in replacements:
            continue
        shutil.copyfile(source / name, target)
        sums.append(f'{digest}  {target.name}')
    for name, (path, digest) in replacements.items():
        shutil.copyfile(path, output / name)
        sums.append(f'{digest}  {name}')
    if args.gsl_dmabuf:
        # Keep the original ELF bytes/SONAME. The loader resolves the libgsl.so
        # NEEDED name through this private file; no global loader changes.
        for name in ('gles-readback', 'libqti-mapper3-bridge.so', 'libquest2-egl-wrapper.so', 'PROBE_SHA256SUMS'):
            shutil.copyfile(probe_dir / name, output / name)
        # The modern GSL uses its actual DMA-BUF allocator, not the ION shim.
        lines = [line for line in (output / 'PROBE_SHA256SUMS').read_text().splitlines()
                 if not line.endswith('  libquest2-ion-dmaheap.so')]
        (output / 'PROBE_SHA256SUMS').write_text('\n'.join(lines) + '\n')
    (output / 'SHA256SUMS').write_text('\n'.join(sums) + '\n')
    print('QUEST2_GLES_DONOR_STAGED', output.name)

if __name__ == '__main__':
    main()
