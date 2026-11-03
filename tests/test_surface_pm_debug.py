import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

R = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('pm_debug', R/'scripts/prepare-surface-pm-debug.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def image(extra='', path='AnyBoard'):
    raw = bytearray(512)
    raw[:2] = b'MZ'
    struct.pack_into('<I', raw, 0x3c, 64)
    raw[64:68] = b'PE\0\0'
    struct.pack_into('<H', raw, 68, 0xaa64)
    struct.pack_into('<H', raw, 88, 0x20b)
    options = ('console=tty0 suspend.screen_only=1 nohibernate '
               'suspend.screen_only_autosuspend=1 initrd=\\EFI\\'+path+'\\init.gz '+extra)
    return bytes(raw)+options.encode('utf-16le')+b'\0\0'+b'CODE_UNCHANGED'


class DebugLauncher(unittest.TestCase):
    def test_only_admission_token_changes_for_different_inputs(self):
        for path in ('FirstSq1', 'AnotherSq2'):
            raw = image(path=path)
            out = m.prepare(raw)
            old = 'suspend.screen_only_autosuspend=1'.encode('utf-16le')
            new = ('suspend.screen_only_test=1'+' '*7).encode('utf-16le')
            self.assertEqual(out, raw.replace(old, new))
            self.assertEqual(len(out), len(raw))
            self.assertNotIn(old, out)

    def test_reject_conflicting_gates_and_unprotected_input(self):
        for extra in ('suspend.screen_only_trial=1', 'suspend.screen_only=0',
                      'suspend.screen_only_test=1', 'suspend.autosuspend_tgid=42'):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                m.prepare(image(extra))
        with self.assertRaises(ValueError):
            m.prepare(image().replace('nohibernate'.encode('utf-16le'), 'xohibernate'.encode('utf-16le')))

    def test_reject_signed_wrong_arch_and_malformed_pe(self):
        for offset, fmt, value in ((88+144, '<I', 1), (88+64, '<I', 1),
                                   (68, '<H', 0x8664), (0x3c, '<I', 0xfffffff0)):
            raw = bytearray(image())
            struct.pack_into(fmt, raw, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                m.prepare(bytes(raw))
        for raw in (b'', image()+image()[512:]):
            with self.assertRaises(ValueError):
                m.prepare(raw)


class DebugRunner(unittest.TestCase):
    def test_fail_closed_before_logs_or_power_write(self):
        for changed in ('screen_only', 'screen_only_test', 'screen_only_trial',
                        'screen_only_autosuspend', 'trial_pid', 'autosuspend_tgid',
                        'mem_sleep', 'pm_test', 'invalid_stage', 'missing_tracefs'):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as d:
                root = Path(d)
                params = root/'sys/module/suspend/parameters'
                params.mkdir(parents=True)
                values = dict(screen_only='Y', screen_only_test='Y', screen_only_trial='N',
                              screen_only_autosuspend='N', trial_pid='0', autosuspend_tgid='0')
                if changed in values:
                    values[changed] = {'Y':'N', 'N':'Y', '0':'42'}[values[changed]]
                for name, value in values.items():
                    (params/name).write_text(value+'\n')
                power = root/'sys/power'
                power.mkdir()
                (power/'mem_sleep').write_text('[deep]' if changed == 'mem_sleep' else '[s2idle]')
                (power/'pm_test').write_text('[devices]' if changed == 'pm_test' else '[none]')
                (power/'state').write_text('UNTOUCHED')
                source = (R/'probe/test2/surface-pm-debug').read_text()
                source = source.replace('/sys/', str(root/'sys')+'/')
                source = source.replace('export PATH=/bin:/sbin', 'export PATH=/usr/bin:/bin')
                stage = 'none' if changed == 'invalid_stage' else 'devices'
                log = root/'log'
                result = subprocess.run(['sh', '-s', '--', stage, str(log)], input=source,
                                        text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual((power/'state').read_text(), 'UNTOUCHED')
                self.assertFalse(log.exists())


    def test_cleanup_restores_settings_and_reports_failure(self):
        source = (R/'probe/test2/surface-pm-debug').read_text()
        cleanup = source[source.index('cleanup() {'):source.index('\ntrap cleanup EXIT')]
        for fail in (False, True):
            with self.subTest(fail=fail), tempfile.TemporaryDirectory() as d:
                root = Path(d)
                for name in ('instance', 'power', 'params', 'log'):
                    (root/name).mkdir()
                (root/'instance/trace').write_text('TRACE')
                setup = '\n'.join(f'{name}="{root/name}"' for name in ('instance','power','params','log'))
                stubs = ('\ndmesg() { echo KERNEL; }\nrmdir() { return '+str(int(fail))+'; }\n')
                script = setup + '\ndebug=0\ntimes=0\ndelay=5\n' + stubs + cleanup + '\ntrue\ncleanup\n'
                result = subprocess.run(['sh', '-s'], input=script, text=True, capture_output=True)
                self.assertEqual(result.returncode, int(fail), result.stderr)
                self.assertEqual((root/'power/pm_test').read_text(), 'none\n')
                self.assertEqual((root/'power/pm_debug_messages').read_text(), '0\n')
                self.assertEqual((root/'params/pm_test_delay').read_text(), '5\n')
                self.assertEqual((root/'log/trace.txt').read_text(), 'TRACE')
