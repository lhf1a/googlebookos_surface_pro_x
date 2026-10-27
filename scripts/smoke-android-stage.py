#!/usr/bin/env python3
from pathlib import Path
import os,selectors,subprocess,time
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'out/android-test'
cmd=['qemu-system-aarch64','-machine','virt','-cpu','max','-m','4096','-smp','4','-nographic','-no-reboot','-nic','none',
     '-kernel',str(out/'Image'),'-initrd',str(out/'initramfs.cpio.gz'),
     '-append','console=ttyAMA0 loglevel=6 panic=0 sq2.test=smoke lsm=selinux enforcing=0']
for i,name in enumerate(['system','vendor','product','system_ext','system_dlkm','vendor_dlkm']):
    cmd+=['-drive',f'if=none,id=d{i},file={ROOT}/out/googlebook-stage/{name}_a.img,format=raw,readonly=on',
          '-device',f'virtio-blk-pci,drive=d{i}']
p=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
sel=selectors.DefaultSelector();sel.register(p.stdout,selectors.EVENT_READ)
transcript=bytearray();sent=False;deadline=time.monotonic()+180
try:
    with (out/'qemu-android-stage.log').open('wb') as log:
        while time.monotonic()<deadline:
            for key,_ in sel.select(1):
                data=os.read(key.fileobj.fileno(),65536)
                if not data:sel.unregister(key.fileobj);continue
                transcript+=data;log.write(data);log.flush()
            if not sent and b'SQ2-PROBE-READY' in transcript:
                p.stdin.write(b'sleep 2; echo ANDROID_HARNESS_READY; ls /dev/binder; run-googlebook; echo ANDROID_ATTEMPT_RC=$?; poweroff -f\n');p.stdin.flush();sent=True
            if p.poll() is not None:break
        else:raise RuntimeError('QEMU Android test timed out')
    for marker in [b'ANDROID_HARNESS_READY',b'GOOGLEBOOK_INIT_ATTEMPT',b'GOOGLEBOOK_INIT_EXIT=',b'Power down']:
        if marker not in transcript:raise RuntimeError('Missing marker '+repr(marker))
    print('PASS: Android-capable kernel boot and isolated original-init attempt executed; inspect log for Android boot outcome')
finally:
    if p.poll() is None:p.kill()
    p.wait();sel.close()
