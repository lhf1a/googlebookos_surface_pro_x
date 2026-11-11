#!/usr/bin/env python3
"""Check actual framebuffer format propagation/ref ownership with modeled driver leaves.
Native probes check real 6.18 declarations; full TU remains a separate attempt.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def one(text,pattern):
 m=re.findall(pattern,text,re.M|re.S)
 if len(m)!=1:raise ValueError((pattern,len(m)))
 return m[0]
def function(text,name):return one(text,r'^(?:static inline\n|static )?[^\n;{}]*\b'+name+r'\([^;{]*\)\n\{.*?^\}')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--kernel-source',type=Path,required=True);p.add_argument('--kernel-build',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--kind',choices=['host','probe','full'],default='host');a=p.parse_args()
 msm=a.source if (a.source/'msm_fb.c').exists() else a.source/'techpack/display/msm';c=msm/'msm_fb.c';h=msm/'msm_drv.h';text=c.read_text();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 K=a.kernel_source;fh=K/'include/drm/drm_fourcc.h';fc=K/'drivers/gpu/drm/drm_fourcc.c';mc=K/'drivers/gpu/drm/drm_modeset_helper.c';mh=K/'include/drm/drm_mode_config.h'
 inputs={str(x.resolve()):sha(x) for x in [c,h,fh,fc,mc,mh,Path(__file__),ROOT/'tests/sde_framebuffer_api_test.c']}
 create_proto=one(h.read_text(),r'^struct drm_framebuffer \*msm_framebuffer_create\([^;]+;')
 init_proto=one(h.read_text(),r'^struct drm_framebuffer \*msm_framebuffer_init\([^;]+;')
 callback=one(mh.read_text(),r'\tstruct drm_framebuffer \*\(\*fb_create\)\([^;]+;')
 if a.kind=='host':
  start=text.index('static struct drm_framebuffer *msm_framebuffer_init_with_info') if 'static struct drm_framebuffer *msm_framebuffer_init_with_info' in text else text.index('struct drm_framebuffer *msm_framebuffer_create')
  end=text.index('\nstruct drm_framebuffer *\nmsm_alloc_stolen_fb')
  (out/'actual-source.h').write_text(init_proto+'\n'+create_proto+'\n'+one(text,r'^struct msm_framebuffer \{.*?^\};')+'\n'+text[start:end]+'\n')
  (out/'actual-format.h').write_text(one(fh.read_text(),r'^struct drm_format_info \{.*?^\};')+'\n'+function(fh.read_text(),'drm_format_info_plane_width')+'\n'+function(fh.read_text(),'drm_format_info_plane_height')+'\n'+ '\n'.join(function(fc.read_text(),name) for name in ['drm_format_info_block_width','drm_format_info_block_height','drm_format_info_min_pitch'])+'\n')
  (out/'actual-fill.h').write_text(function(mc.read_text(),'drm_helper_mode_fill_fb_struct')+'\n')
  (out/'actual-callback.h').write_text('struct actual_mode_config {\n'+callback+'\n};\n')
  cmds=[['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-sign-compare','-fsanitize=address,undefined','-fno-pie','-no-pie','-I',str(out),str(ROOT/'tests/sde_framebuffer_api_test.c'),'-o',str(out/'check')],[str(out/'check')]]
 elif a.kind=='probe':
  lookup=re.findall(r'drm_get_format_info\([^;]+\)',text);assert lookup
  fill=one(text,r'\tdrm_helper_mode_fill_fb_struct\([^;]+;').strip()
  (out/'probe.c').write_text('#include <drm/drm_fourcc.h>\n#include <drm/drm_framebuffer.h>\n#include <drm/drm_mode_config.h>\n#include <drm/drm_modeset_helper.h>\n'+create_proto+'\nstatic const struct drm_mode_config_funcs __used probe_ops = {.fb_create=msm_framebuffer_create};\nstatic void __used probe(struct drm_device *dev,struct drm_framebuffer *fb,const struct drm_mode_fb_cmd2 *mode_cmd)\n{\nconst struct drm_format_info *info = '+lookup[-1]+';\n'+fill+'\n(void)info;\n}\n')
  (out/'Kbuild').write_text('obj-m += probe.o\n');module=out;target='probe.o'
 else:module=msm.resolve();target='msm_fb.o'
 if a.kind!='host':
  if not a.kernel_build:p.error('--kernel-build is required')
  cmds=[['make','-C',str(a.kernel_build.resolve()),'M='+str(module),'ARCH=arm64','CROSS_COMPILE=aarch64-linux-gnu-','KCFLAGS=-Werror',target,'-j2']]
 runs=[]
 for cmd in cmds:
  q=subprocess.run(cmd,capture_output=True,text=True);runs.append(dict(command=cmd,exit=q.returncode,stdout=q.stdout,stderr=q.stderr));print(a.kind,len(runs),q.returncode,q.stdout.strip() if a.kind=='host' else '',flush=True)
  if q.returncode:break
 (out/'RESULT.json').write_text(json.dumps(dict(kind=a.kind,inputs=inputs,runs=runs,scope=__doc__),indent=2)+'\n');return runs[-1]['exit']
if __name__=='__main__':raise SystemExit(main())
