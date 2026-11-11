#!/usr/bin/env python3
"""Exact fence diagnostic callers with real ops declaration; host leaves modeled.

ARM64 probe uses actual 6.18 headers and downstream context declaration. Full
consumer compilation is separate. Neither mode is hardware/lifecycle acceptance.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess

ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def block(text,pattern):
 m=re.search(pattern,text,re.M|re.S)
 if not m:raise ValueError(pattern)
 return m.group(0)
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--source',type=Path,required=True);p.add_argument('--kernel-source',type=Path,required=True)
 p.add_argument('--kernel-build',type=Path);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--kind',choices=['host','probe','full'],default='host');a=p.parse_args()
 msm=a.source if (a.source/'sde/sde_fence.c').exists() else a.source/'techpack/display/msm'
 c=msm/'sde/sde_fence.c';h=msm/'sde/sde_fence.h';kh=a.kernel_source/'include/linux/dma-fence.h'
 text=c.read_text();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 prefix=text[text.index('#define TIMELINE_VAL_LENGTH'):text.index('/**\n * _sde_fence_create_fd')]
 dump=block(text,r'^void sde_fence_list_dump\([^;{]*\)\n\{.*?^\}')
 (out/'actual-source.h').write_text(prefix+'\n'+dump+'\n')
 inputs={str(x.resolve()):sha(x) for x in [c,h,kh,Path(__file__),ROOT/'tests/sde_fence_diagnostics_test.c']}
 if a.kind=='host':
  (out/'actual-ops.h').write_text(block(kh.read_text(),r'^struct dma_fence_ops \{.*?^\};')+'\n')
  (out/'actual-context.h').write_text(block(h.read_text(),r'^struct sde_fence_context \{.*?^\};')+'\n')
  cmds=[['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-fsanitize=address,undefined','-fno-pie','-no-pie','-I',str(out),str(ROOT/'tests/sde_fence_diagnostics_test.c'),'-o',str(out/'check')],[str(out/'check')]]
 elif a.kind=='probe':
  (out/'probe.c').write_text('#include <linux/sync_file.h>\n#include <linux/dma-fence.h>\n#include <linux/slab.h>\n#include <linux/seq_file.h>\n#include <drm/drm_mode_object.h>\n#include "'+str(h.resolve())+'"\n#define SDE_ERROR(...) pr_err(__VA_ARGS__)\n#define SDE_DEBUG(...) pr_debug(__VA_ARGS__)\n#include "actual-source.h"\n')
  (out/'Kbuild').write_text('obj-m += probe.o\n');module=out;target='probe.o'
 else:module=msm.resolve();target='sde/sde_fence.o'
 if a.kind!='host':
  if not a.kernel_build:p.error('--kernel-build required for native compilation')
  cmds=[['make','-C',str(a.kernel_build.resolve()),'M='+str(module),'ARCH=arm64','CROSS_COMPILE=aarch64-linux-gnu-','KCFLAGS=-Werror',target,'-j2']]
 runs=[]
 for cmd in cmds:
  q=subprocess.run(cmd,capture_output=True,text=True)
  runs.append(dict(command=cmd,exit=q.returncode,stdout=q.stdout,stderr=q.stderr))
  print(a.kind,'stage',len(runs),'exit',q.returncode,q.stdout.strip() if a.kind=='host' else '',flush=True)
  if q.returncode:break
 (out/'RESULT.json').write_text(json.dumps(dict(kind=a.kind,inputs=inputs,runs=runs,scope=__doc__),indent=2)+'\n')
 return runs[-1]['exit']
if __name__=='__main__':raise SystemExit(main())
