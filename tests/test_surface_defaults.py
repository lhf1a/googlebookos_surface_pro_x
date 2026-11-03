import gzip
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    s = importlib.util.spec_from_file_location(name, ROOT/'scripts'/name)
    m = importlib.util.module_from_spec(s)
    s.loader.exec_module(m)
    return m


defaults = load('surface-defaults-payload.py')
cpio = load('pack-sq2-vulkan.py')


def archive(entries):
    return gzip.compress(b''.join(cpio.entry(n, b, m) for n, (m, b) in entries.items())+
                         cpio.entry('TRAILER!!!', b'', 0), mtime=0)


class Defaults(unittest.TestCase):
    def test_density_default_custom_and_invalid_preserves_userdata(self):
        source = (ROOT/'probe/test2/apply-device-info').read_text()
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p/'vendor').mkdir()
            (p/'product/etc').mkdir(parents=True)
            (p/'data').mkdir()
            (p/'data/settings.xml').write_text('display_density_forced=280')
            for args, expected in (([], '320'), (['360'], '360')):
                (p/'vendor/build.prop').write_text('ro.sf.lcd_density=100\nro.vendor.test=keep\n')
                (p/'product/etc/build.prop').write_text('ro.product.product.model=old\nro.sf.lcd_density=200\n')
                # Only the bind mount is substituted; execute the real awk/helper.
                shell = source.replace('mount --bind "$target" "$source"', 'cp "$target" "$source"')
                r = subprocess.run(['sh', '-c', shell, 'density', str(p), *args], capture_output=True)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertIn('ro.sf.lcd_density='+expected, (p/'vendor/build.prop').read_text())
                self.assertIn('ro.vendor.test=keep', (p/'vendor/build.prop').read_text())
                self.assertNotIn('lcd_density', (p/'product/etc/build.prop').read_text())
                self.assertEqual((p/'data/settings.xml').read_text(), 'display_density_forced=280')
            for bad in ['0', '1001', 'bad']:
                r = subprocess.run(['sh', '-c', shell, 'density', str(p), bad], capture_output=True)
                self.assertEqual(r.returncode, 2)

    def fixture(self):
        entries = {'init': (0o100755, b'#!/bin/sh\n'+defaults.ANCHOR),
                   'bin/keep': (0o100755, b'unrelated'),
                   'bin/apply-device-info': (0o100755, b'old')}
        identity = archive({'etc/shadow': (0o100600, b'fixture-not-a-credential'),
                            'bin/dropbear': (0o100755, b'fixture')})
        return archive(entries)+identity, identity

    def test_overlay_custom_off_keeps_identity_and_unrelated(self):
        raw, identity = self.fixture()
        result, audit = defaults.integrate(raw, ROOT, Path('/nonexistent'), cpio, 360, False)
        e = cpio.entries(gzip.decompress(result))
        self.assertIn(b'density=${2:-360}', e['bin/apply-device-info'][1])
        self.assertNotIn('bin/ksu-framework-boot', e)
        self.assertEqual(e['bin/keep'][1], b'unrelated')
        self.assertTrue(result.endswith(identity))
        self.assertEqual(audit['changed'], ['bin/apply-device-info'])

    def test_overlay_pins_fail_closed_and_idempotent(self):
        import hashlib
        from unittest.mock import patch
        raw, identity = self.fixture()
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            for n in defaults.FILES:
                (p/n).write_bytes(n.encode())
            with self.assertRaises(ValueError):
                defaults.integrate(raw, ROOT, p, cpio)
            pins = {n: hashlib.sha256(n.encode()).hexdigest() for n in defaults.FILES}
            with patch.object(defaults, 'FILES', pins):
                result, audit = defaults.integrate(raw, ROOT, p, cpio)
                result2, _ = defaults.integrate(result, ROOT, p, cpio)
                e = cpio.entries(gzip.decompress(result))
                self.assertEqual(e, cpio.entries(gzip.decompress(result2)))
                self.assertEqual(e['init'][1].count(defaults.HOOK), 1)
                self.assertIn(b'density=${2:-320}', e['bin/apply-device-info'][1])
                self.assertTrue(result.endswith(identity))
                self.assertFalse(audit['kernel_replaced'])
                with self.assertRaises(ValueError):
                    defaults.integrate(raw, ROOT, p, cpio, 0)

    def test_standard_and_profile_entry_defaults(self):
        for name in ['build.py', 'build-surface-pro-x.py']:
            text = (ROOT/'scripts'/name).read_text()
            self.assertIn("'--dpi', type=int, default=320", text)
            self.assertIn("'--ksu-manager', choices=('on', 'off'), default='on'", text)
            self.assertIn("'--touch-repair', choices=('on','off'), default='on'", text)
        text = (ROOT/'scripts/build.py').read_text()
        self.assertIn("'--dpi',str(a.dpi),'--ksu-manager',a.ksu_manager,'--ksu-bundle',a.ksu_bundle", text)


if __name__ == '__main__':
    unittest.main()
