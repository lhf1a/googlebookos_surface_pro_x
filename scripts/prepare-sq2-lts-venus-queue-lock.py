#!/usr/bin/env python3
"""Port the 6.18 Venus per-instance VB2 queue mutex into an isolated module tree.

All Venus modules must be rebuilt together because venus_inst layout changes.
Never hot replace the resident firmware-owning core to validate this change.
"""
import argparse,hashlib,json,shutil
from pathlib import Path

def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Expected unique source anchor: '+repr(old))
    return text.replace(old,new,1)

def patch(root):
    path=root/'core.h';s=path.read_text();at=s.index('struct venus_inst {')
    s=s[:at]+once(s[at:],'\tstruct mutex lock;','\tstruct mutex lock;\n\t/* Serialize VB2 queue ioctls separately from firmware state. */\n\tstruct mutex ctx_q_lock;')
    path.write_text(s)
    for name in ('vdec.c','venc.c'):
        path=root/name;s=path.read_text()
        s=once(s,'\tmutex_init(&inst->lock);','\tmutex_init(&inst->lock);\n\tmutex_init(&inst->ctx_q_lock);')
        for queue in ('src_vq','dst_vq'):
            s=once(s,f'\t{queue}->dev = inst->core->dev;',f'\t{queue}->dev = inst->core->dev;\n\t{queue}->lock = &inst->ctx_q_lock;')
        s=once(s,'\tmutex_destroy(&inst->lock);','\tmutex_destroy(&inst->ctx_q_lock);\n\tmutex_destroy(&inst->lock);')
        path.write_text(s)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('source',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args()
    assert not a.output.exists()
    a.output.mkdir(parents=True)
    originals={}
    for f in a.source.iterdir():
        if f.suffix in ('.c','.h') or f.name in ('Makefile','Kconfig'):
            originals[f.name]=hashlib.sha256(f.read_bytes()).hexdigest();shutil.copyfile(f,a.output/f.name)
    patch(a.output)
    changed={}
    for n,h in originals.items():
        assert hashlib.sha256((a.source/n).read_bytes()).hexdigest()==h
        new=hashlib.sha256((a.output/n).read_bytes()).hexdigest()
        if new!=h:changed[n]={'baseline':h,'modified':new}
    assert set(changed)=={'core.h','vdec.c','venc.c'}
    manifest={'source':str(a.source.resolve()),'output':str(a.output.resolve()),'changes':changed,'rebuild_all_venus_modules':True,'hardware_verified':False}
    (a.output/'source-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))
if __name__=='__main__':main()
