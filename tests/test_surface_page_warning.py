import gzip
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
KEY = 'ro.product.build.16k_page.enabled'
HELPER = ROOT/'probe/test2/apply-device-info'


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT/'scripts'/name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PageWarning(unittest.TestCase):
    def test_default_custom_repeat_and_property_precedence(self):
        for existing in ('', KEY+'=true\n'+KEY+'=1\n'):
            with self.subTest(existing=existing), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                files = ['system/build.prop', 'system/etc/build.prop', 'vendor/build.prop',
                         'vendor/etc/build.prop', 'product/etc/build.prop', 'odm/etc/build.prop',
                         'system_ext/etc/build.prop', 'system_dlkm/etc/build.prop', 'vendor_dlkm/etc/build.prop']
                for rel in files:
                    p = root/rel
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.write_text(existing+'ro.product.product.model=old\nro.sf.lcd_density=100\n'
                                 'fixture.keep=yes\nbionic.linker.16kb.app_compat.enabled=false\n')
                (root/'data').mkdir()
                userdata = root/'data/settings.xml'
                userdata.write_text('display_density_forced=280;notification_policy=keep')
                shell = HELPER.read_text().replace('mount --bind "$target" "$source"', 'cp "$target" "$source"')
                for args, density in (([], '300'), (['360'], '360'), (['360'], '360')):
                    p = subprocess.run(['sh', '-c', shell, 'page-warning', str(root), *args], capture_output=True, text=True)
                    self.assertEqual(p.returncode, 0, p.stderr)
                    self.assertEqual([line for rel in files for line in (root/rel).read_text().splitlines()
                                      if line.startswith(KEY+'=')], [KEY+'=false'])
                    self.assertIn('ro.sf.lcd_density='+density, (root/'vendor/build.prop').read_text())
                    for rel in files:
                        self.assertIn('fixture.keep=yes', (root/rel).read_text())
                        self.assertIn('bionic.linker.16kb.app_compat.enabled=false', (root/rel).read_text())
                    self.assertEqual(userdata.read_text(), 'display_density_forced=280;notification_policy=keep')

    def test_payload_with_ksu_on_or_off_preserves_kernel_and_credentials(self):
        c = load('pack-sq2-vulkan.py')
        defaults = load('surface-defaults-payload.py')
        def archive(entries):
            return gzip.compress(b''.join(c.entry(n, b, m) for n, (m, b) in entries.items())+
                                 c.entry('TRAILER!!!', b'', 0), mtime=0)
        identity = archive({'etc/shadow': (0o100600, b'fixture'), 'bin/dropbear': (0o100755, b'fixture')})
        raw = archive({'init': (0o100755, b'#!/bin/sh\n'+defaults.ANCHOR), 'Image': (0o100644, b'kernel-keep')})+identity
        with tempfile.TemporaryDirectory() as td:
            bundle = Path(td)
            for name in defaults.FILES:
                (bundle/name).write_bytes(b'fixture')
            for ksu in (False, True):
                packed, audit = defaults.integrate(raw, ROOT, bundle, c, ksu=ksu)
                e = c.entries(gzip.decompress(packed))
                self.assertEqual(e['bin/apply-device-info'][1], HELPER.read_bytes())
                self.assertIn((KEY+'=false').encode(), e['bin/apply-device-info'][1])
                self.assertEqual(e['Image'][1], b'kernel-keep')
                self.assertTrue(packed.endswith(identity))
                self.assertFalse(audit['kernel_replaced'])


if __name__ == '__main__':
    unittest.main()
