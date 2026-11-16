#!/usr/bin/env python3
"""Apply the hardware-tested SQ2 firmware transport and PC_PREP sleep fixes.

Only edits the supplied build copy. Each file must be exactly patchable or
already patched; validate the whole input before changing any file.
"""
from pathlib import Path
import argparse
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def apply(directory, root=ROOT):
    directory = Path(directory)
    pending = []
    for name in ('venus-s2idle-transport.patch', 'venus-s2idle-quiesce.patch'):
        text = (Path(root) / 'patches' / name).read_text()
        for part in re.split(r'(?m)(?=^--- a/)', text):
            if not part.strip():
                continue
            file = part.splitlines()[0].removeprefix('--- a/')
            command = ['patch', '--batch', '--fuzz=0', '-p1']
            forward = subprocess.run(command + ['--dry-run', '--forward'], cwd=directory,
                                     input=part, text=True, capture_output=True)
            if forward.returncode == 0:
                pending.append((file, part))
                continue
            reverse = subprocess.run(command + ['--dry-run', '--reverse'], cwd=directory,
                                     input=part, text=True, capture_output=True)
            if reverse.returncode:
                raise ValueError('Unknown Venus sleep source: ' + file + '\n' + forward.stdout)
    for file, part in pending:
        subprocess.run(['patch', '--batch', '--fuzz=0', '--forward', '-p1'],
                       cwd=directory, input=part, text=True, check=True, capture_output=True)
    return [file for file, _ in pending]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    print(apply(parser.parse_args().directory))
