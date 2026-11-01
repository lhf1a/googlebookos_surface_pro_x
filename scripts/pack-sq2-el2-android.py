"""Build an Android EL2 payload with the existing video userspace preserved.

Build-only: matching module binaries do not prove PAS lifecycle or decoding.
Use a separate output directory; no deployment or boot selection is performed.
"""
from pathlib import Path
import argparse,gzip,importlib.util,hashlib,shutil,subprocess,zlib,json
r=Path(__file__).resolve().parents[1];p=r/'out/el2-android';spec=importlib.util.spec_from_file_location('c',r/'scripts/pack-sq2-vulkan.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--kernel-build',type=Path,required=True,help='matching kernel build directory containing .config and Image')
parser.add_argument('--venus-build',type=Path,required=True,help='custom Venus modules compiled against that kernel')
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--passwordless-user',help='explicit lab user; secure locks are refused at runtime')
parser.add_argument('--scm-trace',type=Path,help='optional bounded PAS observer built against this kernel')
parser.add_argument('--no-tz-overlay',type=Path,help='explicit EL2 no-TZ firmware SID overlay; enables sq2_no_tz')
args=parser.parse_args()
if args.no_tz_overlay and args.scm_trace:
 parser.error('no-TZ startup does not use the PAS observer')
if args.passwordless_user is not None and (not args.passwordless_user.isascii() or not args.passwordless_user.isdecimal()):
 parser.error('--passwordless-user requires a numeric Android user ID')
trace=None
if args.scm_trace:
 trace=args.scm_trace.read_bytes()
 vermagic=subprocess.check_output(['modinfo','-F','vermagic',str(args.scm_trace)],text=True).split()[0]
 if vermagic != '6.3.8-sq2-android1': parser.error('SCM observer kernel release mismatch')
B=args.kernel_build.resolve()
V=args.venus_build.resolve()
# Reject old pre-network builds before creating an output candidate.
config=(B/'.config').read_text().splitlines()
for option in ['KSU','KPROBES','KSU_KPROBES_HOOK','KVM','VHOST_VSOCK',
               'NETFILTER_XT_TARGET_CT','NF_CONNTRACK_FTP','NF_CONNTRACK_PPTP',
               'NF_NAT_FTP','NF_NAT_PPTP','NF_CT_PROTO_GRE']:
 assert 'CONFIG_'+option+'=y' in config,option
for option in ['NET_SCH_INGRESS','NET_CLS_BPF']:
 assert 'CONFIG_'+option+'=m' in config,option
# End required kernel configuration check.
args.output.mkdir(parents=True,exist_ok=False)
# These custom modules retain the normal decoder/overlay implementation;
# the generic in-tree Venus modules are not equivalent replacements.
video_overrides={
 'sq2_hfi_overlay.ko':Path('/root/.cache/sq2-el2-parity-sq2_hfi_overlay/sq2_hfi_overlay.ko'),
 'sq2_iris1_identify.ko':Path('/root/.cache/sq2-el2-parity-sq2_iris1_identify/sq2_iris1_identify.ko'),
 'videocc-sm8150.ko':Path('/root/.cache/sq2-el2-parity-videocc/videocc-sm8150.ko'),
 'venus-core.ko':V/'venus-core.ko',
 'venus-dec.ko':V/'venus-dec.ko',
}
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
# Begin optional no-TZ payload. The normal PAS helper stays byte-identical.
if args.no_tz_overlay:
 overlay=args.no_tz_overlay
 assert subprocess.check_output(['modinfo','-F','name',str(overlay)],text=True).strip() == 'sq2_fw_overlay'
 assert subprocess.check_output(['modinfo','-F','vermagic',str(overlay)],text=True).split()[0] == '6.3.8-sq2-android1'
 params=subprocess.check_output(['modinfo','-p',str(V/'venus-core.ko')],text=True)
 assert any(line.startswith('sq2_no_tz:') for line in params.splitlines())
 name='etc/sq2-video-native/sq2_fw_overlay.ko'
 assert name not in e
 e[name]=(0o100644,overlay.read_bytes());changed.append(name)
 name='bin/video-decoder-lab-start'
 mode,helper=e[name]
 guard=b'set -eu\n'
 assert helper.count(guard)==1
 helper=helper.replace(guard,guard+b'case " $(cat /proc/cmdline) " in *" sq2.el2=experimental "*) ;; *) exit 1;; esac\n')
 old=b'insmod venus-core.ko sq2_platform_allow=1\ninsmod venus-dec.ko\ncat /proc/sys/kernel/random/boot_id > /tmp/sq2-native-pas-attempted\ninsmod sq2_hfi_overlay.ko allow=1\n'
 assert helper.count(old)==1
 helper=helper.replace(old,b'insmod sq2_hfi_overlay.ko allow=1\ninsmod sq2_fw_overlay.ko allow=1\ninsmod venus-core.ko sq2_platform_allow=1 sq2_no_tz=1\ninsmod venus-dec.ko\ncat /proc/sys/kernel/random/boot_id > /tmp/sq2-native-pas-attempted\n')
 subprocess.run(['sh','-n'],input=helper,check=True)
 e[name]=(mode,helper);changed.append(name)
# End optional no-TZ payload.
# Refresh the runtime integrity manifest after replacing its module payloads.
manifest='etc/sq2-video-native/SHA256SUMS'
manifest_rows=[]
manifest_names=set()
for line in before[manifest][1].decode().splitlines():
 old_sha,name=line.split()
 assert Path(name).name==name and name not in manifest_names,name
 manifest_names.add(name)
 key='etc/sq2-video-native/'+name
 assert hashlib.sha256(before[key][1]).hexdigest()==old_sha,key
 manifest_rows.append(hashlib.sha256(e[key][1]).hexdigest()+'  '+name+'\n')
