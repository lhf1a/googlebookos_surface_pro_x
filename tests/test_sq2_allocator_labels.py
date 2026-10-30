"""Isolated watchdog label tests; never changes a device or waits on a real timer."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1]).read_text() if len(sys.argv) > 1 else (ROOT / 'probe/video/sq2-allocator-watchdog.sh').read_text()
with tempfile.TemporaryDirectory() as tmp:
    d = Path(tmp)
    for name, value in [('target', 'original'), ('backup', 'original'), ('guard', 'Y'), ('boot', 'boot'), ('cgroup', '0::/\n')]:
        (d / name).write_text(value)
    script = source
    for old, new in [
        ('/proc/1/root/vendor/bin/hw/android.hardware.graphics.allocator-service.minigbm', str(d/'target')),
        ('/proc/1/root/vendor/bin/hw/sq2-allocator-original', str(d/'backup')),
        ('/proc/1/root/data/local/tmp/sq2-allocator-watch-', str(d/'watch-')),
        ('/sys/module/suspend/parameters/screen_only', str(d/'guard')),
        ('/proc/sys/kernel/random/boot_id', str(d/'boot')),
        ('/proc/self/cgroup', str(d/'cgroup')),
        ('7f394d7082735401e0a2cdc9fbcb010edef49a4d703366a9a73ef295eeb919da', hashlib.sha256(b'original').hexdigest()),
    ]:
        script = script.replace(old, new)
    mocks = '''id() { echo 0; }
stat() { case "$3" in /) echo root ;; *) echo android ;; esac; }
sleep() { :; }
sync() { :; }
nsenter() { exit 99; }
'''
    codes = []
    for label in ('main10-4k60-a', 'main10-4k60-b', 'main10-4k60-a', '../escape', 'x/y', '', 'a'*49):
        p = subprocess.run(['sh', '-c', mocks + script, 'watchdog-test', 'restore-main10', label], capture_output=True, text=True)
        codes.append(p.returncode)
        print(f'LABEL={label!r} EXIT={p.returncode} STDOUT={p.stdout!r} STDERR={p.stderr!r}')
    expected = [0, 0, 1, 2, 2, 2, 2]
    if codes != expected:
        raise SystemExit(1)
    assert (d/'target').read_text() == 'original'
    print('UNIQUE_LABELS_AND_COLLISION_GUARD_PASS')
