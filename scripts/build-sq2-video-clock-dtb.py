#!/usr/bin/env python3
"""Create a clock-only experimental DTB for the existing 6.3 VIDEOCC driver.

No decoder node, firmware load, deployment, or claim of working video decode.
Legacy GCC keeps VIDEO_AHB/XO enabled; it does not export modern AHB clock ID.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import libfdt

spec = importlib.util.spec_from_file_location('gpu', Path(__file__).with_name('build-sq2-gpu-opp.py'))
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('base', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
raw = a.base.read_bytes()
assert hashlib.sha256(raw).hexdigest() == 'a5a25e439be24ffd506d1dc026d4094c6c2add60e56db54900f8e89e30358516'
assert not a.output.exists()
f = libfdt.Fdt(raw)
before = helper.snapshot(f)
f.resize(len(raw) + 4096)

def unique_compatible(value):
    matches = [v for v in before.values() if value.encode() in v.get('compatible', b'').split(b'\0')]
    assert len(matches) == 1, value
    return matches[0]

cxo = unique_compatible('qcom,sc8180x-rpmh-clk')['phandle']
pd = unique_compatible('qcom,sc8180x-rpmhpd')['phandle']
# RPMH_REGULATOR_LEVEL_LOW_SVS = 64, resolve the existing table by contents.
opps = [v for k,v in before.items() if '/power-controller/opp-table/' in k and v.get('opp-level') == struct.pack('>I', 64)]
assert len(opps) == 1
node = '/soc@0/clock-controller@ab00000'
assert node not in before
assert before['/soc@0']['#address-cells'] == struct.pack('>I', 2)
assert before['/soc@0']['#size-cells'] == struct.pack('>I', 2)
f.add_subnode(f.path_offset('/soc@0'), 'clock-controller@ab00000')
props = {'compatible': b'qcom,sc8180x-videocc\0qcom,sm8150-videocc\0',
         'reg': struct.pack('>4I', 0, 0xab00000, 0, 0x10000),
         'clocks': cxo + struct.pack('>I', 0), 'clock-names': b'bi_tcxo\0',
         'power-domains': pd + struct.pack('>I', 6), 'required-opps': opps[0]['phandle'],
         '#clock-cells': struct.pack('>I', 1), '#reset-cells': struct.pack('>I', 1),
         '#power-domain-cells': struct.pack('>I', 1), 'status': b'okay\0'}
for name,value in props.items():
    f.setprop(f.path_offset(node), name, value)
f.pack()
after = helper.snapshot(f)
assert set(after) - set(before) == {node}
assert all(after[k] == v for k,v in before.items())
old = libfdt.Fdt(raw)
assert f.num_mem_rsv() == old.num_mem_rsv()
assert all(f.get_mem_rsv(i) == old.get_mem_rsv(i) for i in range(f.num_mem_rsv()))
data = bytes(f.as_bytearray())
a.output.parent.mkdir(parents=True, exist_ok=True)
with a.output.open('xb') as stream:
    stream.write(data)
report = {'status': 'experimental-clock-only-not-boot-tested', 'sha256': hashlib.sha256(data).hexdigest(),
          'added': node, 'original_properties_unchanged': True, 'legacy_ahb': 'enabled by existing GCC probe'}
a.output.with_suffix('.audit.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
