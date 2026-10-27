#!/usr/bin/env python3
"""Boot the probe on emulated ARM64; this does NOT validate Surface hardware."""
from pathlib import Path
import argparse
import os
import re
import selectors
import shutil
import subprocess
import tempfile
import time
import socket

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--usb', action='store_true', help='Test log saving on a disposable USB image copy')
parser.add_argument('--efi', action='store_true', help='Boot via ARM64 EFI/GRUB with a QEMU-specific test menu')
parser.add_argument('--usb-image', type=Path, default=ROOT / 'out/sq2-probe.img')
parser.add_argument('--partition-offset', type=int, default=1048576)
parser.add_argument('--save-device', default='')
parser.add_argument('--existing-usb', action='store_true', help='Use a separate log for existing-media validation')
parser.add_argument('--probe-dir', type=Path, default=ROOT / 'out/probe')
parser.add_argument('--test2', action='store_true')
parser.add_argument('--wifi-addon', action='store_true')
parser.add_argument('--unified', action='store_true', help='Check test3 services and unified orchestration with an injected failing input phase')
parser.add_argument('--ssh-smoke', action='store_true', help='Exercise actual key authentication, PTY and legacy SCP over QEMU user networking')
parser.add_argument('--identity-file', type=Path, default=Path(os.environ.get('SQ2_SSH_KEY', str(Path.home() / '.ssh/id_ed25519'))), help='Private key matching the public key embedded by the local build')
args = parser.parse_args()
if args.save_device and not re.fullmatch(r'/dev/sd[a-z]+[0-9]+', args.save_device):
    parser.error('Expected a USB partition path such as /dev/sda2')
if args.efi:
    args.usb = True
log_path = ROOT / ('out/probe/qemu-efi-smoke.log' if args.efi else 'out/probe/qemu-usb-smoke.log' if args.usb else 'out/probe/qemu-smoke.log')
if args.existing_usb:
    log_path = ROOT / 'out/probe/qemu-existing-usb-smoke.log'
if args.test2:
    log_path = args.probe_dir / ('qemu-efi-smoke.log' if args.efi else 'qemu-usb-smoke.log' if args.usb else 'qemu-smoke.log')
usb_temp = tempfile.TemporaryDirectory(prefix='sq2-usb-smoke-') if args.usb else None
command = [
    'qemu-system-aarch64', '-machine', 'virt', '-cpu', 'cortex-a72',
    '-m', '2048', '-smp', '2', '-nographic', '-no-reboot', '-nic', 'none',
]
ssh_done=False
if args.ssh_smoke:
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0)); ssh_port=sock.getsockname()[1]
    command=command[:-2]+['-netdev',f'user,id=sshnet,hostfwd=tcp:127.0.0.1:{ssh_port}-:22','-device','virtio-net-pci,netdev=sshnet,romfile=']
if args.efi:
    command += ['-bios', '/usr/share/qemu-efi-aarch64/QEMU_EFI.fd']
else:
    command += ['-kernel', str(args.probe_dir / 'Image'),
                '-initrd', str(args.probe_dir / 'initramfs.cpio.gz'),
                '-append', 'console=ttyAMA0 loglevel=6 panic=0' + (' sq2.test=smoke' if args.test2 else '')]
if args.usb:
    usb_image = Path(usb_temp.name) / 'usb.img'
    if not args.usb_image.is_file():
        raise RuntimeError('USB image must be a regular file')
    subprocess.run(['cp', '--sparse=always', '--reflink=auto', str(args.usb_image), str(usb_image)], check=True)
    if args.efi:
        # QEMU cannot use the Surface DT. Add a test-only menu to this disposable copy.
        config = (ROOT / 'configs/grub.cfg').read_text().replace('set timeout=20', 'set timeout=1').replace('set default=0', 'set default=3')
        config += '''\nmenuentry 'QEMU EFI smoke' {
    search --no-floppy --set=root --file /SQ2-PROBE-MEDIA.txt
    echo 'SQ2-EFI-GRUB-OK'
    linux /boot/sq2-probe/Image console=ttyAMA0 loglevel=6 panic=0
    initrd /boot/sq2-probe/initramfs.cpio.gz
}
'''
        config_path = Path(usb_temp.name) / 'grub.cfg'
        if args.test2:
            config = config.replace('/boot/sq2-probe/', '/boot/sq2-test2/').replace('console=ttyAMA0 loglevel=6 panic=0', 'console=ttyAMA0 loglevel=6 panic=0 sq2.test=smoke')
        if args.wifi_addon:
            config = config.replace('initrd /boot/sq2-test2/initramfs.cpio.gz', 'initrd /boot/sq2-test2/initramfs.cpio.gz /boot/sq2-test2/wifi-addon.cpio.gz')
        config_path.write_text(config)
        subprocess.run(['mcopy', '-o', '-i', f'{usb_image}@@{args.partition_offset}', str(config_path), '::/EFI/BOOT/grub.cfg'], check=True)
    command += ['-device', 'qemu-xhci', '-drive', f'if=none,id=probeusb,file={usb_image},format=raw',
                '-device', 'usb-storage,drive=probeusb,bootindex=1']
