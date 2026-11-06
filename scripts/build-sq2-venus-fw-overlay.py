#!/usr/bin/env python3
"""Build an opt-in EL2 firmware SID2342 child; never deploy or change providers."""
import argparse
import hashlib
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--stage', type=Path, required=True)
p.add_argument('--kernel', type=Path, required=True)
p.add_argument('--kernel-output', type=Path, required=True)
p.add_argument('--smmu-phandle', type=lambda s: int(s, 0), required=True)
a = p.parse_args()
from kernel_baseline import validate_kernel_source, validate_build_tree
validate_kernel_source(a.kernel)
validate_build_tree(a.kernel_output)
if not 0 < a.smmu_phandle < 2**32:
    p.error('invalid live SMMU phandle')
a.stage.mkdir(parents=True, exist_ok=False)
dts = '''/dts-v1/;
/plugin/;
/ { fragment@0 { target-path = "/soc@0/venus-sq2@aa00000";
 __overlay__ { video-firmware { iommus = <PHANDLE 0x2342 0>; }; };
}; };
'''.replace('PHANDLE', hex(a.smmu_phandle))
(a.stage / 'overlay.dts').write_text(dts)
subprocess.run(['dtc', '-I', 'dts', '-O', 'dtb', '-o', str(a.stage / 'overlay.dtbo'),
                str(a.stage / 'overlay.dts')], check=True)
blob = (a.stage / 'overlay.dtbo').read_bytes()
(a.stage / 'overlay.h').write_text('static const unsigned char overlay[] = {' +
                                  ','.join(map(str, blob)) + '};\n')
source = r'''// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/of.h>
#include <asm/virt.h>
#include "overlay.h"
static bool allow;
module_param(allow, bool, 0400);
static int id = -1;
static int __init start(void)
{
 struct device_node *np;
 bool valid;
 int ret;
 if (!allow || !is_kernel_in_hyp_mode() ||
     !of_machine_is_compatible("microsoft,surface-pro-x") ||
     !of_machine_is_compatible("qcom,sc8180x")) return -EPERM;
 np = of_find_node_by_path("/soc@0/iommu@15000000");
 valid = np && np->phandle == PHANDLE &&
         of_device_is_compatible(np, "qcom,sc8180x-smmu-500");
 of_node_put(np);
 if (!valid) return -EINVAL;
 np = of_find_node_by_path("/soc@0/venus-sq2@aa00000");
 valid = np && of_device_is_compatible(np, "microsoft,sq2-venus-lab");
 of_node_put(np);
 if (!valid) return -ENODEV;
 np = of_find_node_by_path("/soc@0/venus-sq2@aa00000/video-firmware");
 if (np) { of_node_put(np); return -EEXIST; }
 ret = of_overlay_fdt_apply(overlay, sizeof(overlay), &id);
 if (ret) return ret;
 __module_get(THIS_MODULE);
 pr_info("SQ2_FW_OVERLAY id=%d sid=2342 resident_until_reboot NO_CPU_START\n", id);
 return 0;
}
module_init(start);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Opt-in SQ2 EL2 firmware SID2342 candidate child");
'''.replace('PHANDLE', hex(a.smmu_phandle))
(a.stage / 'sq2_fw_overlay.c').write_text(source)
(a.stage / 'Makefile').write_text('obj-m += sq2_fw_overlay.o\n')
subprocess.run(['make', '-s', '-C', str(a.kernel), f'O={a.kernel_output}',
                'ARCH=arm64', 'CROSS_COMPILE=aarch64-linux-gnu-',
                f'M={a.stage}', 'modules'], check=True)
print(hashlib.sha256((a.stage / 'sq2_fw_overlay.ko').read_bytes()).hexdigest())
