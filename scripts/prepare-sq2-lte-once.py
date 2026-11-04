#!/usr/bin/env python3
"""Offline consume-before-chain LTE lab hook; keep two entries and Qualcomm default."""
import argparse
import hashlib
from pathlib import Path
import re

BASE_SHA = '9f1ee01e43da1a769514ef6e344e7d7d69ffaa21db88d8870eb8a082f1d635ef'
ENV = '/EFI/SQ2LTE/once.env'


def prepare(data):
    if hashlib.sha256(data).hexdigest() != BASE_SHA:
        raise ValueError('Unexpected current two-entry menu SHA256')
    text = data.decode('utf-8')
    if re.findall(r'--id\s+([\w-]+)', text) != ['sq2-default', 'sq2-fallback']:
        raise ValueError('Expected exactly the current two entries')
    if text.count('set default=sq2-default\n') != 1:
        raise ValueError('Qualcomm must remain default')
    old = ' chainloader /EFI/SQ2Default/el2launch.efi\n'
    if text.count(old) != 1:
        raise ValueError('Ambiguous Qualcomm launcher')
    lines = [
        ' set sq2_lte_run=no', ' set sq2_lte_once=0', f' if [ -s {ENV} ]; then',
        f'  if load_env -f {ENV} sq2_lte_once; then',
        '   if [ "$sq2_lte_once" = "1" ]; then', '    set sq2_lte_once=0',
        f'    if save_env -f {ENV} sq2_lte_once; then',
        '     set sq2_lte_once=unknown', f'     if load_env -f {ENV} sq2_lte_once; then',
        '      if [ "$sq2_lte_once" = "0" ]; then', '       set sq2_lte_run=yes',
        '      fi', '     fi', '    fi', '   fi', '  fi', ' fi',
        ' if [ "$sq2_lte_run" = "yes" ]; then',
        '  chainloader /EFI/SQ2LTE/lte-snapshot.efi', ' else',
        '  chainloader /EFI/SQ2Default/el2launch.efi', ' fi',
    ]
    result = text.replace(old, '\n'.join(lines) + '\n').encode()
    env = b'# GRUB Environment Block\nsq2_lte_once=1\n'
    return result, env + b'#' * (1024-len(env))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('menu', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    menu, env = prepare(a.menu.read_bytes())
    a.output.mkdir(exist_ok=False)
    (a.output/'grub.cfg').write_bytes(menu)
    (a.output/'once.env').write_bytes(env)
    (a.output/'ARM').write_bytes(b'Explicit LTE read-only lab capture; consume before access\n')
    print('Prepared only: exactly 2 entries; Qualcomm default; persistent consume/readback before EFI chain')


if __name__ == '__main__':
    main()
