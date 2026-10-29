#!/usr/bin/env python3
"""Build only: temporary VIDEOCC + one-shot powered identity overlay/module.

Requires removing the prior temporary consumer-free VIDEOCC/overlay in that
order. This new overlay pins itself until reboot; never force its removal.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'out/video-decode/running-base.dtb'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--stage', type=Path, default=Path('/root/.cache/sq2-iris1-identify'))
args = p.parse_args()
STAGE = args.stage
DEST = ROOT / 'out/video-decode/iris1-identify'
assert hashlib.sha256(BASE.read_bytes()).hexdigest() == 'be17f33fbc8545a356f26b8f7a4c4abbd0a58e33a6401c81b18de260773058ef'
STAGE.mkdir(exist_ok=False)
DEST.mkdir(exist_ok=True)


def prop(path, key, typ='x'):
    return subprocess.check_output(['fdtget', '-t', typ, str(BASE), path, key], text=True).strip()


refs = []
def phandle(path, compatible=None):
    if compatible:
        assert compatible in prop(path, 'compatible', 's').split()
    value = int(prop(path, 'phandle'), 16)
    refs.append((path, value))
    return value


gcc = phandle('/soc@0/clock-controller@100000', 'qcom,gcc-sc8180x')
cxo = phandle('/soc@0/rsc@18200000/clock-controller', 'qcom,sc8180x-rpmh-clk')
pd = phandle('/soc@0/rsc@18200000/power-controller', 'qcom,sc8180x-rpmhpd')
opp_base = '/soc@0/rsc@18200000/power-controller/opp-table'
children = subprocess.check_output(['fdtget', '-l', str(BASE), opp_base], text=True).split()
opps = [opp_base + '/' + name for name in children
        if int(prop(opp_base + '/' + name, 'opp-level'), 16) == 64]
assert len(opps) == 1
opp = phandle(opps[0])
dts = f'''/dts-v1/;
/plugin/;
/ {{
 fragment@0 {{
  target-path = "/soc@0";
  __overlay__ {{
   #address-cells = <2>;
   #size-cells = <2>;
   vid: clock-controller@ab00000 {{
    compatible = "qcom,sc8180x-videocc", "qcom,sm8150-videocc";
    reg = <0 0xab00000 0 0x10000>;
    clocks = <0x{cxo:x} 0>;
    clock-names = "bi_tcxo";
    power-domains = <0x{pd:x} 6>;
    required-opps = <0x{opp:x}>;
    #clock-cells = <1>;
    #reset-cells = <1>;
    #power-domain-cells = <1>;
    status = "okay";
   }};
   iris1-identify@aa00000 {{
    compatible = "microsoft,sq2-iris1-identify";
    reg = <0 0xaa00000 0 0x100000>;
    power-domains = <&vid 0>, <&vid 1>, <&vid 2>;
    clocks = <0x{gcc:x} 227>, <0x{gcc:x} 225>, <0x{gcc:x} 226>,
             <&vid 4>, <&vid 2>, <&vid 3>, <&vid 0>;
    clock-names = "axic", "axi0", "axi1", "core", "vcodec", "cvp", "ahb";
    resets = <0x{gcc:x} 41>, <&vid 0>, <0x{gcc:x} 42>, <0x{gcc:x} 43>;
    reset-names = "axi", "core", "axi0", "axi1";
    status = "okay";
   }};
  }};
 }};
}};
'''
(STAGE / 'overlay.dts').write_text(dts)
subprocess.run(['dtc', '-@', '-I', 'dts', '-O', 'dtb', '-o', str(STAGE / 'overlay.dtbo'),
                str(STAGE / 'overlay.dts')], check=True)
# This boot tree has no /__symbols__. Exporting the local vid label would make
# Linux 6.3 reject the overlay. Keep __local_fixups__ for internal references.
subprocess.run(['fdtput', '-r', str(STAGE / 'overlay.dtbo'), '/__symbols__'], check=True)
blob = (STAGE / 'overlay.dtbo').read_bytes()
(STAGE / 'overlay.h').write_text('static const unsigned char overlay[] = {' +
    ','.join(str(b) for b in blob) + '};\n' +
    'static bool check_overlay_refs(void) { return ' +
    ' && '.join(f'check_ref("{path}", {value})' for path, value in refs) + '; }\n')
shutil.copyfile(ROOT / 'probe/video/sq2_iris1_identify.c', STAGE / 'sq2_iris1_identify.c')
(STAGE / 'Makefile').write_text('obj-m += sq2_iris1_identify.o\n')
subprocess.run(['make', '-s', '-C', '/root/.cache/sq2-thermal/kernel',
    'O=/root/.cache/sq2-video-foundation/build', 'ARCH=arm64', 'CROSS_COMPILE=aarch64-linux-gnu-',
    f'M={STAGE}', 'KBUILD_MODPOST_WARN=1', 'modules'], check=True,
    stdout=(DEST / 'build.log').open('w'), stderr=subprocess.STDOUT)
shutil.copyfile(STAGE / 'sq2_iris1_identify.ko', DEST / 'sq2_iris1_identify.ko')
shutil.copyfile(STAGE / 'overlay.dtbo', DEST / 'overlay.dtbo')
result = {'status': 'build-only-no-firmware-or-hardware-proof',
          'base_sha256': hashlib.sha256(BASE.read_bytes()).hexdigest(),
          'module_sha256': hashlib.sha256((DEST / 'sq2_iris1_identify.ko').read_bytes()).hexdigest(),
          'references': refs, 'overlay_resident_until_reboot': True}
(DEST / 'candidate.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