assert {Path(n).name for n in module_report if n.startswith('etc/sq2-video-native/')} <= manifest_names
e[manifest]=(before[manifest][0],''.join(manifest_rows).encode());changed.append(manifest)
# End runtime integrity manifest refresh.
if args.no_tz_overlay:
 name='etc/sq2-video-native/sq2_fw_overlay.ko'
 digest=hashlib.sha256(e[name][1]).hexdigest()
 e[manifest]=(e[manifest][0],e[manifest][1]+(digest+'  sq2_fw_overlay.ko\n').encode())
 module_report[name]=digest
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
init=init.replace(anchor,b'if grep -q "sq2.el2=experimental" /proc/cmdline; then\n mkdir -p "$logdir" || fail el2-log-directory\n mount --bind "$logdir" /logs || fail el2-logs-bind\n el2-kmsg-collect /logs/kernel-live.txt &\n el2-terminal-autostart > /logs/el2-terminal-autostart.txt 2>&1 &\nfi\n'+anchor)
e['init']=(0o100755,init);changed.append('init')
wifi='bin/wifi-services'
# Refresh the shared audio guard for firmware-started (attached) EL2 ADSP.
audio='bin/audio-drivers-start'
audio_helper=(r/'probe/test2/audio-drivers-start').read_bytes().replace(b'\r\n',b'\n')
subprocess.run(['sh','-n'],input=audio_helper,check=True)
e[audio]=(before[audio][0],audio_helper);changed.append(audio)
old=e[wifi][1]
anchor=b'rmtfs -r -o /run/rmtfs -s -v > /logs/rmtfs.txt 2>&1 &'
assert old.count(anchor)==1
e[wifi]=(e[wifi][0],old.replace(anchor,b'el2-rmtfs-start > /logs/rmtfs.txt 2>&1 &'))
changed.append(wifi)
e['bin/el2-rmtfs-start']=(0o100755,(r/'probe/virtualization/el2-rmtfs-start').read_bytes().replace(b'\r\n',b'\n'))
changed.append('bin/el2-rmtfs-start')
for n,f in [('bin/el2-terminal-stage',r/'probe/virtualization/el2-terminal-stage'),('bin/kvm-arm64-witness',r/'out/sq2-el2-test/kvm-arm64-witness'),('bin/el2-kmsg-collect',p/'el2-kmsg-collect')]:
 e[n]=(0o100755,f.read_bytes().replace(b'\r\n',b'\n') if n.endswith('stage') else f.read_bytes());changed.append(n)
e['bin/el2-terminal-autostart']=(0o100755,(r/'probe/virtualization/el2-terminal-autostart').read_bytes().replace(b'\r\n',b'\n'))
changed.append('bin/el2-terminal-autostart')
# Begin lab runtime integration. Preserve the deployed recovery and call order.
mode,init=e['init']
hook=b' el2-terminal-autostart > /logs/el2-terminal-autostart.txt 2>&1 &\n'
assert init.count(hook)==1 and b'el2-android-recovery' not in init
user=b'' if args.passwordless_user is None else b' '+args.passwordless_user.encode('ascii')
init=init.replace(hook,b' el2-terminal-autostart'+user+b' > /logs/el2-terminal-autostart.txt 2>&1 &\n'
                  b' el2-android-recovery > /logs/el2-recovery.txt 2>&1 &\n')
e['init']=(mode,init)
recovery=(r/'probe/virtualization/el2-android-recovery').read_bytes().replace(b'\r\n',b'\n')
subprocess.run(['sh','-n'],input=recovery,check=True)
subprocess.run(['sh','-n'],input=init,check=True)
e['bin/el2-android-recovery']=(0o100755,recovery);changed.append('bin/el2-android-recovery')
if trace is not None:
 name='bin/video-decoder-lab-start'
 mode,helper=e[name]
 anchor=b'mkdir /tmp/sq2-video-start-once\n'
 assert helper.count(anchor)==1 and b'sq2-scm-trace' not in helper
 observer=("echo '"+hashlib.sha256(trace).hexdigest()+"  /etc/sq2-scm-trace.ko' | sha256sum -c -\n"
           "insmod /etc/sq2-scm-trace.ko allow=1\ntrap 'rmmod sq2_scm_trace' EXIT\n").encode()
 helper=helper.replace(anchor,anchor+observer)
 subprocess.run(['sh','-n'],input=helper,check=True)
 e[name]=(mode,helper);changed.append(name)
 e['etc/sq2-scm-trace.ko']=(0o100644,trace);changed.append('etc/sq2-scm-trace.ko')
# End lab runtime integration.
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
(args.output/'pack-audit.json').write_text(json.dumps({'sha256':hashlib.sha256(result).hexdigest(),'kernel_sha256':hashlib.sha256((B/'arch/arm64/boot/Image').read_bytes()).hexdigest(),'changed':changed,'modules':module_report,'credential_last':True,'passwordless_user':args.passwordless_user,'recovery_seconds':360,'scm_trace_sha256':hashlib.sha256(trace).hexdigest() if trace is not None else None,'video_modules_replaced':sorted(video_names),'video_userspace_firmware_preserved':True,'vpu_el2_lifecycle_verified':False,'hardware_boot_tested':False},indent=2)+'\n')
print('Packed',len(result),'bytes; replaced',len(module_report),'modules; BUILD ONLY, VPU lifecycle unverified')
