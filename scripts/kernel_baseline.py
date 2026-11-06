#!/usr/bin/env python3
"""Single kernel-version policy, shared by entry points and module compilers."""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
POLICY = ROOT / 'configs/kernel-baseline.json'


def policy(path=POLICY):
    value = json.loads(Path(path).read_text(encoding='utf-8'))
    if value.get('schema') != 1 or value.get('kernel_version') != '6.18.60':
        raise ValueError('Only the selected Linux 6.18.60 LTS baseline is supported')
    if value.get('kernel_release') != '6.18.60-sq2-android1':
        raise ValueError('Kernel release differs from the matched module ABI')
    return value


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def validate_kernel_source(path, selected=None):
    selected = selected or policy()
    text = (Path(path) / 'Makefile').read_text()
    parts = []
    for key in ('VERSION', 'PATCHLEVEL', 'SUBLEVEL'):
        match = re.search(r'^' + key + r'\s*=\s*(\d+)\s*$', text, re.M)
        if not match:
            raise ValueError('Missing kernel version field: ' + key)
        parts.append(match[1])
    actual = '.'.join(parts)
    if actual != selected['kernel_version']:
        raise ValueError('Unsupported kernel source ' + actual + '; expected ' + selected['kernel_version'])
    return actual


def validate_build_tree(path, selected=None):
    selected = selected or policy()
    actual = (Path(path) / 'include/config/kernel.release').read_text().strip()
    if actual != selected['kernel_release']:
        raise ValueError('Unsupported module ABI ' + actual + '; expected ' + selected['kernel_release'])
    return actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=('source', 'build'))
    parser.add_argument('path', type=Path)
    args = parser.parse_args()
    result = validate_kernel_source(args.path) if args.kind == 'source' else validate_build_tree(args.path)
    print(result)


if __name__ == '__main__':
    main()
