"""Offline fixtures only: no SSH, SIM, NV, network or device access."""
import ast
import gzip
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


c = load('surface_lte_cpio', ROOT/'scripts/pack-sq2-vulkan.py')
m = load('surface_lte', ROOT/'scripts/surface-lte-payload.py')


def archive(files):
    blob = b''.join(c.entry(n, b, mode) for n, (mode, b) in files.items())
    blob += c.entry('TRAILER!!!', b'', 0)
    return gzip.compress(blob + b'\0'*(-len(blob) % 512), mtime=0)


class LteBaseline(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.bundle = Path(self.tmp.name)
        self.files = {
            'init': (0o100755, b'#!/bin/sh\n' + m.HOOKS['init'][0][0] + m.HOOKS['init'][1][0]),
            'bin/wifi-services': (0o100755, b'#!/bin/sh\ndone\nqrtr-ns -f &\n'),
            'etc/sq2-audio/audio-hal': (0o100755, b'UNCHANGED AUDIO'),
            'bin/iptsd': (0o100755, b'UNCHANGED TOUCH'),
            'etc/sq2-ssd-layout': (0o100644, b'OWN STORAGE'),
        }
        self.credentials = archive({'etc/shadow': (0o100600, b'OWN IDENTITY'),
                                    'bin/dropbear': (0o100755, b'OWN SSH')})
        self.base = archive(self.files) + self.credentials
        self.delta = {
            'etc/sq2-lte': (0o40700, b''),
            'etc/sq2-lte/unit-serial': (0o100600, b'FIXTURE-SQ2\n'),
            'etc/sq2-lte/modem_fsg.bin': (0o100600, b'PRIVATE FIXTURE, NOT NV'),
            'etc/sq2-radio': (0o40700, b''),
            'etc/sq2-radio/sq2-radio': (0o100600, b'RADIO FIXTURE'),
        }
        for n in m.HELPERS:
            self.delta[n] = (0o100755, (ROOT/'probe/lte'/n.split('/')[-1]).read_bytes().replace(b'\r\n', b'\n'))
        for n in m.HOOKS:
            self.delta[n] = (self.files[n][0], m.hooked(self.files[n][1], n))
        self.radio = self.base + archive(self.delta) + self.credentials
        self.dt = b'FIXTURE DT'
        (self.bundle/'initramfs.cpio.gz').write_bytes(self.radio)
        (self.bundle/'android-el2.dtb').write_bytes(self.dt)
        self.pins = mock.patch.multiple(m, BASE_SHA=m.sha(self.base), RADIO_SHA=m.sha(self.radio), DT_SHA=m.sha(self.dt))

    def test_delta_preserves_baseline_touch_storage_credentials(self):
        with self.pins:
            delta, dt, source = m.prepare(self.base, ROOT, self.bundle, c)
        # Coordinate/noise overlays and template identity already applied must
        # survive LTE integration; only the explicit init/wifi hooks may differ.
        adapted = self.base + archive({'bin/iptsd': (0o100755, b'NEW TOUCH'),
                                      'etc/sq2-ssd-layout': (0o100644, b'SQ1 STORAGE')}) + self.credentials
        output, audit = m.integrate(adapted, delta, c)
        before, after = c.entries(gzip.decompress(adapted)), c.entries(gzip.decompress(output))
        for n in before.keys() - m.HOOKS.keys():
            self.assertEqual(before[n], after[n], n)
        self.assertTrue(output.endswith(self.credentials))
        self.assertEqual(dt, self.dt)
        self.assertTrue(audit['unrelated_entries_byte_identical'])
        self.assertFalse(source['sq1_lte_validated'])
        for n in ('bin/radio-stage', 'bin/lte-unit-seed'):
            self.assertIn(b'set -eu\nlte-unit-guard || exit 0\n', after[n][1])
        with self.assertRaisesRegex(ValueError, 'already present'):
            m.integrate(output, delta, c)

    def test_reject_unrelated_source_and_tampering(self):
        corrupt = self.radio + archive({'bin/iptsd': (0o100755, b'UNRELATED CHANGE')})
        (self.bundle/'initramfs.cpio.gz').write_bytes(corrupt)
        with self.pins, mock.patch.object(m, 'RADIO_SHA', m.sha(corrupt)):
            with self.assertRaisesRegex(ValueError, 'unrelated baseline'):
                m.prepare(self.base, ROOT, self.bundle, c)
        with self.pins:
            with self.assertRaisesRegex(ValueError, 'Unverified'):
                m.prepare(self.base, ROOT, self.bundle, c)

    def test_missing_input_is_not_silent_feature_drop(self):
        (self.bundle/'initramfs.cpio.gz').unlink()
        with self.pins:
            with self.assertRaisesRegex(ValueError, 'explicitly select --lte off'):
                m.prepare(self.base, ROOT, self.bundle, c)

    def test_default_on_and_entry_specific_common_payloads(self):
        for name in ('build.py', 'build-surface-pro-x.py'):
            tree = ast.parse((ROOT/'scripts'/name).read_text())
            opts = [n for n in ast.walk(tree) if isinstance(n, ast.Call)
                    and n.args and isinstance(n.args[0], ast.Constant) and n.args[0].value == '--lte']
            self.assertEqual(len(opts), 1)
            self.assertEqual(next(k.value.value for k in opts[0].keywords if k.arg == 'default'), 'on')
        standard = (ROOT/'scripts/build.py').read_text()
        self.assertIn("shutil.copyfile(common[entry], dst/'initramfs.cpio.gz')", standard)
        profiles = (ROOT/'scripts/build-surface-pro-x.py').read_text()
        self.assertIn("runtimes = {'Default': packed, 'Fallback': packed}", profiles)
        self.assertIn("runtimes['Default'], lte_audit = lte.integrate", profiles)
        self.assertIn("if machine == 'sq2':", profiles)

    def test_published_ipa_dtb_is_pinned(self):
        self.assertEqual(hashlib.sha256((ROOT/'artifacts/lte/20261102/android-ip4.dtb').read_bytes()).hexdigest(), m.DT_SHA)


class UnitGuard(unittest.TestCase):
    def test_hardware_and_opt_out_guards_independent_of_sleep_policy(self):
        paths={'/proc/cmdline':'sq2.el2=1 nohibernate',
               '/sys/module/suspend/parameters/screen_only':'N',
               '/proc/device-tree/model':'Microsoft Surface Pro X (SQ2)\0'}
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);code=(ROOT/'probe/lte/lte-unit-guard').read_text()
            for path,value in paths.items():
                p=root/path.lstrip('/');p.parent.mkdir(parents=True,exist_ok=True);p.write_text(value);code=code.replace(path,str(p))
            helper=root/'helper';helper.write_text(code)
            def run():return subprocess.run(['sh',str(helper)],capture_output=True).returncode
            for model in ('SQ1','SQ2'):
                (root/'proc/device-tree/model').write_text('Microsoft Surface Pro X ('+model+')\0')
                for flag in ('N','Y'):
                    (root/'sys/module/suspend/parameters/screen_only').write_text(flag);self.assertEqual(run(),0)
            (root/'sys/module/suspend/parameters/screen_only').unlink();self.assertEqual(run(),0)
            for path,bad in [('/proc/device-tree/model','Unsupported board\0'),('/proc/cmdline','sq2.el2=1'),('/proc/cmdline','sq2.el2=1 nohibernate sq2.lte=0'),('/proc/cmdline','sq2.el2=1 nohibernate sq2.rescue=1')]:
                p=root/path.lstrip('/');old=p.read_text();p.write_text(bad);self.assertNotEqual(run(),0,path);p.write_text(old)


if __name__ == '__main__':
    unittest.main()
