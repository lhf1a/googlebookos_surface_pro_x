#!/usr/bin/env python3
"""Real PID 1 boot, persistent FAT log disk, immutable source partitions."""
from pathlib import Path
import subprocess,time,sys,argparse
parser=argparse.ArgumentParser()
parser.add_argument('--duration',type=int,default=150)
args=parser.parse_args()
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'out/android-test'
disk=Path('/root/.cache/sq2-probe-build/googlebook/qemu-native-logs.img')
with disk.open('wb') as f:f.truncate(64*1024*1024)
subprocess.run(['mkfs.vfat',str(disk)],check=True,stdout=subprocess.DEVNULL)
cmd=['qemu-system-aarch64','-machine','virt','-cpu','max','-m','4096','-smp','4','-nographic','-no-reboot','-nic','none',
     '-kernel',str(out/'Image'),'-initrd',str(out/'native-initramfs.cpio.gz'),
     '-append','console=ttyAMA0 loglevel=4 panic=0 sq2.native=smoke lsm=selinux enforcing=0 printk.devkmsg=on']
for i,name in enumerate(['system','vendor','product','system_ext','system_dlkm','vendor_dlkm']):
    cmd+=['-drive',f'if=none,id=d{i},file={ROOT}/out/googlebook-stage/{name}_a.img,format=raw,readonly=on','-device',f'virtio-blk-pci,drive=d{i}']
cmd+=['-drive',f'if=none,id=logs,file={disk},format=raw','-device','virtio-blk-pci,drive=logs']
with (out/'qemu-native-console.log').open('wb') as log:
    p=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT)
    try:p.wait(timeout=args.duration)
    except subprocess.TimeoutExpired:p.terminate();p.wait(timeout=10)
dest=out/'qemu-native-saved';dest.mkdir(exist_ok=True)
subprocess.run(['mcopy','-s','-i',str(disk),'::/logs',str(dest)],check=True)
logs=sorted(dest.glob('logs/googlebook-native-*/kernel-live.txt'),key=lambda p:p.stat().st_mtime)
text=logs[-1].read_text(errors='replace')
console=(out/'qemu-native-console.log').read_text(errors='replace')
assert 'SQ2_NATIVE_HANDOFF' in console, 'Native handoff not reached'
assert 'init second stage started' in text, 'Original Android second-stage init not reached'
subprocess.run([sys.executable,str(ROOT/'scripts/verify-googlebook-native.py')],check=True)
print('PASS: real PID 1 Android second-stage init reached; persistent USB-format log recovered')
print('Saved log:',logs[-1])
print('This does not assert Android framework or desktop boot success.')
