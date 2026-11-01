#!/usr/bin/env python3
"""Build the current SQ2's two-entry EL2 boot bundle; never write a device.

Firmware, Qualcomm libraries and the tested DT/EFI handoff are pinned private
inputs. The kernel/modules are existing source-build outputs, not rebuilt here.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zlib

R = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def normal_payload(raw, c, user=None, kernel_headers=None):
    """Keep the working runtime, remove only experiment logging/reboot hooks."""
    before = c.entries(gzip.decompress(raw))
    e = dict(before)
    # The pinned Image keeps its matching headers; make modules regenerates this archive.
    headers_name = 'lib/modules/6.3.8-sq2-android1/kernel/kernel/kheaders.ko'
    if kernel_headers is not None:
        e[headers_name] = kernel_headers
    init = e['init'][1]
    start = init.index(b'if grep -q "sq2.el2=experimental" /proc/cmdline; then\n')
    end = init.index(b'\nfi\n', start) + 4
    block = init[start:end]
    assert b'el2-kmsg-collect' in block and b'el2-android-recovery' in block
    target = b'' if user is None else b' ' + str(user).encode('ascii')
    hook = (b'if grep -qw "sq2.el2=1" /proc/cmdline; then\n'
            b' el2-terminal-autostart' + target + b' > /logs/el2-terminal-autostart.txt 2>&1 &\nfi\n')
    init = init[:start] + hook + init[end:]
    assert all(n not in init for n in (b'el2-kmsg-collect', b'el2-android-recovery', b'el2-logs-bind'))
    subprocess.run(['sh', '-n'], input=init, check=True)
    e['init'] = (0o100755, init)
    name = 'bin/video-decoder-lab-start'
    mode, helper = e[name]
    guard = b'*" sq2.el2=experimental "*)'
    assert helper.count(guard) == 1
    helper = helper.replace(guard, b'*" sq2.el2=experimental "*|*" sq2.el2=1 "*)')
    subprocess.run(['sh', '-n'], input=helper, check=True)
    e[name] = (mode, helper)
    for name in ('el2-rmtfs-start', 'el2-terminal-stage', 'el2-terminal-autostart'):
        body = (R/'probe/virtualization'/name).read_bytes().replace(b'\r\n', b'\n')
        subprocess.run(['sh', '-n'], input=body, check=True)
        e['bin/'+name] = (0o100755, body)
    # Private password addon is preserved byte-for-byte and remains last.
    rest, credential = raw, None
    while rest:
        d = zlib.decompressobj(31)
        members = c.entries(d.decompress(rest))
        assert d.eof
        member = rest[:len(rest)-len(d.unused_data)]
        if 'etc/shadow' in members and 'bin/dropbear' in members:
            credential = member
        rest = d.unused_data
    assert credential
    archive = b''.join(c.entry(n, b, m) for n, (m, b) in e.items()) + c.entry('TRAILER!!!', b'', 0)
    archive += b'\0' * (-len(archive) % 512)
    result = gzip.compress(archive, compresslevel=3, mtime=0) + credential
    assert c.entries(gzip.decompress(result)) == e
    changed = sorted(n for n in e if e[n] != before.get(n))
    assert set(changed) <= {'init', 'bin/el2-rmtfs-start', 'bin/el2-terminal-stage',
                            'bin/el2-terminal-autostart', 'bin/video-decoder-lab-start', headers_name}
    return result, changed


def launcher_source(text, directory, graphics):
    assert directory in ('SQ2Default', 'SQ2Fallback') and graphics in ('closed', 'mesa')
    assert text.count('static CHAR16 options[] =') == 1
    assert 'sq2.el2=experimental' in text and 'sq2.vulkan=mesa' in text
    text = text.replace('SQ2EL2Android', directory)
    text = text.replace('sq2.el2=experimental', 'sq2.el2=1')
    text = text.replace('sq2.vulkan=mesa', 'sq2.vulkan='+graphics).replace(' sq2.el2_trace=1', '')
    assert 'suspend.screen_only=1 nohibernate' in text and 'sq2.display=2.7-60' in text
    return text


def menu():
    return """insmod part_gpt