process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
selector = selectors.DefaultSelector()
selector.register(process.stdout, selectors.EVENT_READ)
transcript = bytearray()
sent = False
deadline = time.monotonic() + 210
try:
    with log_path.open('wb') as log:
        while time.monotonic() < deadline:
            for key, _ in selector.select(timeout=1):
                block = os.read(key.fileobj.fileno(), 65536)
                if not block:
                    selector.unregister(key.fileobj)
                    continue
                transcript.extend(block)
                log.write(block)
                log.flush()
            if not sent and b'SQ2-PROBE-READY' in transcript:
                extra = 'gpu-probe; input-events all 1; iw --version; ' if args.test2 else ''
                if args.wifi_addon:
                    extra += 'rmtfs -?; wpa_supplicant -v; timeout 2 qrtr-ns -f; '
                if args.unified:
                    extra += ('iptsd --help; iptsd-check-device --help; wifi-services; '
                              'test -d /var/lib/tqftpserv && echo TQFTP_DIRECTORY_OK; '
                              'modprobe cfg80211; sleep 2; dmesg | grep -i wens; '
                              'test -c /dev/uinput && echo UINPUT_NODE_OK; '
                              'uinput-fixture 1 & sleep 1; input-events multitouch 6 > /logs/synthetic-one.txt; echo SYNTHETIC_ONE_RC=$?; wait; '
                              'uinput-fixture 2 & sleep 1; input-events multitouch 6 > /logs/synthetic-two.txt; echo SYNTHETIC_TWO_RC=$?; wait; '
                              'mkdir -p /run/mock; '
                              "printf '#!/bin/busybox sh\\nexit 3\\n' > /run/mock/test-input; "
                              "for p in test-power test-wifi test-gpu; do printf '#!/bin/busybox sh\\nexit 0\\n' > /run/mock/$p; done; "
                              'chmod 755 /run/mock/*; PATH=/run/mock:/bin:/sbin run-tests pending; '
                              'cat /logs/suite.txt; ')
                ending='cat /proc/mounts; save-probe ' + args.save_device + '; poweroff -f\n'
                if args.ssh_smoke:
                    ending='network-up ethernet > /logs/network-up.txt 2>&1; echo NETWORK_UP_RC=$?; cat /logs/network-up.txt; if start-ssh; then echo SQ2-QEMU-SSH-READY; else cat /logs/ssh.txt; poweroff -f; fi\n'
                process.stdin.write(('echo SQ2-SHELL-OK; sleep 3; ' + extra + ending).encode())
                process.stdin.flush()
                sent = True
            if args.ssh_smoke and not ssh_done and b'\r\nSQ2-QEMU-SSH-READY\r\n' in transcript:
                with tempfile.TemporaryDirectory(prefix='sq2-ssh-check-') as directory:
                    tmp=Path(directory); identity=tmp/'identity'
                    shutil.copyfile(args.identity_file,identity)
                    identity.chmod(0o600)
                    opts=['-i',str(identity),'-o','IdentitiesOnly=yes','-o','BatchMode=yes','-o','StrictHostKeyChecking=yes','-o','HostKeyAlias=sq2-probe','-o','UserKnownHostsFile='+str(ROOT/'private/sq2-ssh/known_hosts'),'-o','ConnectTimeout=10']
                    ssh=['ssh',*opts,'-p',str(ssh_port),'root@127.0.0.1']
                    reply=subprocess.check_output([*ssh,'id -u; test -c /dev/uinput && echo SSH_COMMAND_OK'],stderr=subprocess.STDOUT,timeout=20)
                    assert b'SSH_COMMAND_OK' in reply and b'0\n' in reply,reply
                    # Keep stdin open like a real terminal. Inheriting the CI
                    # runner's EOF can hang up the remote PTY before exec.
                    pty_read,pty_write=os.pipe()
                    try:
                        pty=subprocess.check_output(['ssh',*opts,'-p',str(ssh_port),'-tt','root@127.0.0.1','test -t 0 && echo SSH_PTY_OK'],stdin=pty_read,stderr=subprocess.STDOUT,timeout=20)
                    except subprocess.CalledProcessError as exc:
                        print(exc.output.decode(errors='replace'),flush=True)
                        debug=subprocess.run([*ssh,'cat /logs/ssh.txt; ls -l /dev/ptmx; cat /proc/mounts; dmesg | tail -20'],capture_output=True,timeout=20)
                        print(debug.stdout.decode(errors='replace'),flush=True)
                        raise
                    finally:
                        os.close(pty_read);os.close(pty_write)
                    assert b'SSH_PTY_OK' in pty,pty
                    payload=tmp/'payload'; payload.write_bytes(b'SQ2 SSH transfer validation\n')
                    scp=['scp','-O',*opts,'-P',str(ssh_port)]
                    subprocess.run([*scp,str(payload),'root@127.0.0.1:/tmp/scp-check'],check=True,timeout=20,capture_output=True)
                    subprocess.run([*scp,'root@127.0.0.1:/tmp/scp-check',str(tmp/'readback')],check=True,timeout=20,capture_output=True)
                    assert (tmp/'readback').read_bytes()==payload.read_bytes()
                    # Reject a different key; this must never fall back to a password.
                    subprocess.run(['ssh-keygen','-q','-t','ed25519','-N','','-f',str(tmp/'wrong')],check=True)
                    badopts=opts.copy(); badopts[1]=str(tmp/'wrong')
                    rejected=subprocess.run(['ssh',*badopts,'-p',str(ssh_port),'root@127.0.0.1','true'],capture_output=True,timeout=20)
                    assert rejected.returncode!=0 and b'Permission denied (publickey)' in rejected.stderr,rejected.stderr
                    (args.probe_dir/'qemu-ssh-result.txt').write_text('PASS: key login; command execution; interactive PTY; SCP upload/download; wrong key rejected; password auth unavailable\n')
                ssh_done=True
                process.stdin.write(b'echo SSH_FUNCTIONAL_OK; save-probe; poweroff -f\n');process.stdin.flush()
            if process.poll() is not None:
                break
        else:
            raise RuntimeError('QEMU did not finish within 210 seconds')
    # Avoid mistaking the terminal echo of our command for a shell response.
    if b'\r\nSQ2-SHELL-OK\r\n' not in transcript and b'\nSQ2-SHELL-OK\n' not in transcript:
        raise RuntimeError('Diagnostic shell did not execute')
    if args.usb and b'Saved logs to /media/probe/logs/' not in transcript:
        raise RuntimeError('USB log saving did not succeed')
    if args.efi and b'SQ2-EFI-GRUB-OK' not in transcript:
        raise RuntimeError('EFI/GRUB test menu did not execute')
    if args.test2 and (b'RESULT render_nodes=0' not in transcript or b'NO_DEVICE kind=all' not in transcript or b'iw version 6.9' not in transcript):
        raise RuntimeError('Test2 tools did not execute or unexpected emulated hardware')
    if args.wifi_addon and (b'wpa_supplicant v2.11' not in transcript or b'Unknown option:' not in transcript):
        raise RuntimeError('Wi-Fi addon/static and dynamic tools did not execute')
    if args.unified:
        for marker in [b'TQFTP_DIRECTORY_OK',b"X.509 cert 'wens:",b'END multitouch exit=3',b'UINPUT_NODE_OK',b'SYNTHETIC_ONE_RC=3',b'SYNTHETIC_TWO_RC=0',b'TEST FINISHED AND LOGS SAVED',b'The hidraw device node']:
            if marker not in transcript: raise RuntimeError('Unified smoke marker missing: '+repr(marker))
        if b'regulatory.db is malformed' in transcript: raise RuntimeError('Regulatory signature still rejected')
    if not args.usb and b'Expected one USB partition labeled SQ2PROBE; no disk mounted.' not in transcript:
        raise RuntimeError('No-media save guard did not run')
    if b'Invalid ELF header' in transcript or b'Kernel panic' in transcript:
        raise RuntimeError('Kernel/module error in diagnostic boot')
    if b'System halted' not in transcript and b'Power down' not in transcript:
        raise RuntimeError('Guest did not shut down normally')
    if args.usb:
        listing = subprocess.check_output(['mdir', '-i', f'{usb_image}@@{args.partition_offset}', '::/logs'])
        if b'<DIR>' not in listing:
            raise RuntimeError('No saved log directory on the USB image')
        if args.unified:
            folders=re.findall(rb'/media/probe/(logs/test3-pending-[a-f0-9-]+)',bytes(transcript))
            if not folders: raise RuntimeError('Unified logs folder missing')
            summary=subprocess.check_output(['mtype','-i',f'{usb_image}@@{args.partition_offset}','::/'+folders[-1].decode()+'/summary.txt'])
            for marker in [b'END multitouch exit=3',b'revision=c',b'SKIP previously validated:']:
                if marker not in summary: raise RuntimeError('Persisted summary missing '+repr(marker))
            for marker in [b'BEGIN input',b'BEGIN power-rtc',b'BEGIN gpu',b'BEGIN wifi',b'=== touchpad ===']:
                if marker in summary: raise RuntimeError('Verified phase was not skipped: '+repr(marker))
            (args.probe_dir/'qemu-unified-summary.txt').write_bytes(summary)
    if args.ssh_smoke and not ssh_done: raise RuntimeError('SSH functional checks did not execute')
    print(f'PASS: {"EFI/GRUB, " if args.efi else ""}ARM64 boot, diagnostic shell, {"USB log saving" if args.usb else "no-media guard"}, shutdown. Log: {log_path}')
finally:
    selector.close()
    if process.poll() is None:
        process.kill()
    process.wait()
    if usb_temp:
        usb_temp.cleanup()
