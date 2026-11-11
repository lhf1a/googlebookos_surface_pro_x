#!/usr/bin/env python3
"""Run actual SDE private/GEM/PRIME/close source bodies with managed-core models.

Each scenario runs in an isolated child so baseline sanitizer failures do not
hide later cases. pthread locks/admission and reference/failure accounting are
real host execution, not proof of kernel scheduling or hardware retirement.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,re,subprocess

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('sde_prime_extract',ROOT/'scripts/check-sde-prime.py')
extractor=importlib.util.module_from_spec(spec);spec.loader.exec_module(extractor)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',required=True,type=Path)
    parser.add_argument('--kernel-source',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=False)
    source=args.source.resolve()
    if (source/'techpack/display/msm').is_dir():source=source/'techpack/display/msm'
    gem,prime,drv,vma=(source/n for n in ['msm_gem.c','msm_gem_prime.c','msm_drv.c','msm_gem_vma.c'])
    core=args.kernel_source/'drivers/gpu/drm/drm_prime.c'
    native='msm_drm_private_alloc' in drv.read_text()
    order=[]
    if native:order.append((drv,['msm_drm_owner_get_live','msm_drm_owner_put_live','msm_drm_owner_retire','msm_drm_private_release','msm_drm_private_alloc']))
    order.append((drv,['msm_drm_device_init']))
    order.append((core,['drm_gem_is_prime_exported_dma_buf']))
    if native:order.append((gem,['msm_gem_publish']))
    order.append((gem,['msm_gem_new_impl','physaddr','put_pages_vram','add_vma','lookup_vma','del_vma']))
    order.append((vma,['msm_gem_address_space_destroy','msm_gem_address_space_put','smmu_aspace_map_vma','smmu_aspace_unmap_vma']))
    order.append((gem,['put_iova']))
    if native:order.append((gem,['msm_gem_get_iova_locked_active']))
    order.append((gem,['msm_gem_get_iova_locked']))
    if native:order.append((gem,['msm_gem_pin_iova_active']))
    order.append((gem,['msm_gem_pin_iova','msm_gem_get_and_pin_iova','msm_gem_vunmap_locked','get_vaddr','msm_gem_put_vaddr','msm_gem_free_object']))
    if native:order.append((gem,['_msm_gem_new_active']))
    order.append((gem,['_msm_gem_new']))
    if native:order.append((gem,['msm_gem_import_active']))
    order.append((gem,['msm_gem_import','msm_gem_delayed_import']))
    order.append((prime,['msm_gem_prime_import_sg_table','msm_gem_prime_vmap','msm_gem_prime_vunmap']))
    order.append((core,['drm_gem_prime_import_dev','drm_prime_gem_destroy']))
    if native:order.append((prime,['msm_gem_prime_import_active']))
    order.append((prime,['msm_gem_prime_import']))
    order.append((drv,['context_init','context_close','msm_preclose','msm_postclose','msm_ioctl_power_ctrl','msm_drm_uninit']))
    bodies=[extractor.extract(path.read_text(),name) for path,names in order for name in names]
    (args.output/'actual-functions.h').write_text('\n\n'.join(bodies)+'\n')
    uapi=source.parents[2]/'include/uapi/drm/msm_drm.h'
    macros=[]
    for path,names in [(source/'msm_gem.h',['MSM_BO_STOLEN','MSM_BO_KEEPATTRS','MSM_BO_SKIPSYNC','MSM_BO_EXTBUF']),
                       (uapi,['MSM_BO_WC','MSM_BO_UNCACHED','MSM_BO_CACHED','MSM_BO_CACHE_MASK','MSM_BO_SCANOUT'])]:
        for name in names:
            matches=re.findall(r'^#define\s+'+name+r'\s+[^\n]+',path.read_text(),re.M)
            if len(matches)!=1:raise ValueError((path,name,matches))
            macros+=matches
    if native:macros+=['#define OWNER_MODEL 1']
    (args.output/'actual-definitions.h').write_text('\n'.join(macros)+'\n')
    runs=[]
    for label,command in [('compile',['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-but-set-variable','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-pie','-no-pie','-pthread','-I',str(args.output),str(ROOT/'tests/sde_owner_lifetime_test.c'),'-o',str(args.output/'check')]),('run',[str(args.output/'check')])]:
        p=subprocess.run(command,capture_output=True,text=True,timeout=120,env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1','ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1'})
        runs.append(dict(label=label,command=command,stdout=p.stdout,stderr=p.stderr,exit=p.returncode))
        (args.output/(label+'.stdout.txt')).write_text(p.stdout);(args.output/(label+'.stderr.txt')).write_text(p.stderr)
        if p.returncode:break
    files=[gem,prime,drv,vma,core,uapi,source/'msm_drv.h',source/'msm_gem.h',Path(__file__),ROOT/'scripts/check-sde-prime.py',ROOT/'tests/sde_owner_lifetime_test.c',ROOT/'tests/sde_owner_lifetime_support.h',ROOT/'tests/sde_owner_lifetime_cases.h']
    result=dict(status='pass' if runs[-1]['exit']==0 else 'fail',owner_model=native,
                inputs={str(p.resolve()):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
                runs=runs,functions=[n for _,names in order for n in names],hardware_validated=False,
                boundaries='Managed core/resource/DMA leaves modeled; actual owner, GEM/PRIME/close/uninit bodies and pthread execution. Not full safe unbind, all atomic/ioctl admission, kernel scheduling or physical retirement proof.')
    (args.output/'RESULT.json').write_text(json.dumps(result,indent=2)+'\n')
    print(runs[-1]['stdout'],end='')
    if runs[-1]['label']=='compile':print(runs[-1]['stderr'],end='')
    print(args.output/'RESULT.json',result['status'])
    return 0 if result['status']=='pass' else 1


if __name__=='__main__':raise SystemExit(main())
