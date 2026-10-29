#!/usr/bin/env python3
"""Build an explicitly selected, isolated DMA reference; never deploy or power VPU."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--stage', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--smmu-phandle', type=lambda value: int(value, 0), required=True)
p.add_argument('--profile', choices=['legacy-1300', 'windows-2300'], required=True)
a = p.parse_args()
if not 0 < a.smmu_phandle < 2**32:
    p.error('SMMU phandle must be a nonzero u32 from the current live tree')
if a.output.exists() or a.stage.exists():
    p.error('Stage/output must both be new directories')
a.stage.mkdir(parents=True)
a.output.mkdir(parents=True)
ids = [0x00601300] if a.profile == 'legacy-1300' else [0x2300, 0x2320, 0x2340]
cells = [(0x1300, 0x60)] if a.profile == 'legacy-1300' else [(i, 0) for i in ids]
iommus = ', '.join(f'<0x{a.smmu_phandle:x} 0x{sid:x} 0x{mask:x}>' for sid, mask in cells)
dts = f'''/dts-v1/;
/plugin/;
/ {{ fragment@0 {{ target-path = "/soc@0";
 __overlay__ {{ sq2-video-dma-reference {{
  compatible = "microsoft,sq2-video-dma-reference";
  iommus = {iommus};
  status = "okay";
 }}; }};
}}; }};
'''
(a.stage/'overlay.dts').write_text(dts)
subprocess.run(['dtc', '-I', 'dts', '-O', 'dtb', '-o', str(a.stage/'overlay.dtbo'),
                str(a.stage/'overlay.dts')], check=True, stderr=(a.output/'dtc.log').open('w'))
blob = (a.stage/'overlay.dtbo').read_bytes()
(a.stage/'reference-overlay.h').write_text(
    f'#define REFERENCE_PROFILE "{a.profile}"\n'
    f'#define REFERENCE_IDS {", ".join(hex(x) for x in ids)}\n'
    f'#define REFERENCE_SMMU_PHANDLE 0x{a.smmu_phandle:x}\n'
    'static const unsigned char reference_overlay[] = {' + ','.join(map(str, blob)) + '};\n')
source = ROOT/'probe/video/sq2_video_dma_reference.c'
(a.stage/'sq2_video_dma_ref.c').write_bytes(source.read_bytes())
(a.stage/'Makefile').write_text('obj-m += sq2_video_dma_ref.o\n')
subprocess.run(['make', '-s', '-C', '/root/.cache/sq2-thermal/kernel',
    'O=/root/.cache/sq2-video-foundation/build', 'ARCH=arm64', 'CROSS_COMPILE=aarch64-linux-gnu-',
    f'M={a.stage}', 'KBUILD_MODPOST_WARN=1', 'modules'], check=True,
    stdout=(a.output/'build.log').open('w'), stderr=subprocess.STDOUT)
module = (a.stage/'sq2_video_dma_ref.ko').read_bytes()
(a.output/'sq2_video_dma_ref.ko').write_bytes(module)
(a.output/'overlay.dtbo').write_bytes(blob)
(a.output/'candidate.json').write_text(json.dumps({
    'status': 'build-only-NO-POWER-NO-MMIO-NO-FIRMWARE', 'profile': a.profile,
    'smmu_phandle': hex(a.smmu_phandle), 'encoded_ids': [hex(i) for i in ids],
    'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'module_sha256': hashlib.sha256(module).hexdigest(), 'bytes': len(module),
    'allocation_bytes': 16*1024*1024, 'iova_end_exclusive': '0xc0000000',
    'limit': 'Only host mapping/CPU readback/unmapping; no proof of VPU DMA or correct SQ2 SID',
}, indent=2)+'\n')
print(a.profile, hashlib.sha256(module).hexdigest())
