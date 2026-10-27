#!/usr/bin/env python3
"""Boot a disposable physical-layout image twice; prove /data is preserved."""
from pathlib import Path
import argparse, hashlib, json, shutil, subprocess, time
parser=argparse.ArgumentParser(); parser.add_argument('--resume-first',action='store_true'); args=parser.parse_args()
R=Path(__file__).resolve().parents[1]; O=R/'out/ssd'; C=Path('/root/.cache/sq2-probe-build/ssd')
plan=json.loads((O/'layout.json').read_text()); parts={p['name']:p for p in plan['partitions']}
manifest=json.loads((O/'image-manifest.json').read_text())
for name,digest in manifest['boot_payload_sha256'].items():
    assert hashlib.sha256((R/'out/android-test'/name).read_bytes()).hexdigest()==digest
base=C/'googlebook-sq2-ssd.img'; test=C/'qemu-ssd.img'
if args.resume_first:
    assert test.is_file() and (O/'qemu-boot-1.log').is_file()
else:
    assert not test.exists(), 'Archive previous test image explicitly'
    subprocess.run(['cp','--sparse=always','--reflink=auto',str(base),str(test)],check=True)
fat=f"{test}@@{parts['SQ2PROBE']['offset']}"
extracted=O/f'test-initramfs-{time.time_ns()}.cpio.gz'
subprocess.run(['mcopy','-o','-i',fat,'::/boot/sq2-android1/native-combined-initramfs.cpio.gz',str(extracted)],check=True)
assert hashlib.sha256(extracted.read_bytes()).hexdigest()==manifest['boot_payload_sha256']['native-combined-initramfs.cpio.gz']
cfg=O/'qemu-grub.cfg'
cfg.write_text('''set timeout=1
set default=0
menuentry 'SQ2 SSD EFI test' {
    search --no-floppy --fs-uuid --set=root B026-1027
    echo 'SQ2-SSD-EFI-GRUB-OK'
    linux /boot/sq2-android1/Image console=ttyAMA0 loglevel=4 panic=0 sq2.native=ssd-smoke lsm=selinux enforcing=0 printk.devkmsg=on
    initrd /boot/sq2-android1/native-combined-initramfs.cpio.gz
}
''')
subprocess.run(['mcopy','-o','-i',fat,str(cfg),'::/EFI/BOOT/grub.cfg'],check=True)
marker=O/'persistence-token.txt'; marker.write_text('SQ2-SSD-PERSISTENCE-20261027\n')
boots=[]
for iteration in (1,2):
    log=O/f'qemu-boot-{iteration}.log'
    cmd=['qemu-system-aarch64','-machine','virt','-cpu','max','-m','4096','-smp','4',
         '-nographic','-no-reboot','-nic','none','-bios','/usr/share/qemu-efi-aarch64/QEMU_EFI.fd',
         '-device','qemu-xhci','-drive',f'if=none,id=ssd,file={test},format=raw',
         '-device','usb-storage,drive=ssd,serial=SQ2SSDSMOKE']
    print(f'Boot {iteration}: ARM64 UEFI -> GRUB -> physical super -> Android init',flush=True)
    if not (args.resume_first and iteration==1):
        with log.open('wb') as out:
            p=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT)
            deadline=time.monotonic()+240
            while p.poll() is None and time.monotonic()<deadline:
                time.sleep(2)
                current=log.read_text(errors='replace')
                if 'Kernel panic' in current or 'SQ2_NATIVE_PREP_FAILED' in current: break
            if p.poll() is None:p.terminate();p.wait(timeout=10)
    console=log.read_text(errors='replace')
    assert 'SQ2_NATIVE_HANDOFF' in console, console[-6000:]
    dest=O/f'boot-{iteration}-{time.time_ns()}'; dest.mkdir()
    subprocess.run(['mcopy','-o','-s','-i',fat,'::/logs',str(dest)],check=True)
    logs=sorted(dest.glob('logs/googlebook-native-*/kernel-live.txt'),key=lambda p:p.stat().st_mtime)
    # Select by UUID in the serial console, not FAT timestamp granularity.
    bootid=console.split('SQ2_GOOGLEBOOK_NATIVE_PREP bootid=')[1].split()[0]
    logdir=dest/'logs'/('googlebook-native-'+bootid)
    text=(logdir/'kernel-live.txt').read_text(errors='replace')
    props=(logdir/'properties-latest.txt').read_text()
    assert 'init second stage started' in text
    assert 'Activated 45 packages' in text
    assert any("Command 'mount_all --late'" in line and 'succeeded' in line for line in text.splitlines())
    assert '[init.svc.vendor.boot-default]: [running]' in props
    assert '[init.svc.vendor.keymint-default]: [running]' in props
    assert 'InitFatalReboot' not in text
    mounts=(logdir/'security-overlay-after-init.txt').read_text()
    assert ('/dev/sda1 /data ext4' in mounts or '/dev/block/by-name/userdata /data ext4' in mounts
            or 'EXT4-fs (sda1): mounted filesystem' in text), 'Physical userdata mount not observed'
    loop=subprocess.check_output(['losetup','--find','--show','--offset',str(parts['userdata']['offset']),
                                  '--sizelimit',str(parts['userdata']['bytes']),str(test)],text=True).strip()
    try:
        check=subprocess.run(['e2fsck','-p',loop],capture_output=True,text=True)
        assert check.returncode in (0,1),check.stdout+check.stderr
        if iteration==1:
            subprocess.run(['debugfs','-w','-R',f'write {marker} /.sq2-persistence-test',loop],check=True)
        else:
            value=subprocess.check_output(['debugfs','-R','cat /.sq2-persistence-test',loop],text=True)
            assert value==marker.read_text(),'Persistent userdata marker lost'
    finally:subprocess.run(['losetup','-d',loop],check=True)
    boots.append(dict(boot_id=bootid,late_userdata_mount=True,bootstrap_apex=45,bootcontrol=True,keymint=True))
    print(f'PASS boot {iteration}: Android mounted physical userdata',flush=True)
result=dict(physical_layout_boot=True,efi_grub_boot=True,persistent_data_two_boots=True,boots=boots,
            hardware_desktop_verified=False,
            payload_sha256=manifest['boot_payload_sha256'])
(O/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: external physical partition path and persistent userdata survived two UEFI boots',flush=True)
