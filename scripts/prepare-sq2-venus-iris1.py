#!/usr/bin/env python3
"""Prepare isolated, compile-only Iris1 HFI changes; no platform match or install.

Hardware resource/power and firmware lifetime support are still required.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
HASHES = {
    'core.c': '48f33e0a262e3e70ab3e65cf0ffded6e451a52f5f41c5cf8179cf4da2fd8e922',
    'core.h': '38cfa27196a5204c7250b11ed140821751f6ee8af980715a0ba06db81aabd590',
    'hfi_venus.c': '8323b3366c1044eef997b27f3215d38a8526e56fb3c25b0e0a2fff551ac06064',
    'hfi_venus_io.h': '767743e0ef215aca2fb39470cd3777c8880b14f9ceb41c220ed423c96caf4b7e',
    'firmware.c': 'abd83c9fd854784ddec523bc21d941f3ee5373ca587f19c838e52375cc7c8e71',
}


def replace(text, before, after):
    if text.count(before) != 1:
        raise ValueError('expected one source anchor: ' + repr(before))
    return text.replace(before, after, 1)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kernel', type=Path, default=Path('/root/.cache/sq2-thermal/kernel'))
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--firmware-lease', action='store_true',
                   help='compile the independent PAS memory owner (no enabled caller)')
    args = p.parse_args()
    src = args.kernel / 'drivers/media/platform/qcom/venus'
    for name, digest in HASHES.items():
        if hashlib.sha256((src / name).read_bytes()).hexdigest() != digest:
            raise ValueError('source hash mismatch: ' + name)
    if args.firmware_lease:
        loader = args.kernel / 'drivers/soc/qcom/mdt_loader.c'
        if hashlib.sha256(loader.read_bytes()).hexdigest() != \
                '5dd1d028daafe8006400ca50bc6b7ec54a933521f0f6f1c672445e92a9c3d30b':
            raise ValueError('MDT loader source hash mismatch')
    if args.output.exists():
        raise ValueError('output exists; preserve earlier candidates')
    args.output.mkdir(parents=True)
    for f in src.iterdir():
        if f.is_file() and (f.suffix in ('.c', '.h') or f.name == 'Makefile'):
            shutil.copyfile(f, args.output / f.name)
    shutil.copyfile(ROOT / 'probe/video/venus-iris1.h', args.output / 'venus-iris1.h')
    core = (src / 'core.h').read_text()
    core = replace(core, '\tenum hfi_version hfi_version;\n',
                   '\t/* VPU hardware is independent of HFI packet version. */\n'
                   '\tbool iris1;\n\tenum hfi_version hfi_version;\n')
    core = replace(core, '#define IS_V4(core)',
                   '#define IS_IRIS1(core) ((core)->res->iris1)\n#define IS_V4(core)')
    # Keep all pre-existing SoC tables unchanged (zero-initialized iris1=false).
    (args.output / 'core.h').write_text(core)
    hfi = (src / 'hfi_venus.c').read_text()
    hfi = replace(hfi, '#include "firmware.h"', '#include "firmware.h"\n#include "venus-iris1.h"')
    hfi = replace(hfi, '\tif (IS_V6(hdev->core)) {\n\t\tmask_val = readl(wrapper_base + WRAPPER_INTR_MASK);',
                  '\tif (IS_IRIS1(hdev->core)) {\n'
                  '\t\tmask_val = venus_iris1_irq_mask(readl(wrapper_base + WRAPPER_INTR_MASK));\n'
                  '\t} else if (IS_V6(hdev->core)) {\n\t\tmask_val = readl(wrapper_base + WRAPPER_INTR_MASK);')
    hfi = replace(hfi, '\tvenus_set_registers(hdev);',
                  '\tif (IS_IRIS1(hdev->core))\n\t\tvenus_iris1_clock_config(hdev->core);\n'
                  '\tvenus_set_registers(hdev);')
    hfi = replace(hfi, '\tret = venus_boot_core(hdev);',
                  '\tif (IS_IRIS1(hdev->core))\n'
                  '\t\tvenus_iris1_dsp_map(hdev->core, hdev->ifaceq_table.da, SHARED_QSIZE);\n\n'
                  '\tret = venus_boot_core(hdev);')
    (args.output / 'hfi_venus.c').write_text(hfi)
    fw = (src / 'firmware.c').read_text()
    fw = replace(fw, '#include "hfi_venus_io.h"', '#include "hfi_venus_io.h"\n#include "venus-iris1.h"')
    fw = replace(fw, '\tret = venus_load_fw(core, fwpath, &mem_phys, &mem_size);',
                 '\tif (IS_IRIS1(core)) {\n'
                 '\t\tif (!IS_V4(core))\n\t\t\treturn -EINVAL;\n'
                 '\t\tvenus_iris1_clock_config(core);\n\t}\n\n'
                 '\tret = venus_load_fw(core, fwpath, &mem_phys, &mem_size);')
    (args.output / 'firmware.c').write_text(fw)
    if args.firmware_lease:
        for name in ('sq2-fw-transaction.h', 'sq2-venus-fw.h', 'sq2-venus-fw.c',
                     'sq2_video_fw_layout.h'):
            shutil.copyfile(ROOT / 'probe/video' / name, args.output / name)
        with (args.output / 'Makefile').open('a') as f:
            f.write('\nvenus-core-objs += sq2-venus-fw.o\n')
    manifest = {'status': 'compile-only-no-platform-match-no-hardware-validation',
                'firmware_lease_compiled_without_callsite': args.firmware_lease,
                'input_sha256': HASHES,
                'output_sha256': {f.name: hashlib.sha256(f.read_bytes()).hexdigest()
                                  for f in sorted(args.output.iterdir()) if f.is_file()}}
    (args.output / 'candidate.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(args.output)


if __name__ == '__main__':
    main()
