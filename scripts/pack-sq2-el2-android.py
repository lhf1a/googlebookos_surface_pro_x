"""Build an Android EL2 payload with the existing video userspace preserved.

Build-only: matching module binaries do not prove PAS lifecycle or decoding.
Use a separate output directory; no deployment or boot selection is performed.
"""
from pathlib import Path
import argparse,gzip,importlib.util,hashlib,shutil,subprocess,zlib,json
r=Path(__file__).resolve().parents[1];p=r/'out/el2-android';B=Path('/root/.cache/sq2-el2-android-20261031/build');spec=importlib.util.spec_from_file_location('c',r/'scripts/pack-sq2-vulkan.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
args.output.mkdir(parents=True,exist_ok=False)
# These custom modules retain the normal decoder/overlay implementation;
# the generic in-tree Venus modules are not equivalent replacements.
video_overrides={
 'sq2_hfi_overlay.ko':Path('/root/.cache/sq2-el2-parity-sq2_hfi_overlay/sq2_hfi_overlay.ko'),
 'sq2_iris1_identify.ko':Path('/root/.cache/sq2-el2-parity-sq2_iris1_identify/sq2_iris1_identify.ko'),
 'videocc-sm8150.ko':Path('/root/.cache/sq2-el2-parity-videocc/videocc-sm8150.ko'),
 'venus-core.ko':B.parent/'venus/venus-core.ko',
 'venus-dec.ko':B.parent/'venus/venus-dec.ko',
}
config=(B/'.config').read_text().splitlines()
for option in ['KSU','KPROBES','KSU_KPROBES_HOOK','KVM','VHOST_VSOCK']:
 assert 'CONFIG_'+option+'=y' in config,option
names=['native-combined-initramfs.cpio.gz','bluetooth-startup-v2.cpio.gz','touch-calibration-v2.cpio.gz','touch-firstboot-v1.cpio.gz','touch-size24-v1.cpio.gz','touch-firstboot-thumb-v1.cpio.gz'];raw=b''.join((p/n).read_bytes() for n in names);before=c.entries(gzip.decompress(raw));e=dict(before);changed=[];module_report={};missing=[]
for n,(m,b) in before.items():
 if not n.endswith('.ko'):continue
 if n.startswith('etc/sq2-video-native/') and Path(n).name in video_overrides:
  src=video_overrides[Path(n).name]
 elif n.startswith('lib/modules/'):
  rel=n.split('/kernel/',1)[1];src=B/rel
 elif n.endswith('sq2_battery_compat.ko'):src=p/'sq2_battery_compat.ko'
 else:
  matches=list(B.rglob(Path(n).name));src=matches[0] if len(matches)==1 else None
 if src is None or not src.exists():missing.append(n);continue
 data=src.read_bytes();tmp=args.output/'module.tmp.ko';tmp.write_bytes(data);subprocess.run(['aarch64-linux-gnu-strip','--strip-debug',str(tmp)],check=True);data=tmp.read_bytes();e[n]=(m,data);changed.append(n);module_report[n]=hashlib.sha256(data).hexdigest()
assert not missing,missing
stage=args.output/'mod-stage/lib/modules/6.3.8-sq2-android1';stage.mkdir(parents=True,exist_ok=True)
for n,(m,b) in e.items():
 if n.startswith('lib/modules/') and n.endswith('.ko'):
  f=args.output/'mod-stage'/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(b)
for n in ['modules.builtin','modules.builtin.modinfo','modules.order']:shutil.copyfile(B/n,stage/n)
subprocess.run(['depmod','-b',str(args.output/'mod-stage'),'6.3.8-sq2-android1'],check=True)
for f in stage.glob('modules.*'):
 n='lib/modules/6.3.8-sq2-android1/'+f.name;e[n]=(0o100644,f.read_bytes());changed.append(n)
prop='etc/googlebook-security/build.prop';old=e[prop][1];assert b'ro.boot.hypervisor.protected_vm.supported=1' in old;e[prop]=(e[prop][0],old.replace(b'ro.boot.hypervisor.protected_vm.supported=1',b'ro.boot.hypervisor.protected_vm.supported=0'));changed.append(prop)
init=e['init'][1];anchor=b'apply-device-info > /logs/device-info.txt 2>&1 || fail device-info\n';assert init.count(anchor)==1
init=init.replace(anchor,b'el2-terminal-stage > /logs/el2-terminal.txt 2>&1 || fail el2-terminal-stage\n'+anchor)
# Bounded explicit experiment logs only; never diagnostic Wi-Fi association.
anchor=b'# Normal-boot helper output stays in initramfs RAM, without synchronous ESP I/O.\n';assert init.count(anchor)==1
init=init.replace(anchor,b'if grep -q "sq2.el2=experimental" /proc/cmdline; then\n mkdir -p "$logdir" || fail el2-log-directory\n mount --bind "$logdir" /logs || fail el2-logs-bind\n el2-kmsg-collect /logs/kernel-live.txt &\nfi\n'+anchor)
e['init']=(0o100755,init);changed.append('init')
wifi='bin/wifi-services'
old=e[wifi][1]
anchor=b'rmtfs -r -o /run/rmtfs -s -v > /logs/rmtfs.txt 2>&1 &'
assert old.count(anchor)==1
e[wifi]=(e[wifi][0],old.replace(anchor,b'el2-rmtfs-start > /logs/rmtfs.txt 2>&1 &'))
changed.append(wifi)
e['bin/el2-rmtfs-start']=(0o100755,(r/'probe/virtualization/el2-rmtfs-start').read_bytes().replace(b'\r\n',b'\n'))
changed.append('bin/el2-rmtfs-start')
for n,f in [('bin/el2-terminal-stage',r/'probe/virtualization/el2-terminal-stage'),('bin/kvm-arm64-witness',r/'out/sq2-el2-test/kvm-arm64-witness'),('bin/el2-kmsg-collect',p/'el2-kmsg-collect')]:
 e[n]=(0o100755,f.read_bytes().replace(b'\r\n',b'\n') if n.endswith('stage') else f.read_bytes());changed.append(n)
# Retain the original final credential gzip member, last in the new image.
rest=raw;cred=None
while rest:
 d=zlib.decompressobj(31);members=c.entries(d.decompress(rest));member=rest[:len(rest)-len(d.unused_data)]
 if 'etc/shadow' in members and 'bin/dropbear' in members:cred=member
 rest=d.unused_data
assert cred
a=b''.join(c.entry(n,b,m) for n,(m,b) in e.items())+c.entry('TRAILER!!!',b'',0);a+=b'\0'*(-len(a)%512);result=gzip.compress(a,compresslevel=3,mtime=0)+cred
final=c.entries(gzip.decompress(result));assert all(final[n]==v for n,v in e.items());assert all(final[n]==v for n,v in before.items() if n not in changed)
video_names={n for n in before if n.startswith('etc/sq2-video-native/') and n.endswith('.ko')}
assert video_names and video_names <= module_report.keys()
assert all(final[n]==v for n,v in before.items() if n.startswith(('etc/sq2-c2-runtime-boot/','lib/firmware/')))
(args.output/'android-el2.cpio.gz').write_bytes(result)
(args.output/'pack-audit.json').write_text(json.dumps({'sha256':hashlib.sha256(result).hexdigest(),'kernel_sha256':hashlib.sha256((B/'arch/arm64/boot/Image').read_bytes()).hexdigest(),'changed':changed,'modules':module_report,'credential_last':True,'video_modules_replaced':sorted(video_names),'video_userspace_firmware_preserved':True,'vpu_el2_lifecycle_verified':False,'hardware_boot_tested':False},indent=2)+'\n')
print('Packed',len(result),'bytes; replaced',len(module_report),'modules; BUILD ONLY, VPU lifecycle unverified')
