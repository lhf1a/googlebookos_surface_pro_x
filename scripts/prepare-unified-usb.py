#!/usr/bin/env python3
"""Driver-only incremental staging and disposable QEMU image; no disk access."""
from pathlib import Path
import hashlib,json,shutil,subprocess
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'out/test2-usb'; efi=out/'efi'; res=out/'resources'
shutil.copyfile(ROOT/'configs/grub-unified.cfg',efi/'EFI/BOOT/grub.cfg')
for f in ['Image','initramfs.cpio.gz','wifi-addon.cpio.gz','sc8180x-surface-pro-x.dtb','kernel.config']:
    shutil.copyfile(ROOT/'out/test2'/f,efi/'boot/sq2-test2'/f)
instructions=r'''Surface Pro X SQ2 统一驱动测试（test3 revision c，2026-10-26）

GRUB 选择第 1 项 MULTITOUCH + SSH，通常 2–4 分钟。
自动连接已配置的 configured lab Wi-Fi 并启动 SSH，屏幕显示 SSH_READY 和当前 IP。
TOUCHSCREEN：提示后等 5 秒，两根手指同时触摸并滑动屏幕，持续 30 秒。
仅验证 IPTSd 多点触控；跳过已通过的单指触屏、触控板、电池/RTC、GPU 基础命令及 Wi-Fi 联网测试。
不需要操作触控板、拔插充电器；U 盘保持连接。Wi-Fi 连接是 SSH 的前置步骤。

各阶段自动存盘，每 10 秒另存检查点；失败会继续后续阶段。
看到 TEST FINISHED AND LOGS SAVED 后，可保持开机，通过 SSH 继续调试，无需搬回 U 盘。
工作站 PowerShell：.\scripts\Connect-SQ2Linux.ps1 -IPAddress 屏幕显示的IP
用户名 root，只接受工作站已有公钥；脚本固定验证诊断系统主机密钥。
登录后：cat /logs/summary.txt；run-tests pending；save-probe；poweroff -f。
文件传输可用 scp -O（旧 SCP 协议），或 SSH 管道。/tmp 和 /logs 在内存中，重启前运行 save-probe。
网络失败可在本机运行 network-up 后运行 start-ssh。内核/设备树更新仍需重启。
若提示 FINAL SAVE FAILED，请保持 U 盘连接并输入 save-probe 重试。
日志在 SQ2PROBE/logs/test3-pending-启动UUID/，summary.txt 为汇总。
错过触屏窗口可在测试结束后运行 run-tests pending 重跑；它会覆盖本次启动同目录内的测试结果。
完整回归仅在手动执行 run-tests all 时运行，默认不执行。

修复内容：tqftpserv RAM 目录；新版监管数据库签名证书；匹配 26 MiB 设备树区域的 ADSP 固件；Adreno SMMU 标识；IPTSd 3.1.1 与 uinput；区分手指/手写笔。
revision c：启用 INPUT_MISC 和内置 uinput；要求真实双指并发事件才通过；增加密钥认证 SSH、PTY、SCP 和持续 DHCP 续租。
这些修改已完成构建和模拟环境检查，实际 Surface 修复效果仍需这次日志确认。
原先三项测试已合并；第 2 项仅保留原始基线回退，第 3 项关机。
内置 NVMe 不会自动挂载。RTC 仅读取并校正本次系统时间，不写回 RTC。
原有日志保留。本盘仍是 Linux 硬件诊断环境，不是 Googlebook OS 安装盘。
'''
for p in [efi/'TEST-INSTRUCTIONS.txt',res/'TEST-INSTRUCTIONS.txt']:
    p.write_text(instructions,encoding='utf-8-sig')
old=json.loads((out/'driver-payload-manifest.json').read_text(encoding='utf-8-sig'))
for f in old['files']:
    p=ROOT/f['source']; f['bytes']=p.stat().st_size;f['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
old['scope']='Unified test3 driver diagnostics; recovery deferred; existing logs preserved'
(out/'driver-payload-manifest.json').write_text(json.dumps(old,indent=2)+'\n')
fat=Path('/root/.cache/sq2-probe-build/test3-fat.img'); image=out/'qemu-test3.img'
with fat.open('wb') as f:f.truncate(510*1024*1024)
subprocess.run(['mkfs.vfat','-F32','-n','SQ2PROBE',str(fat)],check=True)
subprocess.run(['mcopy','-i',str(fat),'-s',*[str(p) for p in efi.iterdir()],'::/'],check=True)
subprocess.run(['fsck.fat','-n',str(fat)],check=True)
with image.open('wb') as f:f.truncate(512*1024*1024)
subprocess.run(['sgdisk','--clear','--new=1:2048:+510M','--typecode=1:ef00',str(image)],check=True)
subprocess.run(['dd','if='+str(fat),'of='+str(image),'bs=1M','seek=1','conv=notrunc','status=none'],check=True)
print('Prepared unified driver manifest:',len(old['files']),'files')
