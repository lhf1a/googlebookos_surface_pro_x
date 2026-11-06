#!/usr/bin/env python3
"""Build an additive unrestricted PM environment; offline only, never writes a device."""
from pathlib import Path
import argparse,gzip,hashlib,importlib.util,json,re,shutil,zlib
R=Path(__file__).resolve().parents[1]
def load(n):
 s=importlib.util.spec_from_file_location(n,R/'scripts'/n);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def sha(b):return hashlib.sha256(b).hexdigest()
def build(a):
 if a.output.exists():raise ValueError('Fresh output directory required')
 for name in [a.directory,a.baseline_directory,a.baseline_launcher]:
  if not re.fullmatch(r'[A-Za-z0-9_.-]+',name) or name in ('.','..'):raise ValueError('Invalid EFI path component')
 p=load('surface-power-payload.py');c=load('pack-sq2-vulkan.py')
 raw=a.baseline.read_bytes();before=c.entries(gzip.decompress(raw))
 packed,audit=p.integrate(raw,R,a.power_bundle,c,policy='unrestricted');after=c.entries(gzip.decompress(packed))
 runtime=load('unrestricted-power-runtime.py')
 after,nested=runtime.nested_codec_runtime(after,a.codec_launcher.read_bytes() if a.codec_launcher else None)
 audit['nested_codec_runtime']=nested
 helper='bin/video-decoder-lab-start';module='etc/sq2-video-native/sq2_hfi_overlay.ko'
 if a.video_reference_module:
  import subprocess
  blob=a.video_reference_module.read_bytes()
  if b'SQ2_HFI_VIDEOCC' not in blob:raise ValueError('Dynamic VIDEOCC module required')
  for field,want in [('name','sq2_hfi_overlay'),('vermagic',__import__('kernel_baseline').policy()['kernel_release'])]:
   value=subprocess.check_output(['modinfo','-F',field,str(a.video_reference_module)],text=True).strip()
   if (value if field=='name' else value.split()[0])!=want:raise ValueError('Video reference module ABI/name mismatch')
  original=dict(after);fix=load('pack-sq2-video-reference-fix.py')
  after[helper]=(after[helper][0],fix.patch_helper(after[helper][1]))
  after[module]=(after[module][0],blob)
  after,_=runtime.refresh_manifests(original,after)
 elif helper in after and b'test "$ph" = 000000b5' in after[helper][1]:
  raise ValueError('Legacy fixed VIDEOCC requires explicit accepted --video-reference-module')
 audit['dynamic_video_reference']=bool(a.video_reference_module)

 rest=raw;identity=None
 while rest:
  d=zlib.decompressobj(31);data=d.decompress(rest)
  if not d.eof:raise ValueError('Truncated baseline gzip')
  member=rest[:len(rest)-len(d.unused_data)];e=c.entries(data)
  if 'etc/shadow' in e and 'bin/dropbear' in e:identity=member
  rest=d.unused_data
 if identity is None or not raw.endswith(identity):raise ValueError('Original identity member must already be last')
 changed={n:v for n,v in after.items() if v!=before.get(n)}
 audit['changed']=sorted(changed)
 if before.keys()-after.keys():raise ValueError('Cannot remove an entry using an additive overlay')
 body=b''.join(c.entry(n,b,m) for n,(m,b) in sorted(changed.items(),key=lambda x:(x[0].count('/'),x[0])))+c.entry('TRAILER!!!',b'',0);body+=b'\0'*(-len(body)%512)
 overlay=gzip.compress(body,compresslevel=9,mtime=0)+identity
 if c.entries(gzip.decompress(raw+overlay))!=after:raise ValueError('Layered last-entry-wins mismatch')
 modules=[n for n in set(before)|set(after) if n.endswith('.ko') and before.get(n)!=after.get(n)]
 if set(modules)-({module} if a.video_reference_module else set()):raise ValueError('Unrestricted retrofit must preserve other matching driver modules')
 unresolved=[]
 for n,(mode,b) in after.items():
  if b.startswith(b'\x7fELF') and b'/sys/module/suspend/parameters/screen_only' in b:unresolved.append(n)
 if unresolved:raise ValueError('Rebuild binary screen guard dependencies: '+','.join(unresolved))
 out=a.output/'EFI'/a.directory;out.mkdir(parents=True)
 files=p.bundle_files(a.power_bundle);(out/'Image').write_bytes(files['Image']);(out/'unrestricted.cpio.gz').write_bytes(overlay)
 for src,name in [(a.dtb,'android-el2.dtb'),(a.qebspil,'qebspilaa64.efi'),(a.tcb,'tcblaunch.exe')]:shutil.copyfile(src,out/name)
 base='\\EFI\\'+a.baseline_directory+'\\initramfs.cpio.gz';addon='\\EFI\\'+a.directory+'\\unrestricted.cpio.gz'
 efi=p.launcher(a.power_bundle,a.machine,directory=a.directory,policy='unrestricted',base_initrd=base,initrd_addons=(addon,));(out/'unrestricted.efi').write_bytes(efi)
 strings=[m[0][:-2].decode('utf-16le') for m in re.finditer(rb'(?:[\x20-\x7e]\x00){8,}\x00\x00',efi) if m[0].decode('utf-16le').startswith('console=')]
 if len(strings)!=1:raise ValueError('One kernel options string required')
 options=strings[0];tokens=options.split()
 if tokens.count('suspend.screen_only=0')!=1 or tokens.count('suspend.screen_only_autosuspend=0')!=1:raise ValueError('Unrestricted parameters missing')
 if b'framework-suspend-start >' in after['init'][1] or b'lab-keep-awake >' in after['init'][1]:raise ValueError('Artificial owner/keep-awake hook remains')
 (out/'kernel-options.txt').write_text(options+'\n')
 if a.menu:
  menu=a.menu.read_text();old='/EFI/'+a.baseline_directory+'/'+a.baseline_launcher
  if menu.count(old)!=1:raise ValueError('Exactly one original default chainloader required')
  (a.output/'grub.candidate.cfg').write_text(menu.replace(old,'/EFI/'+a.directory+'/unrestricted.efi'))
 hashes={p.name:sha(p.read_bytes()) for p in out.iterdir()};(out/'SHA256SUMS').write_text(''.join(h+'  '+n+'\n' for n,h in sorted(hashes.items())))
 report=dict(audit,baseline_initrd_sha256=sha(raw),baseline_initrd_efi_path=base,overlay_sha256=sha(overlay),overlay_bytes=len(overlay),credential_member_last=True,credential_bytes_unchanged=True,private_identity_present=True,not_for_cross_device_distribution=True,modules_changed=modules,changed_entries=sorted(changed),EFI_sha256=sha(efi),kernel_sha256=sha(files['Image']),new_esp_bytes=sum(x.stat().st_size for x in out.iterdir()),device_deployed=False,hardware_tested=False)
 (a.output/'environment.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for n in ['baseline','power-bundle','dtb','qebspil','tcb','output']:p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--machine',choices=('sq1','sq2'),default='sq2');p.add_argument('--directory',default='SQ2Free');p.add_argument('--baseline-directory',required=True);p.add_argument('--baseline-launcher',default='normal.efi');p.add_argument('--menu',type=Path)
 p.add_argument('--codec-launcher',type=Path,help='Explicit v2u launcher rebuilt from the paired source; preserves installed v1')
 p.add_argument('--video-reference-module',type=Path,help='Explicit accepted dynamic VIDEOCC module; no other module changes allowed')
 a=p.parse_args();result=build(a);print(json.dumps({k:result[k] for k in ['policy','screen_only_enabled','owner_gate','request_budget','admission_deadline','changed_entries','new_esp_bytes','device_deployed','hardware_tested']},indent=2))
