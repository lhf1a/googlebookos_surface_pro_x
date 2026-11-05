#!/usr/bin/env python3
"""Offline, hash-pinned SQ2 GPU profiles: validated 661/416 default, 700/432 opt-in.

`build()` returns the bytes that passed hardware validation: 661 MHz at the OEM
TURBO_L1 corner 416. `build_700()` keeps the later 700 MHz / RPMh432 transform as
an explicit opt-in only: that profile's own record says boot was confirmed while
load and long-term stability were never established, and the 2026-11-05 GMU
watchdog lockups happened while it was the default. Neither function deploys.
No claim of measured mV, physical PLL frequency or SQ1 compatibility.
"""
import hashlib
import importlib.util
from pathlib import Path
import re
import struct

NORMAL = 'sc8180x-surface-pro-x.dtb'
RESCUE = 'sc8180x-surface-pro-x-rescue.dtb'
BASE_SHA = 'a5a25e439be24ffd506d1dc026d4094c6c2add60e56db54900f8e89e30358516'
NORMAL_SHA = BASE_SHA
EXPERIMENTAL_SHA = '6db562c2a28c82ca66a1e87b98d7c63c32c5a84f909917c8435fdc3fc19fef25'
RESCUE_SHA = '7a4d1c73cc8d64ffef979edf00e1240bef8bf15678690b7a53c45b72434dda55'
BOOT = '/boot/sq2-android1/'
PROFILES = ('validated-661', 'experimental-700')
DEFAULT = 'validated-661'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build(raw):
    """Return the hardware-validated default profile unchanged (661 MHz / 416)."""
    if sha(raw) != BASE_SHA:
        raise ValueError('Expected pinned SQ2 CPU/GPU661-416 DTB; not SQ1 or arbitrary OPPs')
    return raw


def build_700(raw):
    """Opt-in 700 MHz / RPMh432 transform of the same pinned 661/416 baseline."""
    if sha(raw) != BASE_SHA:
        raise ValueError('Expected pinned SQ2 CPU/661 DTB; not SQ1 or arbitrary OPPs')
    # libfdt is required only for Linux/WSL building, not Windows deployment.
    import libfdt
    spec = importlib.util.spec_from_file_location('gpu_opp', Path(__file__).with_name('build-sq2-gpu-opp.py'))
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    f = libfdt.Fdt(raw)
    before = helper.snapshot(f)
    table = helper.TABLE
    f.setprop(f.path_offset(table+'/opp-661000000'), 'opp-level', struct.pack('>I', 432))
    template = helper.snapshot(f)[table+'/opp-661000000']
    f.resize(len(raw)+2048)
    node = f.add_subnode(f.path_offset(table), 'opp-700000000')
    for key, value in template.items():
        f.setprop(node, key, struct.pack('>Q', 700000000) if key == 'opp-hz' else value)
    f.pack()
    after = helper.snapshot(f)
    allowed = {table+'/opp-661000000': dict(before[table+'/opp-661000000'],
                                          **{'opp-level': struct.pack('>I', 432)})}
    if (set(after)-set(before) != {table+'/opp-700000000'}
            or any(after[p] != allowed.get(p, props) for p, props in before.items())):
        raise ValueError('Unexpected DT delta')
    original = libfdt.Fdt(raw)
    if (f.num_mem_rsv() != original.num_mem_rsv()
            or any(f.get_mem_rsv(i) != original.get_mem_rsv(i) for i in range(f.num_mem_rsv()))):
        raise ValueError('Reserved memory changed')
    result = bytes(f.as_bytearray())
    if sha(result) != EXPERIMENTAL_SHA:
        raise ValueError('Generated DTB differs from the 700/432 boot-tested bytes')
    return result


def verify(normal, rescue):
    nsha = sha(normal)
    if nsha == NORMAL_SHA:
        maximum_hz, opp_level = 661000000, 416
        validation = '661/416 hardware-validated default (the profile with no observed GPU/GMU faults)'
    elif nsha == EXPERIMENTAL_SHA:
        maximum_hz, opp_level = 700000000, 432
        validation = ('700/432 opt-in experiment: boot confirmed only; load and long-term '
                      'stability untested, GMU watchdog lockups observed on 2026-11-05')
    else:
        raise ValueError('Default GPU DTB differs from pinned SQ2 profile')
    if sha(rescue) != RESCUE_SHA:
        raise ValueError('Rescue GPU DTB differs from pinned SQ2 profile')
    return dict(schema=1, device='SQ2', maximum_hz=maximum_hz, opp_level=opp_level,
                normal_sha256=nsha, rescue_sha256=RESCUE_SHA,
                rescue_maximum_hz=514000000, validation=validation)


def validate_menu(menu):
    text = menu.decode('utf-8')
    blocks = re.findall(r'^menuentry [^\n]*\{[^{}]*\}', text, re.M)
    parsed, normal, rescue = [], 0, 0
    for body in blocks:
        linux = re.findall(r'^\s*linux .+$', body, re.M)
        if not linux:
            continue
        dtb = re.findall(r'^\s*devicetree (\S+)\s*$', body, re.M)
        is_rescue = 'sq2.rescue=1' in linux[0].split()
        if len(linux) != 1 or dtb != [BOOT+(RESCUE if is_rescue else NORMAL)]:
            raise ValueError('GPU menu must use pinned normal DTB and separate rescue')
        policy=[t for t in linux[0].split() if t.startswith('suspend.screen_only=')]
        if 'nohibernate' not in linux[0].split() or len(policy)!=1 or policy[0] not in ('suspend.screen_only=0','suspend.screen_only=1'):
            raise ValueError('Missing or invalid suspend policy')
        parsed += linux
        normal += not is_rescue
        rescue += is_rescue
    if parsed != re.findall(r'^\s*linux .+$', text, re.M) or not normal or not rescue:
        raise ValueError('Missing or unparsed GPU boot entries')


def policy(menu, normal, rescue):
    validate_menu(menu)
    return dict(verify(normal, rescue), menu_sha256=sha(menu))
