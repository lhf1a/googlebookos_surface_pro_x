#!/usr/bin/env python3
"""Exercise actual KGSL SVM selection/mmap and matching core mmap rejection cleanup."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[1]

def extract(code, name):
    matches = re.findall(r"^(?:static )?(?:inline )?(?:int|void|unsigned long)\s+" +
                         re.escape(name) + r"\([^;{]*\)\n\{.*?^\}", code, re.M | re.S)
    if len(matches) != 1:
        raise ValueError('Expected one function: ' + name)
    return matches[0]

def check(source, kernel, output):
    output.mkdir(parents=True, exist_ok=False)
    inputs = [source/'kgsl.c', kernel/'mm/mmap.c', kernel/'mm/internal.h', kernel/'mm/vma.c']
    functions = [extract(inputs[0].read_text(), n) for n in ('_gpu_set_svm_region', 'get_align', 'check_svm_area', 'get_svm_unmapped_area', 'kgsl_get_unmapped_area', 'kgsl_mmap')]
    functions += [extract(inputs[1].read_text(), '__get_unmapped_area'), extract(inputs[2].read_text(), 'mmap_file'), extract(inputs[3].read_text(), '__mmap_new_file_vma')]
    header=output/'kgsl-svm-functions.h'; header.write_text('\n\n'.join(functions)+'\n')
    harness=ROOT/'tests/kgsl_svm_test.c'
    cmd=['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-fsanitize=address,undefined','-fno-pie','-no-pie','-I',str(output),str(harness),'-o',str(output/'check')]
    subprocess.run(cmd,check=True)
    r=subprocess.run([str(output/'check')],capture_output=True,text=True)
    (output/'check.log').write_text(r.stdout+r.stderr)
    report=dict(status='host-tested-not-hardware-validated' if r.returncode==0 else 'failed',exit=r.returncode,command=cmd,result=r.stdout+r.stderr,input_sha256={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs+[header,harness,Path(__file__)]},limits='Actual KGSL and core __get_unmapped_area/mmap_file/__mmap_new_file_vma; VMA tree, MMU, page insertion, LSM and retained-owner leaves modeled. No concurrent ioctl/GPU, actual 32-bit process, MM page tables or hardware translation proof.')
    (output/'RESULT.json').write_text(json.dumps(report,indent=2)+'\n');r.check_returncode();print(r.stdout,end='')

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source',type=Path,required=True);ap.add_argument('--kernel-source',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args();check(a.source.resolve(),a.kernel_source.resolve(),a.output.absolute())
