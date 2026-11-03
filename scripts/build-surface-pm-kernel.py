#!/usr/bin/env python3
"""Build the generic framework-owner gate on an isolated current port source copy."""
from pathlib import Path
import argparse,shutil,subprocess,os,hashlib,json
R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--kernel-source',type=Path,required=True)
p.add_argument('--config',type=Path,required=True)
p.add_argument('--work',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--jobs',type=int,default=12)
a=p.parse_args()
for field in ('kernel_source','config','work','output'): setattr(a,field,getattr(a,field).resolve())
if a.work.exists() or a.output.exists():p.error('fresh work and output directories required')
if a.jobs<1:p.error('jobs must be positive')
source=(a.kernel_source/'kernel/power/suspend.c').read_text()
if 'static bool sq1_trial_take(' not in source:p.error('current diagnostic admission base required; apply the archived debug/single-owner patch first')
if 'static bool sq1_autosuspend_allowed(' in source:p.error('native owner gate already exists: reuse or review that source, do not nest patches')
a.work.mkdir(parents=True);a.output.mkdir(parents=True)
k=a.work/'kernel';b=a.work/'build'
shutil.copytree(a.kernel_source,k,symlinks=False);b.mkdir()
shutil.copyfile(a.config,b/'.config')
env=dict(os.environ,ARCH='arm64',CROSS_COMPILE='aarch64-linux-gnu-',
 KBUILD_BUILD_USER='surface-pm',KBUILD_BUILD_HOST='builder',
 KBUILD_BUILD_TIMESTAMP='Tue Nov 3 00:00:00 UTC 2026',KBUILD_BUILD_VERSION='1')
commands=[
 ['patch','--batch','--fuzz=0','-d',str(k),'-p1','-i',str(R/'patches/surface-pm/01-framework-owner.patch')],
 ['python3',str(R/'scripts/check-sq1-autosuspend-policy.py'),str(k/'kernel/power/suspend.c')],
 ['make','-s','-C',str(k),'O='+str(b),'olddefconfig'],
 ['cmp',str(a.config),str(b/'.config')],
 ['make','-s','-C',str(k),'O='+str(b),'-j'+str(a.jobs),'Image'],
]
with (a.output/'build.log').open('w') as log:
 for cmd in commands:subprocess.run(cmd,check=True,env=env,stdout=log,stderr=subprocess.STDOUT)
shutil.copyfile(b/'arch/arm64/boot/Image',a.output/'Image')
shutil.copyfile(b/'.config',a.output/'kernel.config')
manifest=dict(scope='shared SQ1/SQ2 code; no machine identity gate; build-only, real boot/key tests separate',
 commands=commands,inputs=dict(source=str(a.kernel_source.resolve()),config_sha256=hashlib.sha256(a.config.read_bytes()).hexdigest()),
 outputs={n:hashlib.sha256((a.output/n).read_bytes()).hexdigest() for n in ('Image','kernel.config')})
(a.output/'build.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps(manifest,indent=2))
