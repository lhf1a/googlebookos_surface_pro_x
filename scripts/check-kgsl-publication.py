#!/usr/bin/env python3
"""Check actual KGSL map publication, both IB validators and command-held backing."""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def extract(s,n):
    pattern=r"^(?:static )?(?:inline )?(?:u64|int|long|void|bool|unsigned long|struct kgsl_mem_entry \*\s*__must_check)\s*"+re.escape(n)+r"\([^;{]*\)\n\{.*?^\}"
    matches=re.findall(pattern,s,re.M|re.S)
    if len(matches)!=1:raise ValueError('Expected function '+n)
    return matches[0]
def check(source,output):
    output.mkdir(parents=True,exist_ok=False);inputs=[];functions=[]
    for filename,names in [('kgsl.h',['kgsl_mem_entry_gpuaddr']),('kgsl.c',['kgsl_mem_entry_unmap','kgsl_mem_entry_detach_process','kgsl_mem_entry_destroy','_gpu_set_svm_region','kgsl_sharedmem_find','kgsl_sharedmem_find_id_flags','kgsl_sharedmem_find_id','kgsl_ioctl_gpumem_get_info','kgsl_ioctl_gpuobj_info']),('kgsl_drawobj.c',['kgsl_drawobj_pin_ib','cmdobj_destroy','kgsl_drawobj_add_memobject'])]:
        f=source/filename;inputs.append(f);functions += [extract(f.read_text(),n) for n in names]
    for filename,newname in [('adreno_dispatch.c','verify_dispatch_ib'),('adreno_hwsched.c','verify_hwsched_ib')]:
        f=source/filename;inputs.append(f);functions.append(extract(f.read_text(),'_verify_ib').replace('_verify_ib',newname))
        body=extract(f.read_text(),'_verify_cmdobj');assert '_verify_ib(dev_priv,' in body
    code=(source/'kgsl_drawobj.c').read_text();assert code.count('kmem_cache_zalloc(memobjs_cache, GFP_KERNEL)')==2
    profile=extract(code,'add_profiling_buffer');assert 'entry && !kgsl_mem_entry_gpuaddr(entry)' in profile
    header=output/'kgsl-publication-functions.h';header.write_text('\n\n'.join(functions)+'\n')
    harness=ROOT/'tests/kgsl_publication_test.c';cmd=['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-fsanitize=address,undefined','-pthread','-fno-pie','-no-pie','-I',str(output),str(harness),'-o',str(output/'check')]
    subprocess.run(cmd,check=True);r=subprocess.run([str(output/'check')],capture_output=True,text=True,timeout=30);(output/'check.log').write_text(r.stdout+r.stderr)
    report=dict(status='host-tested-not-hardware-validated' if r.returncode==0 else 'failed',exit=r.returncode,command=cmd,result=r.stdout+r.stderr,input_sha256={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs+[header,harness,Path(__file__)]},limits='Actual KGSL functions, pthread-blocked map/lookup interleavings and modeled krefs/IDR/backend/deferred worker. Not a weak-memory litmus, kernel stress, hardware execution or GPU-retirement proof. Profiling/validator caller and allocation sites also source-checked.')
    (output/'RESULT.json').write_text(json.dumps(report,indent=2)+'\n');r.check_returncode();print(r.stdout,end='')
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--source',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();check(a.source.resolve(),a.output.absolute())
