#!/usr/bin/env python3
"""Consume-before-chain boot preparation; preserve each input menu's identity/layout."""
import argparse,hashlib,re
from pathlib import Path

def prepare(data, launcher='/EFI/SurfacePmAuto/el2launch.efi', env_path='/EFI/SurfacePmAuto/once.env'):
    for value in (launcher,env_path):
        if not re.fullmatch(r'/(?:[A-Za-z0-9_.-]+/)*[A-Za-z0-9_.-]+',value) or '..' in value.split('/'):
            raise ValueError('unsafe EFI file path')
    text=data.decode('utf-8')
    if 'surface_pm_once' in text: raise ValueError('use the pristine menu, not an armed copy')
    defaults=re.findall(r'^set default=([\w-]+)\s*$',text,re.M)
    entries=list(re.finditer(r"^menuentry [^\n]*--id\s+([\w-]+)\s*\{\n(.*?)^\}\n?",text,re.M|re.S))
    if len(defaults)!=1 or len(entries)!=2: raise ValueError('one default and two existing entries required')
    selected=[m for m in entries if m[1]==defaults[0]]
    if len(selected)!=1: raise ValueError('default entry missing or ambiguous')
    m=selected[0];body=m[2]
    chains=list(re.finditer(r'^(\s*)chainloader\s+(/[^\s]+)\s*$',body,re.M))
    if len(chains)!=1: raise ValueError('one plain existing default chain required')
    chain=chains[0];original=chain[2];indent=chain[1]
    lines=[
        'set surface_pm_run=no','set surface_pm_once=0',f'if [ -s {env_path} ]; then',
        f' if load_env -f {env_path} surface_pm_once; then',
        '  if [ "$surface_pm_once" = "1" ]; then',
        '   set surface_pm_once=0',f'   if save_env -f {env_path} surface_pm_once; then',
        '    set surface_pm_once=unknown',f'    if load_env -f {env_path} surface_pm_once; then',
        '     if [ "$surface_pm_once" = "0" ]; then','      set surface_pm_run=yes',
        '     fi','    fi','   fi','  fi',' fi','fi',
        'if [ "$surface_pm_run" = "yes" ]; then',f' chainloader {launcher}',
        'else',f' chainloader {original}','fi',
    ]
    replacement='\n'.join(indent+l for l in lines)+'\n'
    newbody=body[:chain.start()]+replacement+body[chain.end():]
    result=text[:m.start(2)]+newbody+text[m.end(2):]
    env=b'# GRUB Environment Block\nsurface_pm_once=1\n'
    return result.encode(),env+b'#'*(1024-len(env))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('baseline',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--launcher',default='/EFI/SurfacePmAuto/el2launch.efi')
    p.add_argument('--env-path',default='/EFI/SurfacePmAuto/once.env')
    p.add_argument('--expected-menu-sha256')
    a=p.parse_args();data=a.baseline.read_bytes()
    if a.expected_menu_sha256 and hashlib.sha256(data).hexdigest()!=a.expected_menu_sha256:
        p.error('menu changed since the deployment snapshot')
    menu,env=prepare(data,a.launcher,a.env_path)
    a.output.mkdir(exist_ok=False)
    (a.output/'grub.cfg').write_bytes(menu);(a.output/'once.env').write_bytes(env)
