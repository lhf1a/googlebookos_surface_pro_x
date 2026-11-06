#!/usr/bin/env python3
"""Prepare an isolated 6.18 SSAM module source with deployed OF binding support.

Build with: make -C MATCHED_KERNEL_BUILD M=OUTPUT ARCH=arm64
            CROSS_COMPILE=TOOLCHAIN_PREFIX modules
Never modifies the kernel source or deploys to a device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('kernel_source', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    source = a.kernel_source/'drivers/platform/surface/aggregator'
    assert not a.output.exists(), 'Output must be fresh'
    files = sorted(p for p in source.iterdir() if p.suffix in ('.c', '.h') or p.name in ('Makefile', 'Kconfig'))
    assert any(p.name == 'core.c' for p in files)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    original = {p.name: sha(p) for p in files}
    a.output.mkdir(parents=True)
    for f in files:
        shutil.copyfile(f, a.output/f.name)
    patch = Path(__file__).resolve().parents[1]/'patches/surface-6.18-legacy-of.patch'
    for dry in (True, False):
        cmd = ['patch', '--batch', '--forward', '--fuzz=0', '-p1', '-i', str(patch)]
        if dry:
            cmd.append('--dry-run')
        subprocess.run(cmd, cwd=a.output, check=True)
    assert original == {p.name: sha(p) for p in files}, 'Source changed'
    report = dict(source=str(source.resolve()), output=str(a.output.resolve()),
                  patch_sha256=sha(patch), original_sha256=original,
                  modified_sha256={p.name: sha(a.output/p.name) for p in files},
                  build_required=True, runtime_validation_required=True)
    (a.output/'source-manifest.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
