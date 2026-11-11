#!/usr/bin/env python3
"""Check real pinctrl header use sites and plane logging-include order.

Narrow probes use actual public headers and the source's actual include decision;
full mode compiles six complete source TUs, retaining unrelated failures.
Neither establishes driver/hardware readiness.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

FILES=['dp/dp_parser.c','dp/dp_power.c','dp/dp_gpio_hpd.c','dp/dp_lphw_hpd.c','dsi/dsi_panel.c','sde/sde_plane.c']


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',required=True,type=Path)
    p.add_argument('--kernel-build',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--kind',choices=['probe','full'],default='full')
    a=p.parse_args();root=a.source.resolve();msm=root/'techpack/display/msm' if (root/'techpack/display/msm').is_dir() else root
    out=a.output.absolute();out.mkdir(parents=True,exist_ok=False)
    inputs={};sources={}
    for n in FILES:
        raw=(msm/n).read_bytes();inputs[str(msm/n)]=hashlib.sha256(raw).hexdigest();sources[n]=raw.decode()
    if a.kind=='probe':
        for i,n in enumerate(FILES):
            text=sources[n]
            if i<5:
                header='#include <linux/pinctrl/consumer.h>\n' if '#include <linux/pinctrl/consumer.h>' in text else ''
                symbols=sorted(set(re.findall(r'\b((?:devm_)?pinctrl_(?:get|put|lookup_state|select_state))\(',text)))
                assert symbols,n
                body=header+'#include <linux/compiler_attributes.h>\nconst void *const pinctrl_symbols[] __used = {'+','.join(symbols)+'};\n'
            else:
                pr=re.search(r'^#define pr_fmt\(fmt\).*$',text,re.M);inc=re.search(r'^#include <drm/drm_gem_atomic_helper.h>$',text,re.M)
                assert pr and inc
                first,second=sorted([pr,inc],key=lambda m:m.start())
                body=first.group()+'\n'+second.group()+'\n#include <linux/printk.h>\nvoid log_probe(void);\nvoid log_probe(void) { pr_info("probe\\n"); }\n'
            (out/('probe%d.c'%i)).write_text(body)
        (out/'Kbuild').write_text('obj-m := '+' '.join('probe%d.o'%i for i in range(6))+'\n')
        module=out;targets=['probe%d.o'%i for i in range(6)]
    else:
        module=msm;targets=[n[:-2]+'.o' for n in FILES]
    cmd=['make','-C',str(a.kernel_build.resolve()),'M='+str(module),'ARCH=arm64','CROSS_COMPILE=aarch64-linux-gnu-','KCFLAGS=-Werror','-k','-j4',*targets]
    r=subprocess.run(cmd,capture_output=True,text=True)
    (out/'stdout.txt').write_text(r.stdout);(out/'stderr.txt').write_text(r.stderr)
    errors=[l for l in r.stderr.splitlines() if 'error:' in l]
    target_errors=[l for l in errors if ('pinctrl' in l and ('implicit declaration' in l or 'from integer' in l or 'undeclared' in l)) or ('pr_fmt' in l and 'redefined' in l)]
    report=dict(kind=a.kind,command=cmd,exit=r.returncode,stdout=r.stdout,stderr=r.stderr,inputs=inputs,target_errors=target_errors,objects=[str(module/n) for n in targets if (module/n).exists()],scope=__doc__)
    (out/'RESULT.json').write_text(json.dumps(report,indent=2)+'\n')
    print(a.kind,'exit',r.returncode,'objects',len(report['objects']),'target_errors',len(target_errors))
    return r.returncode


if __name__=='__main__':raise SystemExit(main())
