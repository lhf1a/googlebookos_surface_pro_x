#!/usr/bin/env python3
"""Run current DRM fence collection and actual SDE property/prepare callers."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, subprocess
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('owner_check',ROOT/'scripts/check-sde-prime.py');owner=importlib.util.module_from_spec(spec);spec.loader.exec_module(owner)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',required=True,type=Path);p.add_argument('--kernel-source',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--core-chain-failure',action='store_true');p.add_argument('--core',type=Path,help='explicit isolated core candidate, never changes paired kernel');a=p.parse_args();a.output=a.output.absolute();a.output.mkdir(parents=True,exist_ok=False)
    core=a.core or a.kernel_source/'drivers/gpu/drm/drm_gem_atomic_helper.c';plane=a.source/'sde/sde_plane.c'
    names=['_sde_plane_get_aspace','_sde_plane_validate_fb','sde_plane_prepare_fb']
    bodies=[owner.extract(core.read_text(),'drm_gem_plane_helper_prepare_fb')]+[owner.extract(plane.read_text(),n) for n in names]
    (a.output/'actual-functions.h').write_text('\n\n'.join(bodies)+'\n')
    runs=[]
    for label,cmd in [('compile',['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=address,undefined','-fno-pie','-no-pie','-I',str(a.output),str(ROOT/'tests/sde_prime_fence_test.c'),'-o',str(a.output/'check')]),('run',[str(a.output/'check')]+(['chain-failure'] if a.core_chain_failure else []))]:
        r=subprocess.run(cmd,capture_output=True,text=True,timeout=30,env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'});runs.append(dict(label=label,command=cmd,stdout=r.stdout,stderr=r.stderr,exit_status=r.returncode));(a.output/(label+'.stdout.txt')).write_text(r.stdout);(a.output/(label+'.stderr.txt')).write_text(r.stderr)
        if r.returncode:break
    files=[core,plane,Path(__file__),ROOT/'tests/sde_prime_fence_test.c'];result=dict(status='pass' if runs[-1]['exit_status']==0 else 'fail',inputs={str(q.resolve()):hashlib.sha256(q.read_bytes()).hexdigest() for q in files},runs=runs,functions=['drm_gem_plane_helper_prepare_fb']+names,core_chain_failure=a.core_chain_failure,hardware_validated=False)
    (a.output/'RESULT.json').write_text(json.dumps(result,indent=2)+'\n');print(runs[-1]['stdout'],end='');print('RESULT',a.output/'RESULT.json',result['status']);return 0 if result['status']=='pass' else 1


if __name__=='__main__':raise SystemExit(main())
