#!/usr/bin/env python3
"""Build only: a separate video-SMMU consumer of the existing VIDEOCC.

Never changes/removes the clock provider. The module is resident until reboot.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--stage', type=Path, required=True)
p.add_argument('--videocc-phandle', type=lambda s: int(s, 0), required=True,
               help='actual current live VIDEOCC phandle, checked again by module')
p.add_argument('--with-interconnects', action='store_true',
               help='add SC8180X config, video DDR and firmware DDR paths')
a = p.parse_args()
assert 0 < a.videocc_phandle < 2**32
base = ROOT / 'out/video-decode/running-base.dtb'
assert hashlib.sha256(base.read_bytes()).hexdigest() == 'be17f33fbc8545a356f26b8f7a4c4abbd0a58e33a6401c81b18de260773058ef'
def prop(path, key, typ='x'):
    return subprocess.check_output(['fdtget', '-t', typ, str(base), path, key], text=True).strip()
gcc_path = '/soc@0/clock-controller@100000'
smmu_path = '/soc@0/iommu@15000000'
assert prop(gcc_path, 'compatible', 's') == 'qcom,gcc-sc8180x'
assert 'qcom,sc8180x-smmu-500' in prop(smmu_path, 'compatible', 's').split()
gcc = int(prop(gcc_path, 'phandle'), 16)
smmu = int(prop(smmu_path, 'phandle'), 16)
vid = a.videocc_phandle
icc_refs = []
icc_properties = ''
if a.with_interconnects:
    kernel = Path('/root/.cache/sq2-thermal/kernel')
    for source, expected in {
        'drivers/interconnect/qcom/sc8180x.c': '8d9eb593a423bca8c0f41d3ecaa57d80d970c2a9d8c70a832090ddbc06c2abdd',
        'include/dt-bindings/interconnect/qcom,sc8180x.h': '243abe325ef75647ae41ed3c866c6c6429306bd549c0e066ebdcdc42f59b76fb',
    }.items():
        assert hashlib.sha256((kernel / source).read_bytes()).hexdigest() == expected, source
    for path, compatible in [('/soc@0/interconnect@1500000', 'qcom,sc8180x-config-noc'),
                             ('/soc@0/interconnect@9680000', 'qcom,sc8180x-gem-noc'),
                             ('/soc@0/interconnect@1740000', 'qcom,sc8180x-mmss-noc'),
                             ('/interconnect-mc-virt', 'qcom,sc8180x-mc-virt')]:
        assert prop(path, 'compatible', 's') == compatible
        assert prop(path, '#interconnect-cells') == '2'
        icc_refs.append((path, int(prop(path, 'phandle'), 16)))
    cnoc, gem, mmss, mc = (item[1] for item in icc_refs)
    # Fixed sc8180x.h IDs: APSS0 -> VENUS_CFG54, VIDEO_P0=7 and
    # VIDEO_PROC=9 (mas_qxm_venus_arm9) -> EBI_CH0=1. Tag 3=always.
    icc_properties = f'''interconnects = <0x{gem:x} 0 3 0x{cnoc:x} 54 3>,
      <0x{mmss:x} 7 3 0x{mc:x} 1 3>, <0x{mmss:x} 9 3 0x{mc:x} 1 3>;
   interconnect-names = "cpu-cfg", "video-mem", "firmware-mem";'''
a.stage.mkdir(parents=True, exist_ok=False)
dest = ROOT / 'out/video-decode/hfi-overlay'
dest.mkdir(exist_ok=True)
dts = f'''/dts-v1/;
/plugin/;
/ {{ fragment@0 {{ target-path = "/soc@0";
 __overlay__ {{ #address-cells = <2>; #size-cells = <2>;
  iris1-hfi@aa00000 {{
   compatible = "microsoft,sq2-iris1-hfi";
   reg = <0 0xaa00000 0 0x100000>;
   iommus = <0x{smmu:x} 0x1300 0x60>;
   power-domains = <0x{vid:x} 0>, <0x{vid:x} 1>, <0x{vid:x} 2>;
   clocks = <0x{gcc:x} 227>, <0x{gcc:x} 225>, <0x{gcc:x} 226>,
            <0x{vid:x} 4>, <0x{vid:x} 2>, <0x{vid:x} 3>, <0x{vid:x} 0>;
   clock-names = "axic", "axi0", "axi1", "core", "vcodec", "cvp", "ahb";
   resets = <0x{gcc:x} 41>, <0x{vid:x} 0>, <0x{gcc:x} 42>, <0x{gcc:x} 43>;
   reset-names = "axi", "core", "axi0", "axi1";
   {icc_properties}
   status = "okay";
  }};
 }};
}}; }};
'''
(a.stage / 'overlay.dts').write_text(dts)
subprocess.run(['dtc', '-I', 'dts', '-O', 'dtb', '-o', str(a.stage / 'overlay.dtbo'),
                str(a.stage / 'overlay.dts')], check=True, stderr=(dest / 'dtc.log').open('w'))
blob = (a.stage / 'overlay.dtbo').read_bytes()
(a.stage / 'overlay.h').write_text('static const unsigned char overlay[] = {' + ','.join(map(str, blob)) + '};\n')
c = '''// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/of.h>
#include "overlay.h"
static bool allow;
module_param(allow, bool, 0400);
static int id = -1;
static bool check(const char *path, u32 ph) {
 struct device_node *n = of_find_node_by_path(path);
 bool ok = n && n->phandle == ph;
 of_node_put(n); return ok;
}
static int __init start(void) {
 struct device_node *n;
 int ret;
 if (!allow || !of_machine_is_compatible("microsoft,surface-pro-x")) return -EPERM;
 CHECKS
 n = of_find_node_by_path("/soc@0/iris1-hfi@aa00000");
 if (n) { of_node_put(n); return -EEXIST; }
 ret = of_overlay_fdt_apply(overlay, sizeof(overlay), &id);
 if (ret) return ret;
 __module_get(THIS_MODULE);
 pr_info("SQ2_HFI_OVERLAY id=%d resident_until_reboot NO_FIRMWARE\\n", id);
 return 0;
}
module_init(start);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SQ2 temporary dedicated video SMMU consumer; no firmware");
'''
refs = [(gcc_path, gcc), (smmu_path, smmu), ('/soc@0/clock-controller@ab00000', vid)] + icc_refs
c = c.replace('CHECKS', '\n'.join(f'if (!check("{path}", {ph})) return -EINVAL;' for path, ph in refs))
(a.stage / 'sq2_hfi_overlay.c').write_text(c)
(a.stage / 'Makefile').write_text('obj-m += sq2_hfi_overlay.o\n')
subprocess.run(['make', '-s', '-C', '/root/.cache/sq2-thermal/kernel',
    'O=/root/.cache/sq2-video-foundation/build', 'ARCH=arm64', 'CROSS_COMPILE=aarch64-linux-gnu-',
    f'M={a.stage}', 'KBUILD_MODPOST_WARN=1', 'modules'], check=True,
    stdout=(dest / 'build.log').open('w'), stderr=subprocess.STDOUT)
b = (a.stage / 'sq2_hfi_overlay.ko').read_bytes()
(dest / 'sq2_hfi_overlay.ko').write_bytes(b)
(dest / 'overlay.dtbo').write_bytes(blob)
(dest / 'candidate.json').write_text(json.dumps({'status': 'build-only-no-SMMU-or-firmware-proof',
    'module_sha256': hashlib.sha256(b).hexdigest(), 'references': refs,
    'sid': '0x1300', 'mask': '0x60', 'interconnects': a.with_interconnects}, indent=2) + '\n')
print(hashlib.sha256(b).hexdigest())
