"""Exercise the real opt-in pack edit and helper load/failure order."""
from pathlib import Path
from types import SimpleNamespace
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'scripts/pack-sq2-el2-android.py').read_text()
code = source.split('# Begin optional no-TZ payload.', 1)[1].split('\n', 1)[1]
code = code.split('# End optional no-TZ payload.', 1)[0]
original = (ROOT / 'probe/test2/video-decoder-lab-start').read_bytes().replace(b'\r\n', b'\n')

class Commands:
    run = staticmethod(subprocess.run)

    @staticmethod
    def check_output(args, **kwargs):
        if args[1] == '-p':
            return 'sq2_no_tz:bool\n'
        return {'name': 'sq2_fw_overlay\n', 'vermagic': '6.3.8-sq2-android1 SMP\n'}[args[2]]

with tempfile.TemporaryDirectory() as tmp:
    overlay = Path(tmp) / 'sq2_fw_overlay.ko'
    overlay.write_bytes(b'fixture module')
    for enabled in (False, True):
        e = {'bin/video-decoder-lab-start': (0o100755, original)}
        scope = dict(args=SimpleNamespace(no_tz_overlay=overlay if enabled else None),
                     e=e, changed=[], subprocess=Commands, V=Path(tmp))
        exec(code, scope)
        helper = e['bin/video-decoder-lab-start'][1]
        if not enabled:
            assert helper == original and len(e) == 1
            continue
        assert e['etc/sq2-video-native/sq2_fw_overlay.ko'][1] == b'fixture module'
        assert b'sq2_boot_watch' not in helper
        # Simulate the existing environment, preserving shell guards and set -e.
        stubs = r'''
cat() { case "$1" in /proc/cmdline) echo "$CMDLINE";; */screen_only) echo Y;; *) echo boot;; esac; }
uname() { echo 6.3.8-sq2-android1; }
nsenter() { echo 1; }
cd() { :; }
sha256sum() { command cat >/dev/null; }
test() { case "$1" in -d|-e) return 1;; -L|-c) return 0;; *) command test "$@";; esac; }
mkdir() { echo once; }
od() { echo 000000b5; }
insmod() { echo "LOAD $*"; [ "$1" != "$FAIL" ]; }
'''
        helper = helper.decode().replace('> /tmp/sq2-native-pas-attempted', '> /dev/null')
        expected = ['sq2_hfi_overlay.ko', 'sq2_fw_overlay.ko', 'venus-core.ko', 'venus-dec.ko']
        for el2, fail in [(False, ''), (True, ''), (True, expected[0]),
                          (True, expected[1]), (True, expected[2])]:
            cmdline = 'sq2.video.c2=experimental' + (' sq2.el2=experimental' if el2 else '')
            result = subprocess.run(['sh', '-c', stubs + helper], text=True,
                                    capture_output=True, timeout=5,
                                    env={'CMDLINE': cmdline, 'FAIL': fail, 'PATH': '/usr/bin:/bin'})
            loads = [line.split()[1] for line in result.stdout.splitlines() if line.startswith('LOAD ')]
            if not el2:
                assert result.returncode != 0 and not loads, result
            else:
                relevant = [name for name in loads if name in expected]
                assert relevant == (expected[:expected.index(fail)+1] if fail else expected), result
                assert (result.returncode == 0) == (not fail), result
                assert ('SQ2_NATIVE_VIDEO_READY_NOT_FRAME_PROOF' in result.stdout) == (not fail)
        try:
            exec(code, scope)
        except AssertionError:
            pass
        else:
            raise AssertionError('duplicate no-TZ payload accepted')
print('PASS: PAS unchanged; EL2-only overlay-before-core order and fail-closed startup')
