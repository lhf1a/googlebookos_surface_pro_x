"""Execute the packer's real runtime hook edits and recovery with isolated I/O."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'scripts/pack-sq2-el2-android.py').read_text()
code = source.split('# Begin lab runtime integration.', 1)[1]
code = code[code.index('\n') + 1:code.index('# End lab runtime integration.')]
helper = (ROOT / 'probe/test2/video-decoder-lab-start').read_bytes().replace(b'\r\n', b'\n')
init = b'#!/bin/sh\nif true; then\n el2-terminal-autostart > /logs/el2-terminal-autostart.txt 2>&1 &\nfi\n'
for user, trace in [(None, None), ('10', b'fixture module')]:
    e = {'init': (0o100755, init), 'bin/video-decoder-lab-start': (0o100755, helper)}
    scope = dict(e=e, args=SimpleNamespace(passwordless_user=user), r=ROOT,
                 trace=trace, changed=[], subprocess=subprocess, hashlib=hashlib)
    exec(code, scope)
    final = e['init'][1]
    assert final.count(b' el2-android-recovery >') == 1
    assert (b'el2-terminal-autostart 10 ' in final) == (user == '10')
    assert final.index(b'el2-terminal-autostart') < final.index(b'el2-android-recovery')
    if trace is None:
        assert e['bin/video-decoder-lab-start'][1] == helper
        assert 'etc/sq2-scm-trace.ko' not in e
    else:
        actual = e['bin/video-decoder-lab-start'][1]
        assert hashlib.sha256(trace).hexdigest().encode() in actual
        assert actual.index(b'mkdir /tmp/sq2-video-start-once') < actual.index(b'insmod /etc/sq2-scm-trace.ko')
        assert actual.index(b"trap 'rmmod sq2_scm_trace' EXIT") < actual.index(b'insmod "$module.ko"')
        assert e['etc/sq2-scm-trace.ko'][1] == trace
    try:
        exec(code, scope)
    except AssertionError:
        pass
    else:
        raise AssertionError('duplicate runtime hook accepted')

recovery = (ROOT / 'probe/virtualization/el2-android-recovery').read_text()
stubs = r'''
sleep() { echo "sleep $*" >> "$FIXTURE/actions"; }
sync() { :; }
ls() { :; }
nsenter() { echo "nsenter $*" >> "$FIXTURE/actions"; return 1; }
reboot() { echo "reboot $*" >> "$FIXTURE/actions"; }
'''
for mode in ('normal', 'guard', 'keep', 'fallback'):
    with tempfile.TemporaryDirectory() as temporary:
        d = Path(temporary)
        (d / 'cmdline').write_text('' if mode == 'normal' else 'sq2.el2=experimental')
        (d / 'guard').write_text('N' if mode == 'guard' else 'Y')
        if mode == 'keep': (d / 'keep').touch()
        code = recovery.replace('/proc/cmdline', str(d / 'cmdline'))
        code = code.replace('/sys/module/suspend/parameters/screen_only', str(d / 'guard'))
        code = code.replace('/run/el2-control-keep', str(d / 'keep'))
        code = code.replace('/logs/el2-terminal.txt', '/dev/null')
        code = code.replace('/sys/class/remoteproc/remoteproc*', str(d / 'absent*'))
        result = subprocess.run(['sh', '-c', stubs + code], capture_output=True, text=True,
                                env=dict(os.environ, FIXTURE=temporary), timeout=5)
        actions = (d / 'actions').read_text() if (d / 'actions').exists() else ''
        assert ('sys.powerctl reboot' in actions) == (mode == 'fallback')
        assert ('reboot -f' in actions) == (mode == 'fallback')
        if mode in ('normal', 'guard'): assert not actions and result.returncode != 0
        else: assert actions.count('sleep 180') == 2 and result.returncode == 0
        assert ('Recovery canceled' in result.stdout) == (mode == 'keep')
print('PASS: hook order, optional observer, duplicate rejection, recovery guards/cancel/fallback')
