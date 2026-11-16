import importlib.util
import json
from pathlib import Path
import re
import sys
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
import full_image_suspend as s
spec = importlib.util.spec_from_file_location('sleep_patch', ROOT/'scripts/patch-sq2-venus-sleep-fix.py')
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)


def module():
    data = bytearray(64)
    data[:6] = b'\x7fELF\x02\x01'
    data[18:20] = (183).to_bytes(2, 'little')
    return bytes(data)+b'venus_hfi_prepare_system_stop\0stop:prepare-pc\0sq2_pm_stream_reuses\0vermagic=6.18.60-sq2-android1 SMP\0'


def fixture(directory):
    # Old-side hunk/context fixture, not a complete kernel consumer. Actual
    # complete ARM64 source build is recorded separately in the device report.
    for name in ('venus-s2idle-transport.patch', 'venus-s2idle-quiesce.patch'):
        for part in re.split(r'(?m)(?=^--- a/)', (ROOT/'patches'/name).read_text()):
            if not part.strip():
                continue
            lines = part.splitlines(True)
            path = directory/lines[0].strip().removeprefix('--- a/')
            out = []
            for line in lines[2:]:
                if line.startswith('@@'):
                    start = int(re.match(r'@@ -(\d+)', line)[1])-1
                    while len(out) < start:
                        out.append('// unused fixture line '+str(len(out))+'\n')
                elif line.startswith((' ', '-')):
                    out.append(line[1:])
            path.write_text(''.join(out))


class SleepFix(unittest.TestCase):
    def test_source_patch_idempotent(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            fixture(d)
            self.assertEqual(len(p.apply(d)), 4)
            self.assertIn('venus_hfi_prepare_system_stop(core)', (d/'venus-sq2-system-pm.h').read_text())
            before = {f.name: f.read_bytes() for f in d.iterdir()}
            self.assertEqual(p.apply(d), [])
            self.assertEqual(before, {f.name: f.read_bytes() for f in d.iterdir()})

    def test_unknown_source_rejected_before_write(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            fixture(d)
            (d/'hfi_venus.h').write_text('unknown\n')
            before = (d/'sq2-venus-fw.c').read_bytes()
            with self.assertRaises(ValueError):
                p.apply(d)
            self.assertEqual(before, (d/'sq2-venus-fw.c').read_bytes())

    def test_install_all_module_copies_and_framework_once(self):
        bodies = {'init': b'#!/bin/sh\nexec native-handoff /newroot\n',
                  'lib/modules/test/venus-core.ko': b'old', 'etc/native/venus-core.ko': b'old',
                  'lib/modules/test/venus-dec.ko': b'decoder unchanged'}
        records = {}
        files = {'module': module(), 'jar': b'jar-patched-by-load'}
        report = s.install(records, bodies, files, lambda mode: mode)
        self.assertEqual(len(report['module_paths']), 2)
        self.assertEqual(bodies['lib/modules/test/venus-core.ko'], module())
        self.assertEqual(bodies['etc/native/venus-core.ko'], module())
        self.assertEqual(bodies['lib/modules/test/venus-dec.ko'], b'decoder unchanged')
        self.assertEqual(bodies['init'].count(s.HOOK), 1)
        s.install(records, bodies, files, lambda mode: mode)
        self.assertEqual(bodies['init'].count(s.HOOK), 1)
        self.assertNotIn(b'tracing_on', bodies['init'])
        self.assertNotIn(b'pm_test', bodies['init'])

    def test_old_module_rejected(self):
        with self.assertRaisesRegex(ValueError, 'accepted sleep'):
            s.validate_module(module().replace(b'stop:prepare-pc', b'stop:no-prepare'))

    def test_default_build_wired_without_opt_in(self):
        build = (ROOT/'scripts/full_image.py').read_text()
        self.assertIn("suspend = load_suspend(private/'suspend', bundle/'Image', compiled)", build)
        self.assertIn('suspend=suspend,', build)
        hooks = (ROOT/'scripts/full_image_video.py').read_text()
        self.assertIn('changed.extend(sleep_fix.apply(directory))', hooks)

    def test_policy_preserves_custom_settings_and_no_credentials(self):
        helper = (ROOT/'probe/test2/surface-sleep-defaults').read_text()
        self.assertIn('[ "$screen" = 2147483647 ]', helper)
        self.assertIn('[ "$stay" = 15 ]', helper)
        self.assertIn('[ ! -e "$stamp" ] || exit 0', helper)
        self.assertNotIn('locksettings', helper)
        self.assertNotIn('wake_lock', helper)

    def test_policy_actual_shell_branches(self):
        source = (ROOT/'probe/test2/surface-sleep-defaults').read_text()
        wrapper = 'android() { nsenter -t 1 -m -- /system/bin/"$@"; }'
        fake = r'''android() {
 if [ "$1" = getprop ]; then echo 1; return; fi
 shift
 if [ "$1" = --user ]; then shift 2; fi
 op=$1; key=$3
 if [ "$op" = get ]; then cat "$fixture/$key"; return; fi
 [ "${fail_put:-0}" = 0 ] || return 7
 printf '%s\n' "$4" > "$fixture/$key"
}'''
        self.assertIn(wrapper, source)
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            script = source.replace(wrapper, fake).replace(
                'stamp=/proc/1/root/data/system/surface-sleep-defaults-v1', 'stamp="$fixture/stamp"')
            path = d/'run.sh'
            path.write_text(script)
            def run(screen, stay, fail=False):
                (d/'screen_off_timeout').write_text(str(screen))
                (d/'stay_on_while_plugged_in').write_text(str(stay))
                import os
                env = dict(os.environ, fixture=str(d), fail_put=str(int(fail)))
                result = subprocess.run(['sh', str(path)], env=env, capture_output=True, text=True)
                values = tuple((d/n).read_text().strip() for n in ('screen_off_timeout','stay_on_while_plugged_in'))
                return result, values
            result, values = run(2147483647, 15)
            self.assertEqual((result.returncode, values), (0, ('60000','0')))
            # Once migrated, even an intentional never-sleep selection survives.
            result, values = run(2147483647, 15)
            self.assertEqual((result.returncode, values), (0, ('2147483647','15')))
            (d/'stamp').unlink()
            result, values = run(30000, 3)
            self.assertEqual((result.returncode, values), (0, ('30000','3')))
            (d/'stamp').unlink()
            result, _ = run(2147483647, 15, True)
            self.assertEqual(result.returncode, 7)
            self.assertFalse((d/'stamp').exists())


if __name__ == '__main__':
    unittest.main()
