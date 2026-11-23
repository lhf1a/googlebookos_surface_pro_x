#!/usr/bin/env python3
"""Build RPC/PAS12 EFI and collect already-built sensor modules/HAL/overlay.

OEM inputs come from the normalized Windows export (surface-assets); no device
access, registry export, or implicit online donor fallback occurs here.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from hybrid_bootstrap import ROOT, digest


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('surface-assets','kernel-components','platform-components','sources-root','venus-firmware','output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--reuse-rpc', type=Path, help='explicit previously compiled static ARM64 hexagonrpcd')
    p.add_argument('--reuse-efi', type=Path, help='explicit previously compiled PAS12 EFI')
    p.add_argument('--cross-compile', default='aarch64-linux-gnu-')
    p.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 1))
    a = p.parse_args()
    a.output = a.output.resolve(); a.output.mkdir(parents=True, exist_ok=False)
    report = {'commands':[], 'reused':{}, 'device_accessed':False}
    def run(command):
        command = list(map(str, command))
        x = subprocess.run(command, capture_output=True, text=True)
        report['commands'].append(dict(command=command, exit=x.returncode, stdout=x.stdout, stderr=x.stderr))
        (a.output/'BUILD.json').write_text(json.dumps(report,indent=2)+'\n')
        if x.returncode: raise RuntimeError('Command failed: '+str(command)+'; see BUILD.json')
    rpc = a.output/'hexagonrpcd'
    if a.reuse_rpc:
        shutil.copyfile(a.reuse_rpc, rpc)
        report['reused']['rpc'] = {'path':str(a.reuse_rpc.resolve()), 'sha256':digest(rpc)}
    else:
        source = a.sources_root/'hexagonrpc'
        pins = json.loads((ROOT/'configs/hybrid-dependencies.json').read_text())['repositories']
        expected = next(x['commit'] for x in pins if x['id']=='hexagonrpc')
        if subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip() != expected:
            raise ValueError('Wrong hexagonrpc source revision')
        files = []
        for directory in ('libhexagonrpc','hexagonrpcd'):
            files += [source/directory/n for n in re.findall(r"'([^']+\.c)'", (source/directory/'meson.build').read_text())]
        if not files: raise ValueError('No RPC source files')
        run([a.cross_compile+'gcc','-O2','-static','-Wall','-Wextra','-Wno-unused-parameter',
             '-I'+str(source/'include'),*files,'-lpthread','-o',rpc])
    efi = a.reuse_efi
    if efi:
        report['reused']['efi'] = {'path':str(efi.resolve()), 'sha256':digest(efi)}
    else:
        parent, child = a.output/'efi-parent', a.output/'efi-slpi'
        run([sys.executable,ROOT/'scripts/prepare-sq2-el2-preboot.py','--output',parent,
             '--source',a.sources_root/'qebspil','--gnu-efi-source',a.sources_root/'qebspil-gnu-efi',
             '--dtc-source',a.sources_root/'qebspil-dtc','--venus-firmware',a.venus_firmware])
        run([sys.executable,ROOT/'scripts/prepare-spx-slpi-efi.py',parent,child])
        run(['make','-C',child,'CROSS_COMPILE='+a.cross_compile,'-j'+str(a.jobs)])
        efi = child/'out/qebspilaa64.efi'
    modules = a.output/'modules'; modules.mkdir()
    for sub, name in (('sensor-als','spx-tcs3430'),('sensor-slpi','spx-slpi-pas'),('sensor-slpi','fastrpc')):
        shutil.copyfile(a.kernel_components/sub/(name+'.ko'), modules/(name+'.ko'))
    platform = a.platform_components/'platform'
    run([sys.executable,ROOT/'scripts/build-spx-sensor-bundle.py','--modules',modules,
         '--hal',platform/'sensor-hal/sensors.spx.so','--overlay',platform/'sensor-overlay/spx-sensors-overlay.apk',
         '--rpc',rpc,'--efi',efi,'--config',a.surface_assets/'sensors/config',
         '--calibration',a.surface_assets/'sensors/spx-tcs3430-golden.bin',
         '--firmware',a.surface_assets/'firmware/qcom/msft/surface/pro-x/qcslpi8180.mbn',
         '--output',a.output/'sensors'])
    report['bundle'] = str(a.output/'sensors')
    (a.output/'BUILD.json').write_text(json.dumps(report,indent=2)+'\n')
    print(a.output/'sensors')


if __name__ == '__main__':
    main()
