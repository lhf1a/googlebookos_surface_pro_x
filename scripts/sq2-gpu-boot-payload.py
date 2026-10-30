#!/usr/bin/env python3
"""Offline, hash-pinned SQ2 700 MHz / RPMh432 default; retain514 rescue.

Only this exact DTB has completed an SQ2 boot. No claim of measured mV,
physical PLL frequency, load stability or SQ1 compatibility. Never deploys.
"""
import hashlib
import importlib.util
from pathlib import Path
import re
import struct

NORMAL = 'sc8180x-surface-pro-x.dtb'
RESCUE = 'sc8180x-surface-pro-x-rescue.dtb'
BASE_SHA = 'a5a25e439be24ffd506d1dc026d4094c6c2add60e56db54900f8e89e30358516'
NORMAL_SHA = '6db562c2a28c82ca66a1e87b98d7c63c32c5a84f909917c8435fdc3fc19fef25'
RESCUE_SHA = '7a4d1c73cc8d64ffef979edf00e1240bef8bf15678690b7a53c45b72434dda55'
BOOT = '/boot/sq2-android1/'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build(raw):
    """Reproduce the tested bytes from the pinned CPU+GPU661/416 baseline."""
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
    if sha(result) != NORMAL_SHA:
        raise ValueError('Generated DTB differs from the boot-tested SQ2 bytes')
    return result


def verify(normal, rescue):
    if sha(normal) != NORMAL_SHA or sha(rescue) != RESCUE_SHA:
        raise ValueError('Default GPU/rescue DTB differs from pinned SQ2 profile')
    return dict(schema=1, device='SQ2', maximum_hz=700000000, opp_level=432,
                normal_sha256=NORMAL_SHA, rescue_sha256=RESCUE_SHA,
                rescue_maximum_hz=514000000,
                validation='700/432 boot confirmed; load and long-term stability untested')


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
        if not {'suspend.screen_only=1', 'nohibernate'}.issubset(linux[0].split()):
            raise ValueError('GPU menu missing screen-only protection')
        parsed += linux
        normal += not is_rescue
        rescue += is_rescue
    if parsed != re.findall(r'^\s*linux .+$', text, re.M) or not normal or not rescue:
        raise ValueError('Missing or unparsed GPU boot entries')


def policy(menu, normal, rescue):
    validate_menu(menu)
    return dict(verify(normal, rescue), menu_sha256=sha(menu))
