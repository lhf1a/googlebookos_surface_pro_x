#!/usr/bin/env python3
"""Boot the actual guarded Image in QEMU and check rejected sleep requests."""
from pathlib import Path
import gzip
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
build = Path('/root/.cache/sq2-probe-build')
out = root / 'out/screen-only'
with tempfile.TemporaryDirectory(prefix='sq2-screen-only-', dir=build) as temp:
    stage = Path(temp)
    for name in ('bin', 'dev', 'proc', 'sys'):
        (stage / name).mkdir()
    shutil.copy2(build / 'busybox/busybox', stage / 'bin/busybox')
    (stage / 'init').write_text('''#!/bin/busybox sh
export PATH=/bin
/bin/busybox --install -s /bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
guard=/sys/module/suspend/parameters/screen_only
test "$(cat "$guard")" = Y || { echo GUARD_MISSING; poweroff -f; }
if echo N > "$guard"; then echo GUARD_WRITABLE; poweroff -f; fi
for state in freeze mem disk; do
    if echo "$state" > /sys/power/state; then
        echo "UNEXPECTED_SLEEP_SUCCESS=$state"
        poweroff -f
    fi
    echo "REJECTED=$state"
done
echo SQ2_SCREEN_ONLY_PASS
poweroff -f
''')
    (stage / 'init').chmod(0o755)
    spec = subprocess.check_output(['python3', str(root / 'scripts/cpio-list.py'), str(stage)])
    listing = stage / 'cpio.list'
    listing.write_bytes(spec)
    cpio = subprocess.check_output([str(build / 'kernel-android-out/usr/gen_init_cpio'), str(listing)])
    initrd = stage / 'initramfs.gz'
    initrd.write_bytes(gzip.compress(cpio, mtime=0))
    result = subprocess.run([
        'qemu-system-aarch64', '-machine', 'virt', '-cpu', 'cortex-a72',
        '-m', '1024', '-smp', '2', '-nographic', '-no-reboot', '-nic', 'none',
        '-kernel', str(out / 'Image'), '-initrd', str(initrd),
        '-append', 'console=ttyAMA0 panic=-1 suspend.screen_only=1 nohibernate',
    ], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
    (out / 'qemu-screen-only.log').write_bytes(result.stdout)
    text = result.stdout.decode(errors='replace')
    assert result.returncode == 0, text[-4000:]
    for expected in ('REJECTED=freeze', 'REJECTED=mem', 'REJECTED=disk', 'SQ2_SCREEN_ONLY_PASS'):
        assert expected in text, text[-4000:]
    assert 'GUARD_MISSING' not in text and 'GUARD_WRITABLE' not in text
    assert 'UNEXPECTED_SLEEP_SUCCESS' not in text
    assert 'PM: suspend entry' not in text
    print('PASS: read-only guard; freeze/mem/disk rejected before suspend entry')
