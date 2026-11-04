#!/usr/bin/env python3
"""QEMU-only actual-kernel PM admission test; freezer stage, not real hardware sleep."""
from pathlib import Path
import argparse,gzip,hashlib,importlib.util,json,re,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('image',type=Path);p.add_argument('efi',type=Path);p.add_argument('busybox',type=Path);p.add_argument('output',type=Path)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
r=Path(__file__).resolve().parents[1];spec=importlib.util.spec_from_file_location('cpio',r/'scripts/pack-sq2-vulkan.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
efi=a.efi.read_bytes();strings=[m[0][:-2].decode('utf-16le') for m in re.finditer(rb'(?:[\x20-\x7e]\x00){8,}\x00\x00',efi) if m[0].decode('utf-16le').startswith('console=')]
assert len(strings)==1
params=[t for t in strings[0].split() if t.startswith('suspend.')]
flags=[t for t in params if t.startswith('suspend.screen_only=')];assert len(flags)==1 and flags[0] in ['suspend.screen_only=0','suspend.screen_only=1']
init=r"""#!/bin/busybox sh
export PATH=/bin
/bin/busybox --install -s /bin
mkdir -p /proc /sys /dev /tmp
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
[ -c /dev/console ] || mknod /dev/console c 5 1
exec </dev/console >/dev/console 2>&1
printf 'POLICY=';cat /sys/module/suspend/parameters/screen_only
printf 'OWNER_BEFORE=';cat /sys/module/suspend/parameters/autosuspend_tgid
printf 'CALLER_UID=';id -u
echo 0 > /sys/module/suspend/parameters/pm_test_delay
echo freezer > /sys/power/pm_test || { echo PM_TEST_SETUP_FAILED;poweroff -f; }
for n in 1 2 3;do
 (echo freeze > /sys/power/state) 2>/tmp/error
 result=$?
 echo REQUEST=$n,RC=$result
 cat /tmp/error
done
printf 'OWNER_AFTER=';cat /sys/module/suspend/parameters/autosuspend_tgid
echo none > /sys/power/pm_test
echo QEMU_UNRESTRICTED_ADMISSION_FINISHED
poweroff -f
"""
entries={'bin':(0o40755,b''),'dev':(0o40755,b''),'proc':(0o40755,b''),'sys':(0o40755,b''),'tmp':(0o41777,b''),'bin/busybox':(0o100755,a.busybox.read_bytes()),'init':(0o100755,init.encode())}
body=b''.join(c.entry(n,data,mode) for n,(mode,data) in entries.items())+c.entry('TRAILER!!!',b'',0);body+=b'\0'*(-len(body)%512)
initrd=a.output/'fixture.cpio.gz';initrd.write_bytes(gzip.compress(body,mtime=0))
cmd=['qemu-system-aarch64','-machine','virt','-cpu','cortex-a72','-m','1024','-smp','2','-nographic','-no-reboot','-nic','none','-kernel',str(a.image.resolve()),'-initrd',str(initrd.resolve()),'-append','console=ttyAMA0 rdinit=/init panic=0 nohibernate lsm=selinux enforcing=0 '+' '.join(params)]
(a.output/'command.json').write_text(json.dumps(cmd,indent=2)+'\n')
try:
 q=subprocess.run(cmd,capture_output=True,timeout=90);stdout,stderr,rc=q.stdout,q.stderr,q.returncode
except subprocess.TimeoutExpired as ex:stdout,stderr,rc=ex.stdout or b'',(ex.stderr or b'')+b'\nQEMU_TIMEOUT\n',124
(a.output/'stdout').write_bytes(stdout);(a.output/'stderr').write_bytes(stderr);(a.output/'exit').write_text(str(rc)+'\n')
s=stdout.decode(errors='replace');policy='N' if flags[0].endswith('=0') else 'Y';values=[int(x) for x in re.findall(r'REQUEST=\d+,RC=(\d+)',s)]
assert rc==0,(rc,s[-5000:],stderr)
assert 'POLICY='+policy in s and 'CALLER_UID=0' in s and 'OWNER_BEFORE=0' in s and 'OWNER_AFTER=0' in s,s[-6000:]
assert len(values)==3 and all((v==0)==(policy=='N') for v in values),s[-6000:]
assert 'QEMU_UNRESTRICTED_ADMISSION_FINISHED' in s and 'PM_TEST_SETUP_FAILED' not in s
result={'screen_only':policy,'caller_uid':0,'owner_before':0,'owner_after':0,'same_request_input':'freeze x3; pm_test=freezer; pm_test_delay=0','request_exit_codes':values,'qemu_exit':rc,'image_sha256':hashlib.sha256(a.image.read_bytes()).hexdigest(),'efi_sha256':hashlib.sha256(efi).hexdigest(),'scope':'Actual ARM64 kernel PM admission in QEMU; NO real board sleep or long-term stability proof'}
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