insmod fat
insmod chain
set timeout=10
set default=sq2-default
menuentry 'SQ2 Android + Linux - Qualcomm / 60Hz / GPU700' --id sq2-default {
 search --no-floppy --fs-uuid --set=root B027-0512
 chainloader /EFI/SQ2Default/el2launch.efi
}
menuentry 'SQ2 working fallback - Mesa / 60Hz / GPU700' --id sq2-fallback {
 search --no-floppy --fs-uuid --set=root B027-0512
 chainloader /EFI/SQ2Fallback/el2launch.efi
}
"""


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, default=R/'out/default-boot')
    p.add_argument('--baseline', type=Path, default=R/'out/default-input')
    p.add_argument('--launcher-source', type=Path, default=R/'out/el2-android/android-el2-v15-full/efi-source')
    p.add_argument('--kernel-build', type=Path, default=Path('/root/.cache/sq2-el2-smr-20261101/build'))
    p.add_argument('--venus-build', type=Path, default=Path('/root/.cache/sq2-venus-no-tz-v4'))
    p.add_argument('--no-tz-overlay', type=Path, default=Path('/root/.cache/sq2-venus-fw-overlay-v1/sq2_fw_overlay.ko'))
    p.add_argument('--passwordless-user', type=int, help='optional existing passwordless lab user; never clears credentials')
    args = p.parse_args()
    pins = json.loads((R/'configs/sq2-default-inputs.json').read_text())
    for name, expected in pins['baseline'].items():
        assert sha((args.baseline/name).read_bytes()) == expected, name
    for name, expected in pins['launcher_source'].items():
        assert sha((args.launcher_source/name).read_bytes()) == expected, name
    assert sha((args.kernel_build/'arch/arm64/boot/Image').read_bytes()) == pins['baseline']['Image']
    assert args.passwordless_user is None or args.passwordless_user >= 0
    args.output.mkdir(parents=True, exist_ok=False)
    shutil.copytree(args.baseline/'firmware', args.output/'firmware')
    cmd = [sys.executable, str(R/'scripts/pack-sq2-el2-android.py'),
           '--kernel-build', str(args.kernel_build), '--venus-build', str(args.venus_build),
           '--no-tz-overlay', str(args.no_tz_overlay), '--output', str(args.output/'payload')]
    if args.passwordless_user is not None:
        cmd += ['--passwordless-user', str(args.passwordless_user)]
    subprocess.run(cmd, check=True)
    spec = importlib.util.spec_from_file_location('cpio', R/'scripts/pack-sq2-vulkan.py')
    c = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(c)
    baseline_entries = c.entries(gzip.decompress((args.baseline/'initramfs.cpio.gz').read_bytes()))
    headers_name = 'lib/modules/6.3.8-sq2-android1/kernel/kernel/kheaders.ko'
    report = {'profile': 'el2-qualcomm-60-700', 'hardware_tested': False, 'entries': {}}
    raw = (args.output/'payload/android-el2.cpio.gz').read_bytes()
    packed, changed = normal_payload(raw, c, args.passwordless_user, baseline_entries[headers_name])
    effective = c.entries(gzip.decompress(packed))
    for name, expected in pins['audio_modules'].items():
        assert sha(effective[name][1]) == expected, 'Audio modules differ from the audible baseline: '+name
    # The accepted audio/runtime is shared; only the graphics launcher differs.
    for directory, graphics in (('SQ2Default', 'closed'), ('SQ2Fallback', 'mesa')):
        out = args.output/'EFI'/directory
        out.mkdir(parents=True)
        for name in ('Image', 'android-el2.dtb', 'qebspilaa64.efi', 'tcblaunch.exe'):
            shutil.copyfile(args.baseline/name, out/name)
        (out/'initramfs.cpio.gz').write_bytes(packed)
        source = args.output/('source-'+directory)
        shutil.copytree(args.launcher_source, source, symlinks=True, ignore=shutil.ignore_patterns('out'))
        f = source/'src/bounce_main.c'
        f.write_text(launcher_source(f.read_text(), directory, graphics))
        with (args.output/(directory+'-build.log')).open('w') as log:
            subprocess.run(['make', '-s', '-C', str(source), '-j8'], stdout=log, stderr=subprocess.STDOUT, check=True)
        shutil.copyfile(source/'out/slbounce.efi', out/'el2launch.efi')
        hashes = {f.name: sha(f.read_bytes()) for f in sorted(out.iterdir())}
        (out/'SHA256SUMS').write_text(''.join(f'{digest}  {name}\n' for name, digest in hashes.items()))
        report['entries'][directory] = {'sha256': hashes, 'graphics': graphics,
                                       'payload_changes': changed,
                                       'baseline_kheaders_sha256': sha(baseline_entries[headers_name][1])}
    (args.output/'grub.cfg').write_text(menu())
    (args.output/'build-audit.json').write_text(json.dumps(report, indent=2)+'\n')
    print('Built two-entry SQ2 bundle:', args.output)


if __name__ == '__main__':
    main()
