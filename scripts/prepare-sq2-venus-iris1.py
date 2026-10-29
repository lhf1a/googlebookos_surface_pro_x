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
                   help='compile PAS owner and SQ2 firmware dispatch (platform probe still blocked)')
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
    shutil.copyfile(ROOT / 'probe/video/sq2-venus-dma.h', args.output / 'sq2-venus-dma.h')
    for name in ('sq2-venus-platform.c', 'sq2-venus-platform.h', 'sq2-venus-lifetime.h',
                 'sq2-venus-power.c', 'sq2-venus-power.h'):
        shutil.copyfile(ROOT / 'probe/video' / name, args.output / name)
    with (args.output / 'Makefile').open('a') as f:
        f.write('\nvenus-core-objs += sq2-venus-platform.o sq2-venus-power.o\n')
    core = (src / 'core.h').read_text()
    core = replace(core, 'struct venus_core {',
                   'struct sq2_venus_fw;\nstruct sq2_venus_platform;\nstruct venus_core {\n'
                   '\tstruct sq2_venus_platform *sq2_platform;\n'
                   '\tstruct sq2_venus_fw *sq2_fw_owner;\n'
                   '\tbool sq2_boot_attempted, sq2_secure_active;')
    core = replace(core, '\tenum hfi_version hfi_version;\n',
                   '\t/* VPU hardware is independent of HFI packet version. */\n'
                   '\tbool iris1;\n\tbool sq2_windows_hfi;\n\tenum hfi_version hfi_version;\n')
    core = replace(core, '#define IS_V4(core)',
                   '#define IS_SQ2_HFI(core) ((core)->res->sq2_windows_hfi)\n'
                   '#define IS_IRIS1(core) ((core)->res->iris1)\n#define IS_V4(core)')
    # Keep all pre-existing SoC tables unchanged (zero-initialized iris1=false).
    (args.output / 'core.h').write_text(core)
    platform = (src / 'core.c').read_text()
    platform = replace(platform, '\tmutex_init(&core->pm_lock);',
                       '\t/* PAS callsites are compile-tested; devres quarantine is pending. */\n'
                       '\tif (IS_SQ2_HFI(core))\n\t\treturn -EOPNOTSUPP;\n\n'
                       '\tmutex_init(&core->pm_lock);')
    (args.output / 'core.c').write_text(platform)
    hfi = (src / 'hfi_venus.c').read_text()
    hfi = replace(hfi, '#include <linux/slab.h>',
                  '#include <linux/slab.h>\n#include <linux/iommu.h>\n#include "sq2-venus-dma.h"')
    hfi = replace(hfi, '\tstruct venus_core *core;\n',
                  '\tstruct venus_core *core;\n\tstruct sq2_venus_dma sq2_dma;\n')
    hfi = replace(hfi, '#include "firmware.h"', '#include "firmware.h"\n#include "venus-iris1.h"')
    hfi = replace(hfi, '\tif (IS_V6(hdev->core)) {\n\t\tmask_val = readl(wrapper_base + WRAPPER_INTR_MASK);',
                  '\tif (IS_IRIS1(hdev->core)) {\n'
                  '\t\tmask_val = venus_iris1_irq_mask(readl(wrapper_base + WRAPPER_INTR_MASK));\n'
                  '\t} else if (IS_V6(hdev->core)) {\n\t\tmask_val = readl(wrapper_base + WRAPPER_INTR_MASK);')
    hfi = replace(hfi, '\tvenus_set_registers(hdev);',
                  '\tif (IS_IRIS1(hdev->core))\n\t\tvenus_iris1_clock_config(hdev->core);\n'
                  '\tvenus_set_registers(hdev);')
    hfi = replace(hfi, '\tret = venus_boot_core(hdev);',
                  '\tif (IS_IRIS1(hdev->core) && !IS_SQ2_HFI(hdev->core))\n'
                  '\t\tvenus_iris1_dsp_map(hdev->core, hdev->ifaceq_table.da, SHARED_QSIZE);\n\n'
                  '\tret = venus_boot_core(hdev);')
    # SQ2 has one coherent UC allocation: SFR is a view, not a second owner.
    hfi = replace(hfi, '\tdesc->attrs = DMA_ATTR_WRITE_COMBINE;',
                  '\tdesc->attrs = IS_SQ2_HFI(hdev->core) ? 0 : DMA_ATTR_WRITE_COMBINE;')
    hfi = replace(hfi, '\tvenus_free(hdev, &hdev->sfr);',
                  '\tif (!IS_SQ2_HFI(hdev->core))\n\t\tvenus_free(hdev, &hdev->sfr);')
    hfi = replace(hfi, '\tret = venus_alloc(hdev, &desc, ALIGNED_QUEUE_SIZE);',
                  '\tret = venus_alloc(hdev, &desc, IS_SQ2_HFI(hdev->core) ?\n'
                  '\t\tSQ2_VENUS_UC_BYTES : ALIGNED_QUEUE_SIZE);')
    hfi = replace(hfi, '\thdev->ifaceq_table = desc;',
                  '\tif (IS_SQ2_HFI(hdev->core)) {\n'
                  '\t\tret = sq2_venus_dma_check_uc(hdev->core->dev, desc.da, desc.size);\n'
                  '\t\tif (ret) {\n'
                  '\t\t\tvenus_free(hdev, &desc);\n\t\t\treturn ret;\n\t\t}\n'
                  '\t\tmemset(desc.kva, 0, desc.size);\n\t}\n'
                  '\thdev->ifaceq_table = desc;')
    hfi = replace(hfi, '\tret = venus_alloc(hdev, &desc, ALIGNED_SFR_SIZE);',
                  '\tif (IS_SQ2_HFI(hdev->core)) {\n'
                  '\t\tdesc = hdev->ifaceq_table;\n'
                  '\t\tdesc.da += ALIGNED_QUEUE_SIZE;\n'
                  '\t\tdesc.kva += ALIGNED_QUEUE_SIZE;\n'
                  '\t\tdesc.size = ALIGNED_SFR_SIZE;\n\t\tret = 0;\n'
                  '\t} else {\n\t\tret = venus_alloc(hdev, &desc, ALIGNED_SFR_SIZE);\n\t}')
    start = '\twritel(hdev->ifaceq_table.da, cpu_cs_base + UC_REGION_ADDR);'
    end = '\t\twritel(hdev->sfr.da, cpu_cs_base + SFR_ADDR);'
    legacy = hfi[hfi.index(start):hfi.index(end) + len(end)]
    hfi = replace(hfi, legacy,
                  '\tif (IS_SQ2_HFI(hdev->core)) {\n'
                  '\t\tvenus_sq2_cpu_map(hdev->core, hdev->ifaceq_table.da, hdev->sfr.da);\n'
                  '\t} else {\n' + legacy + '\n\t}')
    hfi = replace(hfi, '\twritel(1, cpu_cs_base + CPU_CS_SCIACMDARG3);',
                  '\tif (!IS_SQ2_HFI(hdev->core))\n'
                  '\t\twritel(1, cpu_cs_base + CPU_CS_SCIACMDARG3);')
    hfi = replace(hfi, '\thdev = kzalloc(sizeof(*hdev), GFP_KERNEL);',
                  '\t/* No implicit activation on existing HFI4 platforms. */\n'
                  '\tif (IS_SQ2_HFI(core) && (!IS_IRIS1(core) || !IS_V4(core)))\n'
                  '\t\treturn -EINVAL;\n\n'
                  '\thdev = kzalloc(sizeof(*hdev), GFP_KERNEL);')
    hfi = replace(hfi, '\tret = venus_interface_queues_init(hdev);',
                  '\tif (IS_SQ2_HFI(core)) {\n'
                  '\t\tret = sq2_venus_dma_begin(core->dev, &hdev->sq2_dma);\n'
                  '\t\tif (ret)\n\t\t\tgoto err_kfree;\n\t}\n'
                  '\tret = venus_interface_queues_init(hdev);')
    hfi = replace(hfi, 'err_kfree:\n\tkfree(hdev);',
                  'err_kfree:\n\tsq2_venus_dma_restore(core->dev, &hdev->sq2_dma);\n'
                  '\tmutex_destroy(&hdev->lock);\n\tkfree(hdev);')
    hfi = replace(hfi, '\tvenus_interface_queues_release(hdev);',
                  '\tvenus_interface_queues_release(hdev);\n'
                  '\tsq2_venus_dma_restore(core->dev, &hdev->sq2_dma);')
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
        fw = replace(fw, '#include "venus-iris1.h"',
                     '#include "venus-iris1.h"\n#include "sq2-venus-fw.h"\n'
                     '#include "sq2-venus-platform.h"\n'
                     '#include "venus-sq2-firmware.h"')
        fw = replace(fw, '\tif (!IS_ENABLED(CONFIG_QCOM_MDT_LOADER) ||',
                     '\tif (IS_SQ2_HFI(core))\n\t\treturn venus_sq2_boot(core);\n\n'
                     '\tif (!IS_ENABLED(CONFIG_QCOM_MDT_LOADER) ||')
        fw = replace(fw, 'int venus_shutdown(struct venus_core *core)\n{\n\tint ret;',
                     'int venus_shutdown(struct venus_core *core)\n{\n\tint ret;\n\n'
                     '\tif (IS_SQ2_HFI(core))\n\t\treturn venus_sq2_shutdown(core);')
        (args.output / 'firmware.c').write_text(fw)
        for name in ('sq2-fw-transaction.h', 'sq2-venus-fw.h', 'sq2-venus-fw.c',
                     'sq2_video_fw_layout.h', 'sq2_pil_mem_query.c', 'sq2-pil-region.h',
                     'venus-sq2-firmware.h'):
            shutil.copyfile(ROOT / 'probe/video' / name, args.output / name)
        with (args.output / 'Makefile').open('a') as f:
            f.write('\nvenus-core-objs += sq2-venus-fw.o sq2_pil_mem_query.o\n'
                    'ccflags-y += -DSQ2_PIL_QUERY_LIBRARY -I$(srctree)/drivers/firmware\n')
    manifest = {'status': 'compile-only-no-platform-match-no-hardware-validation',
                'sq2_transport': 'explicit inactive flag; coherent 16MiB UC; bounded IOVA; SFR view; CPU-only mailboxes; no VERSION write',
                'remaining': ['core probe/recovery/remove dispatch and runtime PM adaptation',
                              'V4L2 buffer address validation and runtime', 'Android Codec2'],
                'sq2_dma': 'exact 2300/2320/2340 SID + singleton DMA group; saved/restored 32bit mask and bus limit; UC alignment and every-page translation checked',
                'firmware_lease_dispatch_compiled': args.firmware_lease,
                'sq2_platform_probe_blocked': True,
                'platform_owner': 'compiled independent core/MMIO/IRQ/power/reset/ICC owner; firmware begin/end hooks connected; platform probe/recovery/remove pending',
                'input_sha256': HASHES,
                'output_sha256': {f.name: hashlib.sha256(f.read_bytes()).hexdigest()
                                  for f in sorted(args.output.iterdir()) if f.is_file()}}
    (args.output / 'candidate.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(args.output)


if __name__ == '__main__':
    main()
