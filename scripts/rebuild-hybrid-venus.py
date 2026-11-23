#!/usr/bin/env python3
"""Rebuild only Venus against the exact kernel of an explicit reuse manifest."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from full_image_reuse import digest, inventory, load
from full_image_video import ensure_venus_source_hooks
from full_image_suspend import validate_module


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('reuse-components','kernel-build','source','output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--cross-compile', default='aarch64-linux-gnu-')
    p.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 1))
    a = p.parse_args()
    roots, _, _, parent = load(a.reuse_components)
    kout = a.kernel_build.resolve()
    pairs = {'arch/arm64/boot/Image':'Image', '.config':'kernel.config',
             'Module.symvers':'Module.symvers', 'System.map':'System.map'}
    before = {n:digest(kout/n) for n in pairs}
    if any(before[n] != digest(roots['kernel']/export) for n,export in pairs.items()):
        raise ValueError('Kernel build does not match reused Image/config/symbols/System.map')
    if a.jobs < 1: raise ValueError('jobs must be positive')
    a.output = a.output.resolve(); a.output.mkdir(parents=True,exist_ok=False)
    source = a.source.resolve()
    source_hashes = {str(p.relative_to(source)):digest(p) for p in source.rglob('*') if p.is_file()}
    report = {'parent_manifest':str(a.reuse_components.resolve()), 'parent_manifest_sha256':digest(a.reuse_components),
              'kernel_build':str(kout), 'kernel_identity':before, 'source':str(source),
              'source_sha256':source_hashes, 'commands':[], 'device_written':False}
    def save(): (a.output/'VENUS-REBUILD.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(label, command):
        command = list(map(str,command))
        with (a.output/(label+'.stdout')).open('wb') as out,(a.output/(label+'.stderr')).open('wb') as err:
            x = subprocess.run(command,stdout=out,stderr=err)
        report['commands'].append({'label':label,'command':command,'exit':x.returncode,
                                   'stdout':str(a.output/(label+'.stdout')),'stderr':str(a.output/(label+'.stderr'))})
        save()
        if x.returncode: raise RuntimeError('Venus rebuild failed: '+label+'; see VENUS-REBUILD.json')
    work = a.output/'venus-source'; shutil.copytree(source,work)
    command = ['make','-C',kout,'ARCH=arm64','CROSS_COMPILE='+a.cross_compile,'M='+str(work)]
    run('clean',command+['clean'])
    report['source_patch'] = ensure_venus_source_hooks(work); save()
    run('modules',command+['-j'+str(a.jobs),'modules'])
    validate_module((work/'venus-core.ko').read_bytes())
    # Retain every unaffected staged module and the original kernel bytes.
    export = a.output/'kernel-export'; export.mkdir()
    for name in (*pairs.values(),'build-audit.json'):
        shutil.copyfile(roots['kernel']/name,export/name)
    shutil.copytree(roots['kernel']/'modules-stage',export/'modules-stage')
    changed = {}
    for module in ('venus-core.ko','venus-dec.ko','venus-enc.ko'):
        matches = list((export/'modules-stage').rglob(module))
        if len(matches) != 1: raise ValueError('Expected one staged '+module)
        target = matches[0]
        old = digest(target); shutil.copyfile(work/module,target)
        changed[str(target.relative_to(export))] = {'before':old,'after':digest(target)}
    audit = json.loads((export/'build-audit.json').read_text())
    run('depmod',['depmod','-b',export/'modules-stage','-F',kout/'System.map',audit['kernel_release']])
    if before != {n:digest(kout/n) for n in pairs}: raise ValueError('Kernel build identity changed')
    if source_hashes != {str(p.relative_to(source)):digest(p) for p in source.rglob('*') if p.is_file()}:
        raise ValueError('Original Venus input changed')
    report.update(modules=changed, original_source_unchanged=True, kernel_unchanged=True)
    save()
    audit['parent_export'] = str(roots['kernel'])
    audit['incremental_venus_rebuild'] = report
    audit['commands'] += report['commands']
    audit['files'] = {}  # New manifest below hashes all consumed files.
    for entry in audit.get('external_modules',[]):
        if Path(entry['module']).name in ('venus-core.ko','venus-dec.ko','venus-enc.ko'):
            entry['sha256'] = digest(next((export/'modules-stage').rglob(Path(entry['module']).name)))
    (export/'build-audit.json').write_text(json.dumps(audit,indent=2)+'\n')
    roots['kernel'] = export
    data = dict(parent, roots={k:str(v) for k,v in roots.items()}, sha256=inventory(roots))
    manifest = a.output/'REUSE.json'; manifest.write_text(json.dumps(data,indent=2)+'\n')
    load(manifest)
    print(manifest)


if __name__ == '__main__': main()
