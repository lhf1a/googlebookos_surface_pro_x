#!/usr/bin/env python3
"""Materialize current 6.18 source recipe from the preserved Nov7 input sources.

Works only on a fresh copy; no fallback to stale prebuilt modules on failure.
The known candidate3 source input is required, not inferred from a version string.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

from kernel_baseline import ROOT
from disk_image import module, require


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-archive',type=Path,required=True)
    p.add_argument('--venus-source',type=Path,required=True)
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--resume-after-layout',action='store_true',help='resume this preparation after capture layout, before release cleanup')
    p.add_argument('--private-root',type=Path,default=ROOT/'private/build-inputs')
    a=p.parse_args()
    require(not a.work.exists() or a.resume_after_layout,'Preparation work directory must be fresh')
    target=a.private_root/'kernel-6.18/source'
    require(not target.exists(),'Fixed source input already exists; do not overwrite it silently')
    if not a.resume_after_layout:
        a.work.mkdir(parents=True)
        with tarfile.open(a.source_archive) as archive:
            archive.extractall(a.work,filter='data')
    kernel=a.work/'kernel'; external=a.work/'external'
    # Preserve the historical module source rather than destructively replace it.
    venus=external/'venus'
    if not a.resume_after_layout:
        venus.rename(a.work/'historical-venus');venus.mkdir()
        for path in a.venus_source.iterdir():
            if path.suffix in ('.c','.h') or path.name in ('Makefile','Kconfig'):
                shutil.copyfile(path,venus/path.name)
    steps=[]
    def invoke(filename,function,*args):
        print('APPLY',filename,function,flush=True)
        getattr(module(filename.replace('-','_'),filename+'.py'),function)(*args)
        steps.append(filename+':'+function)
    if not a.resume_after_layout:
        invoke('patch-qcom-soundwire-ahb','apply',kernel)
        if 'struct mutex ctx_q_lock;' not in (venus/'core.h').read_text():
            invoke('prepare-sq2-lts-venus-queue-lock','patch',venus)
        invoke('patch-sq2-venus-omx','apply',venus,ROOT)
        invoke('patch-sq2-venus-avc-caps','apply',venus)
        invoke('patch-sq2-venus-extradata','apply',venus,ROOT)
        invoke('patch-sq2-venus-capture-layout','apply',venus)
    else:
        require('sq2_capture_stride' in (venus/'core.h').read_text(),'Not a capture-layout preparation')
        steps.append('resume-after-layout:see-preserved-first-attempt-log')
    # Current reusable protocol helpers contain END confirmation, DMA retirement,
    # capture ownership and bounds fixes. The core/decoder are compiled together.
    for path in (ROOT/'probe/video').iterdir():
        if path.suffix=='.h' and (venus/path.name).is_file():
            shutil.copyfile(path,venus/path.name)
    invoke('full_image_video','ensure_venus_source_hooks',venus)
    # Remove the remaining opt-in boot/stage diagnostics from this source form.
    # The current shared PM header is already release-clean.
    patch=(ROOT/'patches/venus-release-diagnostics.patch').read_text()
    patch=patch.split('--- a/venus-sq2-system-pm.h')[0]
    if '#include "venus-sq2-boot-watch.h"' in (venus/'hfi_venus.c').read_text():
        subprocess.run(['patch','--batch','--fuzz=0','-p1'],cwd=venus,input=patch,text=True,check=True)
    else:
        require(a.resume_after_layout and 'sq2_stage_trace' not in (venus/'vdec.c').read_text(),
                'Unknown release cleanup state')
    steps.append('venus-release-diagnostics:boot-and-stage')
    hfi=venus/'hfi_venus.c';text=hfi.read_text()
    # The preserved bootstatus candidate spells the same conjunction in the
    # reverse order. Normalize only that exact known form for the shared patch.
    old='\tif (count >= max_tries && !ctrl_status)\n\t\tret = -ETIMEDOUT;'
    if text.count(old)==1:
        hfi.write_text(text.replace(old,'\tif (!ctrl_status && count >= max_tries)\n\t\tret = -ETIMEDOUT;'))
    invoke('patch-sq2-venus-boot-status','apply',venus)
    target.mkdir(parents=True)
    with tarfile.open(target/'source.tar.gz','w:gz',compresslevel=1) as archive:
        archive.add(kernel,arcname='kernel')
        archive.add(external,arcname='external')
    report={'schema':1,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
            'current_release_patches':True,'steps':steps,'archive':'source.tar.gz',
            'kernel_source_compiled':False,'hardware_tested':False,
            'source_input_archive':str(a.source_archive.resolve()),'venus_input':str(a.venus_source.resolve())}
    (target/'SOURCE.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2),flush=True)


if __name__=='__main__':
    main()
