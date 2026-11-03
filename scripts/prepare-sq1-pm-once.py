#!/usr/bin/env python3
"""Prepare a fail-closed, consume-and-readback-before-chain PM experiment."""
import argparse
import hashlib
from pathlib import Path

BASE_SHA = '2776ededb323928840a6a0751779dc2927f6a38657391bac1fb9119f24f99e1d'
ENV = '/EFI/SQ1PmTrial/once.env'

def prepare(data, directory='SQ1PmTrial'):
    if directory not in ('SQ1PmTrial', 'SQ1PmAuto'):
        raise ValueError('unsupported isolated PM directory')
    env_path = '/EFI/' + directory + '/once.env'
    if hashlib.sha256(data).hexdigest() != BASE_SHA:
        raise ValueError('Not the identified current SQ1/SQ2 two-entry menu')
    text = data.decode()
    old = ' chainloader /EFI/SPX/default.efi\n'
    if text.count(old) != 1 or text.count('menuentry ') != 2:
        raise ValueError('Unexpected default launcher or entry count')
    lines = [
        ' set sq1_pm_run=no', ' set sq1_pm_once=0',
        f' if [ -s {env_path} ]; then',
        f'  if load_env -f {env_path} sq1_pm_once; then',
        '   if [ "$sq1_pm_once" = "1" ]; then',
        '    set sq1_pm_once=0',
        f'    if save_env -f {env_path} sq1_pm_once; then',
        '     set sq1_pm_once=unknown',
        f'     if load_env -f {env_path} sq1_pm_once; then',
        '      if [ "$sq1_pm_once" = "0" ]; then',
        '       set sq1_pm_run=yes', '      fi', '     fi', '    fi', '   fi', '  fi', ' fi',
        ' if [ "$sq1_pm_run" = "yes" ]; then',
        f'  chainloader /EFI/{directory}/el2launch.efi', ' else',
        '  chainloader /EFI/SPX/default.efi', ' fi',
    ]
    candidate = text.replace(old, '\n'.join(lines)+'\n').encode()
    env = b'# GRUB Environment Block\nsq1_pm_once=1\n'
    assert candidate.count(b'menuentry ') == 2
    assert b'chainloader /EFI/SPX/fallback.efi' in candidate
    return candidate, env + b'#' * (1024-len(env))

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('baseline', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--directory', choices=['SQ1PmTrial','SQ1PmAuto'], default='SQ1PmTrial')
    a=p.parse_args()
    menu,env=prepare(a.baseline.read_bytes(), a.directory)
    a.output.mkdir(exist_ok=False)
    (a.output/'grub.cfg').write_bytes(menu)
    (a.output/'once.env').write_bytes(env)
