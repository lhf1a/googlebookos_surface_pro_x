#!/usr/bin/env python3
"""Read-only ELF inventory of an extracted Android GPU donor (never execute it).

Run in WSL with binutils. Output is local evidence, not a compatibility verdict.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


def inspect(path, root, readelf):
    def elf(*args):
        return subprocess.run([readelf, *args, str(path)], check=True,
                              capture_output=True, text=True).stdout
    data = path.read_bytes()
    header = elf('-hW')
    dynamic = elf('-dW')
    symbols = elf('--dyn-syms', '-W')
    strings = re.findall(rb'[\x20-\x7e]{6,}', data)
    hints = sorted({s.decode()[:400] for s in strings
                    if any(k in s.lower() for k in
                           (b'/dev/kgsl', b'/dev/dri', b'/dev/ion', b'/dev/dma_heap',
                            b'adreno (tm)', b'a680', b'a650', b'0x06080001',
                            b'drm_ioctl_msm', b'ioctl_drm_', b'msm_param_',
                            b'gsl_drm_', b'msm_bo_'))})
    return {
        'path': path.relative_to(root).as_posix(),
        'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
        'class': re.search(r'Class:\s+(\S+)', header).group(1),
        'machine': re.search(r'Machine:\s+([^\n]+)', header).group(1).strip(),
        'needed': re.findall(r'\(NEEDED\).*?\[(.*?)\]', dynamic),
        'soname': re.findall(r'\(SONAME\).*?\[(.*?)\]', dynamic),
        'build_id': re.findall(r'Build ID:\s+(\w+)', elf('-nW')),
        'undefined_symbols': sorted(set(re.findall(r'\bUND\s+(\S+)', symbols))),
        'hints': hints[:100],
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root', type=Path, help='extracted vendor tree or driver directory')
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--readelf', default='readelf')
    a = p.parse_args()
    root = a.root.resolve(strict=True)
    if not root.is_dir():
        p.error('root must be a directory; extract the archive first')
    files = []
    for f in sorted(root.rglob('*')):
        if f.is_symlink() or not f.is_file():
            continue
        with f.open('rb') as stream:
            if stream.read(4) != b'\x7fELF':
                continue
        files.append(inspect(f, root, a.readelf))
    names = {Path(f['path']).name for f in files}
    names.update(n for f in files for n in f['soname'])
    for f in files:
        f['needed_not_in_inventory'] = [n for n in f['needed'] if n not in names]
    report = {
        'scope': 'static ELF audit; no execution or deployment',
        'limitations': 'Strings do not prove GPU support. Missing dependencies may be supplied by Android; inventory presence does not prove linker namespace or symbol compatibility.',
        'files': files,
    }
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Audited {len(files)} ELF files; report: {a.output}')
    if not files:
        raise SystemExit('No ELF files found; donor audit incomplete')


if __name__ == '__main__':
    main()
