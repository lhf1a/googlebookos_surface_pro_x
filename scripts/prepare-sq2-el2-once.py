#!/usr/bin/env python3
"""Prepare a consume-before-boot EL2 request, offline; never installs or reboots."""
import argparse
from pathlib import Path
import re

BEGIN = '# BEGIN SQ2 EL2 CONSUME-ONCE\n'
END = '# END SQ2 EL2 CONSUME-ONCE\n'
TARGETS = {'ram': 'sq2-el2-kvm-ram', 'fullram': 'sq2-el2-fullram', 'android': 'sq2-el2-android'}
ENV = '/EFI/BOOT/sq2-el2-once.env'

def prepare(menu, request='none'):
    if BEGIN in menu or END in menu:
        if menu.count(BEGIN) != 1 or menu.count(END) != 1:
            raise ValueError('Ambiguous existing EL2 one-shot block')
        a, rest = menu.split(BEGIN); _, b = rest.split(END)
        menu = a + b
    ids = set(re.findall(r'--id\s+([\w-]+)', menu))
    targets = {k: v for k, v in TARGETS.items() if v in ids}
    if not targets or (request != 'none' and request not in targets):
        raise ValueError('Requested diagnostic menu entry does not exist')
    lines = [BEGIN.rstrip(), 'search --no-floppy --fs-uuid --set=root B027-0512',
             'set sq2_el2_once=', f'if [ -s {ENV} ]; then',
             f' if load_env -f {ENV} sq2_el2_once; then']
    for k, v in targets.items():
        lines += [f'  if [ "$sq2_el2_once" = "{k}" ]; then',
                  '   set sq2_el2_once=none',
                  f'   if save_env -f {ENV} sq2_el2_once; then',
                  f'    set default={v}', '   fi', '  fi']
    lines += [' fi', 'fi', END.rstrip()]
    result = menu.rstrip() + '\n\n' + '\n'.join(lines) + '\n'
    data = ('# GRUB Environment Block\nsq2_el2_once=' + request + '\n').encode()
    return result, data + b'#' * (1024 - len(data))

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('menu', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--request', choices=['none', *TARGETS], default='none')
    a = p.parse_args()
    if a.output.exists():
        p.error('Use a fresh output directory')
    menu, env = prepare(a.menu.read_text(), a.request)
    a.output.mkdir(parents=True)
    (a.output / 'grub.cfg').write_text(menu)
    (a.output / 'sq2-el2-once.env').write_bytes(env)
