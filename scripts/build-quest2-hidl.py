#!/usr/bin/env python3
"""Build the 64-bit HIDL manager, ashmem mapper and OEM OMX transport.

Uses prepared Android platform headers/libraries; never installs on a device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('source-archive', 'headers', 'platform', 'cache', 'omx-headers', 'output'):
        parser.add_argument('--' + arg, type=Path, required=True)
    args = parser.parse_args()
    recipe = json.loads((ROOT / 'configs/quest2-hidl-build.json').read_text())
    if hashlib.sha256(args.source_archive.read_bytes()).hexdigest() != recipe['source_sha256']:
        parser.error('hwservicemanager source archive hash mismatch')
    args.output.mkdir(parents=True, exist_ok=False)
    source = args.output / 'hwservicemanager-source'
    source.mkdir()
    with tarfile.open(args.source_archive) as archive:
        for member in archive:
            if not (member.isfile() or member.isdir()):
                continue  # Upstream .clang-format points outside the archive.
            archive.extract(member, source, filter='data')
    subprocess.run(['patch', '-p1', '-i', str(ROOT / 'patches/quest2-hwservicemanager-enabled.patch')],
                   cwd=source, check=True)
    paths = {key: str(value.resolve()) for key, value in vars(args).items()}
    paths.update(root=str(ROOT), source=str(source.resolve()))
    results = {}
    for name, template in recipe['commands'].items():
        command = [arg.format(**paths) for arg in template]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        (args.output / (name + '.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError('Build failed: ' + name + '; see output log')
        binary = (args.output / name).read_bytes()
        if binary[:6] != b'\x7fELF\x02\x01' or int.from_bytes(binary[18:20], 'little') != 183:
            raise ValueError('Not ARM64 ELF: ' + name)
        results[name] = dict(command=command, sha256=hashlib.sha256(binary).hexdigest())
    (args.output / 'BUILD.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps({name: result['sha256'] for name, result in results.items()}, indent=2))

if __name__ == '__main__':
    main()
