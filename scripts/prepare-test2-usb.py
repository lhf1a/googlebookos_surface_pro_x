#!/usr/bin/env python3
"""Prepare files/manifests and a disposable QEMU image; no physical disk access."""
from pathlib import Path
import hashlib,json,shutil,subprocess
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'out/test2-usb'; efi=out/'efi'; res=out/'resources'
for d in [efi/'EFI/BOOT',efi/'boot/sq2-probe',efi/'boot/sq2-test2',res]: d.mkdir(parents=True,exist_ok=True)
shutil.copyfile(ROOT/'downloads/bootaa64.efi',efi/'EFI/BOOT/BOOTAA64.EFI')
shutil.copyfile(ROOT/'configs/grub-test2.cfg',efi/'EFI/BOOT/grub.cfg')
shutil.copyfile(ROOT/'configs/grub.cfg',efi/'EFI/BOOT/grub-baseline.cfg')
for src,dst in [('probe','sq2-probe'),('test2','sq2-test2')]:
    for f in ['Image','initramfs.cpio.gz','sc8180x-surface-pro-x.dtb','kernel.config']:
        shutil.copyfile(ROOT/'out'/src/f,efi/'boot'/dst/f)
shutil.copyfile(ROOT/'out/test2/wifi-addon.cpio.gz',efi/'boot/sq2-test2/wifi-addon.cpio.gz')
(efi/'SQ2-PROBE-MEDIA.txt').write_text('SQ2 hardware test2 / baseline. Not a Googlebook installer.\n')
instructions='''Surface Pro X SQ2 测试 U 盘

分区：SQ2PROBE（2 GiB FAT32，启动和日志）；SQ2DATA（剩余 exFAT，资源）。
此盘为硬件诊断环境，尚不能直接安装/引导 Googlebook OS。
USB UEFI 启动需要此前测试使用的 Secure Boot 设置。

GRUB 第 1 项：触屏、触控板、电池、RTC。
看到 TOUCHSCREEN 提示后，等待 5 秒，在屏幕上点击、划动及双指操作 30 秒。
看到 TOUCHPAD 提示后，等待 5 秒，在键盘盖触控板上移动、点击及双指操作 30 秒。
看到 POWER TEST 后，30 秒内拔插充电器，保持 U 盘连接。
错过操作窗口可输入 test-input 或 test-power 重做，再输入 save-probe。

第 2 项：GPU 驱动、GPU 参数、显存分配。尚不验证 3D 渲染/桌面显示。
如黑屏，等待至少 150 秒，周期日志仅在内核和 USB 仍正常时有效。
第 3 项：Wi-Fi 固件、网卡、扫描，自动连接 configured lab Wi-Fi 并验证 DHCP、网关、DNS 和外网 IP。
Wi-Fi 项包含额外 modem/DSP 固件，载入较慢；启动后请留出 4 分钟测试。
第 4 项：之前实际成功启动的基线环境；手动输入 save-probe 保存。

每项完成看到 Saved logs 和 TEST FINISHED 后，输入 poweroff -f 关机。
再次 USB 启动选择下一项。测试完成后把 U 盘插回工作站分析。
新测试日志在 SQ2PROBE 的 logs/test2-项目-启动UUID 文件夹。
三个测试项互相独立。内部 NVMe 不会自动挂载。
RTC 仅被读取；系统时间据此校正，不会写回 RTC。

SQ2DATA/recovery/mica-recovery.raw 是现有恢复镜像资源，不是可在 SQ2 安装的成品。
固件来源与哈希见 manifests，脚本/配置及源码归档见 source。
'''
(efi/'TEST-INSTRUCTIONS.txt').write_text(instructions,encoding='utf-8-sig')
(res/'TEST-INSTRUCTIONS.txt').write_text(instructions,encoding='utf-8-sig')
entries=[]
def add_file(src,partition,dest):
    h=hashlib.sha256()
    with src.open('rb') as f:
        for b in iter(lambda:f.read(8*1024*1024),b''):h.update(b)
    entries.append({'source':src.relative_to(ROOT).as_posix(),'partition':partition,'path':dest,'bytes':src.stat().st_size,'sha256':h.hexdigest()})
def add_tree(src,partition,prefix):
    for f in sorted(src.rglob('*')):
        if f.is_file() and '__pycache__' not in f.parts:
            add_file(f,partition,(Path(prefix)/f.relative_to(src)).as_posix())
add_tree(efi,'boot','')
add_tree(res,'data','')
add_file(ROOT/'downloads/mica-recovery.raw','data','recovery/mica-recovery.raw')
add_tree(ROOT/'out/test2/firmware','data','firmware/linux')
add_tree(ROOT/'private/sq2-collection-20261026-remote/windows-root','data','firmware/windows-root')
for p in ['scripts','configs','probe']:add_tree(ROOT/p,'data','source/'+p)
for n in ['surface-kernel-b86f786.tar.gz','busybox-1.36.1.tar.bz2','iw-6.9.tar.xz','libnl-3.11.0.tar.gz','OpenSSH-ARM64-v10.4.0.0.msi']:
    add_file(ROOT/'downloads'/n,'data','source/downloads/'+n)
add_file(ROOT/'downloads/wpa_supplicant-2.11.tar.gz','data','source/downloads/wpa_supplicant-2.11.tar.gz')
add_tree(ROOT/'downloads/test2-arm64-libs','data','source/downloads/arm64-libs')
for name in ['qrtr','tqftpserv','pd-mapper','rmtfs']:
    archive=ROOT/'out/test2'/f'{name}-source.tar.gz'
    subprocess.run(['tar','--exclude=.git','-czf',str(archive),'-C',str(ROOT/'upstream'),name],check=True)
    add_file(archive,'data','source/downloads/'+archive.name)
add_file(ROOT/'out/test2/arm64-libs-manifest.json','data','manifests/arm64-libs-manifest.json')
add_file(ROOT/'out/test2/build-record.json','data','manifests/build-record.json')
for p in ['out/test2/firmware-manifest.json','private/sq2-vendor-firmware-unpatched/provenance.json','out/googlebook-layout.json']:
    add_file(ROOT/p,'data','manifests/'+Path(p).name)
for p in ['README.md','out/surface-first-boot-analysis.txt']: add_file(ROOT/p,'data',Path(p).name)
manifest={'disk_serial':'FC2425D40C3E1','disk_bytes':31457280000,'boot_bytes':2147483648,'files':entries}
(out/'payload-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Payload bytes:',{p:sum(f['bytes'] for f in entries if f['partition']==p) for p in ['boot','data']},flush=True)
image=out/'qemu-test2.img'; fat=Path('/root/.cache/sq2-probe-build/test2-fat.img')
with fat.open('wb') as f:f.truncate(510*1024*1024)
subprocess.run(['mkfs.vfat','-F32','-n','SQ2PROBE',str(fat)],check=True)
subprocess.run(['mcopy','-i',str(fat),'-s',*[str(p) for p in efi.iterdir()],'::/'],check=True)
subprocess.run(['fsck.fat','-n',str(fat)],check=True)
with image.open('wb') as f:f.truncate(512*1024*1024)
subprocess.run(['sgdisk','--clear','--new=1:2048:+510M','--typecode=1:ef00',str(image)],check=True)
subprocess.run(['dd','if='+str(fat),'of='+str(image),'bs=1M','seek=1','conv=notrunc','status=none'],check=True)
