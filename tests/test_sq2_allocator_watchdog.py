"""Run the real watchdog script against isolated files and simulated timer changes."""
import hashlib
import itertools
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'probe/video/sq2-allocator-watchdog.sh').read_text()
for mode, scenario in itertools.product(('restore', 'restore-scoped', 'restore-main10'), ('restore', 'unchanged', 'unknown', 'bad-backup')):
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        original, launcher = b'original', b'launcher'
        for name, content in [('target', original), ('backup', original),
                              ('launcher', launcher), ('unknown', b'unknown'),
                              ('guard', b'Y'), ('boot', b'boot'), ('cgroup', b'0::/\n')]:
            (d / name).write_bytes(content)
        if scenario == 'bad-backup':
            (d / 'backup').write_bytes(b'bad')
        script = source
        for old, new in [
            ('/proc/1/root/vendor/bin/hw/android.hardware.graphics.allocator-service.minigbm', str(d/'target')),
            ('/proc/1/root/vendor/bin/hw/sq2-allocator-original', str(d/'backup')),
            ('/proc/1/root/data/local/tmp/sq2-allocator-watch-', str(d/'watch-')),
            ('/sys/module/suspend/parameters/screen_only', str(d/'guard')),
            ('/proc/sys/kernel/random/boot_id', str(d/'boot')),
            ('/proc/self/cgroup', str(d/'cgroup')),
            ('7f394d7082735401e0a2cdc9fbcb010edef49a4d703366a9a73ef295eeb919da', hashlib.sha256(original).hexdigest()),
            ('249da7cae6e999059e0e03a14728b7249681a9c2ecbf57751e8ded4ed276e02e', hashlib.sha256(launcher).hexdigest()),
            ('c859a01459841b8e7a8caff77981dd5d4bd30f13311c0f50e12bc07e6c7416b1', hashlib.sha256(launcher).hexdigest()),
        ]:
            script = script.replace(old, new)
        mutation = ':' if scenario in ('unchanged', 'bad-backup') else f'cp {d / ("launcher" if scenario == "restore" else "unknown")} {d/"target"}'
        mocks = f'''
id() {{ echo 0; }}
stat() {{ case "$3" in /) echo root ;; *) echo android ;; esac; }}
sync() {{ :; }}
sleep() {{ {mutation}; }}
nsenter() {{ echo "$*" >> {d/'events'}; }}
set -- {mode}
'''
        p = subprocess.run(['sh', '-c', mocks + script], capture_output=True, text=True)
        log = (d/f'watch-{mode}-boot/watchdog.log').read_text()
        if scenario == 'restore':
            assert p.returncode == 0 and (d/'target').read_bytes() == original, log
            assert 'ORIGINAL_RESTORED' in log and 'RESTART_REQUESTED' in log
            assert (d/'events').read_text().count('ctl.restart') == 1
        elif scenario == 'unchanged':
            assert p.returncode == 0 and 'ORIGINAL_ALREADY_PRESENT_NO_RESTART' in log
            assert not (d/'events').exists()
        else:
            assert p.returncode != 0 and not (d/'events').exists(), log
        assert not (d/'target.watchdog-new').exists()
        print('WATCHDOG_GUARD_PASS', mode, scenario)
