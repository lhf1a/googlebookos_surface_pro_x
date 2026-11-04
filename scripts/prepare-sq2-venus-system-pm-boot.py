#!/usr/bin/env python3
"""Offline additive devices-stage Venus PM boot; never deploys or changes default.

Requires the full accepted Android base + unrestricted overlay and a freshly
compiled matching --system-sleep module pair. Outputs are private when inputs
contain identity data. No actual low-power request is made by this tool.
"""
from pathlib import Path
import argparse,gzip,hashlib,importlib.util,json,re,subprocess,zlib
R=Path(__file__).resolve().parents[1]
def load(n):
 s=importlib.util.spec_from_file_location(n,R/'scripts'/n);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def sha(b):return hashlib.sha256(b).hexdigest()
def build(a):
 if a.output.exists():raise ValueError('Fresh output directory required')
 for n in [a.directory,a.current_directory,a.base_directory]:
  if not re.fullmatch('[A-Za-z0-9_.-]+',n) or n in ('.','..'):raise ValueError('Invalid EFI component')
 c=load('pack-sq2-vulkan.py');power=load('surface-power-payload.py')
 base=a.base.read_bytes();current=a.current_overlay.read_bytes()
 before=c.entries(gzip.decompress(base+current));after=dict(before)
 module_dir=a.module_directory
 if not json.loads((module_dir/'candidate.json').read_text()).get('system_sleep_candidate'):
  raise ValueError('Explicit --system-sleep candidate manifest required')
 for name in ['venus-core.ko','venus-dec.ko']:
  p=module_dir/name;blob=p.read_bytes()
  if blob[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(blob[18:20],'little')!=183:raise ValueError('AArch64 ELF required')
  abi=subprocess.check_output(['modinfo','-F','vermagic',str(p)],text=True).strip()
  old=before['etc/sq2-video-native/'+name][1]
  if (abi+'\0').encode() not in old:raise ValueError('Actual baseline module ABI mismatch')
  if name=='venus-core.ko' and b'sq2_pm_trace' not in blob:raise ValueError('New system PM callback missing')
  after['etc/sq2-video-native/'+name]=(before['etc/sq2-video-native/'+name][0],blob)
 bt_entry=None
 if a.bluetooth_module:
  names=[n for n in before if n.endswith('/hci_uart.ko')]
  if len(names)!=1:raise ValueError('One existing HCI UART module required')
  bt_entry=names[0];blob=a.bluetooth_module.read_bytes()
  abi=subprocess.check_output(['modinfo','-F','vermagic',str(a.bluetooth_module)],text=True).strip()
  name=subprocess.check_output(['modinfo','-F','name',str(a.bluetooth_module)],text=True).strip()
  if name!='hci_uart' or (abi+'\0').encode() not in before[bt_entry][1]:raise ValueError('HCI UART ABI/name mismatch')
  after[bt_entry]=(before[bt_entry][0],blob)
 if a.audio_wsa_bootstrap:
  name='bin/audio-drivers-start';blob=a.audio_wsa_bootstrap.read_bytes()
  if not blob.startswith(b'#!/bin/busybox sh') or b'AUDIO_WSA_ENUM_READY' not in blob:raise ValueError('Explicit WSA enumeration bootstrap required')
  after[name]=(before[name][0],blob)
 helper='bin/video-decoder-lab-start';s=before[helper][1]
 old=b'insmod venus-core.ko sq2_platform_allow=1 sq2_no_tz=1\n'
 if s.count(old)!=1:raise ValueError('Unknown no-TZ startup helper')
 after[helper]=(before[helper][0],s.replace(old,old.rstrip()+b' sq2_pm_trace=1\n'))
 init=before['init'][1]
 if b'framework-suspend-start >' in init or b'lab-keep-awake >' in init:raise ValueError('Unrestricted baseline required')
 old=b'mount -t sysfs sysfs /sys\n'
 if init.count(old)!=1:raise ValueError('Unknown init sysfs anchor')
 # Install the stage before Android starts. It remains devices for this whole
 # diagnostic boot, including native Android requests. Not an owner/budget gate.
 stage=b'echo 0 > /sys/module/suspend/parameters/pm_test_delay || exit 1\necho devices > /sys/power/pm_test || exit 1\ngrep -q "\\[devices\\]" /sys/power/pm_test || exit 1\n'
 init=init.replace(old,old+stage)
 if a.bluetooth_module:
  marker=b'mount -t tmpfs tmpfs /tmp\n'
  if init.count(marker)!=1:raise ValueError('Unknown RAM log capture anchor')
  # Bounded RAM-only initial-kmsg capture, not a boot/recovery/admission timer.
  init=init.replace(marker,marker+b'(timeout 120 cat /dev/kmsg > /logs/bluetooth-initial-kmsg.txt 2>&1) &\n')
 after['init']=(before['init'][0],init)
 name='etc/sq2-video-native/SHA256SUMS';lines=[]
 for line in before[name][1].decode().splitlines():
  h,n=line.split(None,1);n=n.lstrip('*');oldbody=before['etc/sq2-video-native/'+n][1]
  if sha(oldbody)!=h:raise ValueError('Original native manifest mismatch: '+n)
  lines.append(sha(after['etc/sq2-video-native/'+n][1])+'  '+n+'\n')
 after[name]=(before[name][0],''.join(lines).encode())
 changed={n:v for n,v in after.items() if v!=before.get(n)}
 allowed={'init',helper,name,'etc/sq2-video-native/venus-core.ko','etc/sq2-video-native/venus-dec.ko'}
 if bt_entry:allowed.add(bt_entry)
 if a.audio_wsa_bootstrap:allowed.add('bin/audio-drivers-start')
 if set(changed)!=allowed:raise ValueError('Unexpected effective changes')
 rest=current;identity=None
 while rest:
  d=zlib.decompressobj(31);data=d.decompress(rest)
  if not d.eof:raise ValueError('Truncated gzip')
  member=rest[:len(rest)-len(d.unused_data)]
  e=c.entries(data)
  if 'etc/shadow' in e and 'bin/dropbear' in e:identity=member
  rest=d.unused_data
 if identity is None or not current.endswith(identity):raise ValueError('Current identity member must be last')
 body=b''.join(c.entry(n,b,m) for n,(m,b) in sorted(changed.items()))+c.entry('TRAILER!!!',b'',0)
 body+=b'\0'*(-len(body)%512)
 overlay=gzip.compress(body,mtime=0)+identity
 if c.entries(gzip.decompress(base+current+overlay))!=after:raise ValueError('Last-entry-wins verification failed')
 addon='\\EFI\\'+a.directory+'\\devices.cpio.gz'
 efi=power.launcher(a.power_bundle,'sq2',directory=a.current_directory,policy='unrestricted',base_initrd='\\EFI\\'+a.base_directory+'\\initramfs.cpio.gz',initrd_addons=('\\EFI\\'+a.current_directory+'\\unrestricted.cpio.gz',addon))
 if a.own_kernel:
  image=power.bundle_files(a.power_bundle)['Image']
  if image[0x38:0x3c]!=b'ARMd':raise ValueError('ARM64 Image header required')
  old=('\\EFI\\'+a.current_directory+'\\Image'+'\0').encode('utf-16le')
  new=('\\EFI\\'+a.directory+'\\Image'+'\0').encode('utf-16le')
  if efi.count(old)!=1 or len(new)>len(old):raise ValueError('Kernel path relocation must fit one EFI string')
  efi=efi.replace(old,new+b'\0'*(len(old)-len(new)),1)
 opts=[m[0][:-2].decode('utf-16le') for m in re.finditer(rb'(?:[\x20-\x7e]\x00){8,}\x00\x00',efi) if m[0].startswith(b'c\x00o\x00n\x00s\x00o\x00l\x00e\x00=\x00')]
 if len(opts)!=1 or len(re.findall(r'\binitrd=',opts[0]))!=3:raise ValueError('Three initrd options required')
 out=a.output/'EFI'/a.directory;out.mkdir(parents=True)
 (out/'devices.cpio.gz').write_bytes(overlay);(out/'devices.efi').write_bytes(efi)
 if a.own_kernel:(out/'Image').write_bytes(image)
 (out/'kernel-options.txt').write_text(opts[0]+'\n')
 hashes={p.name:sha(p.read_bytes()) for p in out.iterdir()};(out/'SHA256SUMS').write_text(''.join(h+'  '+n+'\n' for n,h in sorted(hashes.items())))
 report=dict(stage='devices',actual_low_power=False,system_pm_candidate=True,audio_wsa_bootstrap=bool(a.audio_wsa_bootstrap),kernel_image_sha256=sha(power.bundle_files(a.power_bundle)['Image']),own_kernel=a.own_kernel,bluetooth_failure_cleanup=bool(a.bluetooth_module),changed_entries=sorted(changed),baseline_sha256=sha(base),current_overlay_sha256=sha(current),overlay_sha256=sha(overlay),efi_sha256=sha(efi),effective_hashes={n:sha(v[1]) for n,v in changed.items()},identity_unchanged_last=True,private_identity_present=True,not_for_cross_device_distribution=True,device_deployed=False,hardware_tested=False)
 (a.output/'environment.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for n in ['base','current-overlay','power-bundle','module-directory','output']:p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--audio-wsa-bootstrap',type=Path,help='Explicit paired WSA enumeration/recovery bootstrap; original driver unchanged')
 p.add_argument('--own-kernel',action='store_true',help='Explicit derived matching Image in this new EFI directory; other payload paths remain current')
 p.add_argument('--bluetooth-module',type=Path,help='Explicit matching HCI UART failed-setup cleanup candidate; enables initial RAM-only BT log')
 p.add_argument('--directory',default='SQ2Vpm');p.add_argument('--current-directory',default='SQ2Free');p.add_argument('--base-directory',default='SQ2Power')
 a=p.parse_args();r=build(a);print(json.dumps(r,indent=2))
