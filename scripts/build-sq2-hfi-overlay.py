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
refs = [(gcc_path, gcc), (smmu_path, smmu), ('/soc@0/clock-controller@ab00000', vid)]
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
    'sid': '0x1300', 'mask': '0x60'}, indent=2) + '\n')
print(hashlib.sha256(b).hexdigest())
