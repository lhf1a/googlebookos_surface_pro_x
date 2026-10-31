#!/usr/bin/env python3
"""Isolated DTB candidate for upstream SC8180X eDP PHY power-domain correction.

Only phy@aec2a00/power-domains changes: MDSS_GDSC -> existing RPMh MX.
No added voltage/frequency/OPP values. Does not deploy or change defaults.
V13 on the 512-GB LGD_MP1.0 device still produced black output at 5.4/60;
the live DT confirmed the new domain. This is not a validated black-screen fix.
Upstream commit: 24e98cb3d5e2c86565680e00008a794b4eac0040.
Requires the exact tested 700-MHz DTB and python3-libfdt.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import libfdt

BASE_SHA = '6db562c2a28c82ca66a1e87b98d7c63c32c5a84f909917c8435fdc3fc19fef25'
PHY = '/soc@0/phy@aec2a00'


def build(baseline, output):
    raw = baseline.read_bytes()
    if hashlib.sha256(raw).hexdigest() != BASE_SHA:
        raise ValueError('Expected exact tested SQ2 700-MHz baseline')
    if output.exists():
        raise FileExistsError(output)
    spec = importlib.util.spec_from_file_location('gpu_snapshot', Path(__file__).with_name('build-sq2-gpu-opp.py'))
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    fdt = libfdt.Fdt(raw)
    before = helper.snapshot(fdt)
    assert before[PHY]['compatible'] == b'qcom,sc8180x-edp-phy\0'
    assert b'microsoft,surface-pro-x\0' in before['/']['compatible']
    rpmh = [v for v in before.values() if v.get('compatible') == b'qcom,sc8180x-rpmhpd\0']
    assert len(rpmh) == 1
    old = before['/soc@0/clock-controller@af00000']['phandle'] + struct.pack('>I', 0)
    assert before[PHY]['power-domains'] == old
    # SC8180X_MX = 9 in the matching kernel's qcom-rpmpd binding.
    new = rpmh[0]['phandle'] + struct.pack('>I', 9)
    fdt.setprop(fdt.path_offset(PHY), 'power-domains', new)
    after = helper.snapshot(fdt)
    assert after.keys() == before.keys()
    changed = [(node, prop) for node in before for prop in before[node]
               if after[node].get(prop) != before[node][prop]]
    assert changed == [(PHY, 'power-domains')]
    for node in before:
        assert before[node].keys() == after[node].keys()
    result = bytes(fdt.as_bytearray())
    assert helper.snapshot(libfdt.Fdt(result)) == after
    output.write_bytes(result)
    print(json.dumps({'baseline_sha256': BASE_SHA, 'output_sha256': hashlib.sha256(result).hexdigest(),
                      'changed': changed, 'old_cells': list(struct.unpack('>II', old)),
                      'new_cells': list(struct.unpack('>II', new)), 'hardware_status': 'untested'}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    build(args.baseline, args.output)
