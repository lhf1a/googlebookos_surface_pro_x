#!/usr/bin/env python3
"""Execute actual SDE PRIME lifecycle bodies with explicit DMA/lock models."""
import argparse, hashlib, json, os, re, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]


def extract(text, name):
    pat = r'^(?:static )?(?:inline )?(?:struct [a-z_]+ \*|void \*|void|int|dma_addr_t|uint64_t|vm_fault_t|bool|unsigned long)\s*\n?' + name + r'\([^;{]*\)\n\{.*?^\}'
    matches = list(re.finditer(pat, text, re.M | re.S))
    if len(matches) != 1:
        raise ValueError((name, len(matches)))
    return matches[0].group()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', required=True, type=Path)
    p.add_argument('--kernel-source', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--legacy-model', action='store_true', help='adapt only old removed vmap signatures to explicit legacy leaves')
    a = p.parse_args(); a.output = a.output.absolute(); a.output.mkdir(parents=True, exist_ok=False)
    prime = a.source / 'msm_gem_prime.c'; gem = a.source / 'msm_gem.c'; vma = a.source / 'msm_gem_vma.c'
    core = a.kernel_source / 'drivers/gpu/drm/drm_prime.c'
    gem_core = a.kernel_source / 'drivers/gpu/drm/drm_gem.c'
    functions = []
    order = [(core, ['drm_gem_is_prime_exported_dma_buf']),
             (gem, ['msm_gem_new_impl', 'msm_gem_import', 'msm_gem_delayed_import', 'add_vma', 'lookup_vma', 'del_vma']),
             (vma, ['msm_gem_address_space_destroy', 'msm_gem_address_space_put', 'smmu_aspace_map_vma', 'smmu_aspace_unmap_vma']),
             (gem, ['put_iova', 'msm_gem_get_iova_locked', 'msm_gem_pin_iova', 'msm_gem_get_and_pin_iova', 'msm_gem_vunmap_locked', 'get_vaddr', 'msm_gem_put_vaddr', 'msm_gem_free_object']),
             (prime, ['msm_gem_prime_import_sg_table']),
             (core, ['drm_gem_prime_import_dev', 'drm_prime_gem_destroy']),
             (prime, ['msm_gem_prime_import'])]
    if not a.legacy_model:
        order += [(prime, ['msm_gem_prime_vmap', 'msm_gem_prime_vunmap']),
                  (gem_core, ['drm_gem_vmap_locked', 'drm_gem_vunmap_locked', 'drm_gem_vmap', 'drm_gem_vunmap']),
                  (core, ['drm_gem_prime_handle_to_dmabuf'])]
    for path, names in order:
        for n in names:
            body = extract(path.read_text(), n)
            if a.legacy_model:
                body = body.replace('dma_buf_vmap(obj->import_attach->dmabuf)', 'legacy_dma_buf_vmap(obj->import_attach->dmabuf)')
                body = body.replace('dma_buf_vunmap(obj->import_attach->dmabuf, msm_obj->vaddr)', 'legacy_dma_buf_vunmap(obj->import_attach->dmabuf, msm_obj->vaddr)')
            functions.append(body)
    (a.output / 'actual-functions.h').write_text('\n\n'.join(functions) + '\n')
    macros = []
    for name in ['MSM_BO_STOLEN', 'MSM_BO_KEEPATTRS', 'MSM_BO_SKIPSYNC', 'MSM_BO_EXTBUF']:
        macros.append(re.search(r'^#define ' + name + r'\s+.*$', (a.source / 'msm_gem.h').read_text(), re.M).group())
    (a.output / 'actual-definitions.h').write_text('\n'.join(macros) + '\n')
    if a.legacy_model:
        with (a.output / 'actual-definitions.h').open('a') as f:
            f.write('#define LEGACY_MODEL 1\n')
    runs=[]
    commands=[('compile',['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function',
              '-fsanitize=address,undefined','-fno-pie','-no-pie','-I',str(a.output),str(ROOT/'tests/sde_prime_test.c'),'-o',str(a.output/'check')]),('run',[str(a.output/'check')])]
    for label,cmd in commands:
        r=subprocess.run(cmd,capture_output=True,text=True,timeout=30,env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'})
        runs.append(dict(label=label,command=cmd,stdout=r.stdout,stderr=r.stderr,exit_status=r.returncode))
        (a.output/(label+'.stdout.txt')).write_text(r.stdout);(a.output/(label+'.stderr.txt')).write_text(r.stderr)
        if r.returncode:break
    files=[prime,gem,vma,core,gem_core,a.source/'msm_gem.h',Path(__file__),ROOT/'tests/sde_prime_test.c']
    result=dict(status='pass' if runs[-1]['exit_status']==0 else 'fail',legacy_model=a.legacy_model,
                inputs={str(q.resolve()):hashlib.sha256(q.read_bytes()).hexdigest() for q in files},runs=runs,
                functions=[n for _,names in order for n in names],hardware_validated=False)
    (a.output/'RESULT.json').write_text(json.dumps(result,indent=2)+'\n');print(runs[-1]['stdout'],end='');print('RESULT',a.output/'RESULT.json',result['status'])
    return 0 if result['status']=='pass' else 1


if __name__=='__main__':raise SystemExit(main())
