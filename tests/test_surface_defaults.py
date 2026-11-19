import gzip
import os
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
            for args, expected in (([], '300'), (['360'], '360')):
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
        self.assertEqual(audit['changed'], ['bin/apply-device-info', 'bin/surface-swap-boot', 'init'])
        self.assertEqual(e['init'][1].count(defaults.SWAP_HOOK), 1)
        self.assertEqual(audit['swap_below_12g_gib'], 6)

    def test_overlay_custom_inputs_nonempty_and_idempotent(self):
        raw, identity = self.fixture()
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            for n in defaults.FILES:
                (p/n).write_bytes(('custom-'+n).encode())
            result, audit = defaults.integrate(raw, ROOT, p, cpio)
            result2, _ = defaults.integrate(result, ROOT, p, cpio)
            e = cpio.entries(gzip.decompress(result))
            self.assertEqual(e, cpio.entries(gzip.decompress(result2)))
            self.assertEqual(e['init'][1].count(defaults.HOOK), 1)
            self.assertIn(b'density=${2:-300}', e['bin/apply-device-info'][1])
            for n in defaults.FILES:
                self.assertEqual(e['etc/sq2-ksu/'+n][1], ('custom-'+n).encode())
            self.assertNotIn(b'sha256sum', e['bin/ksu-framework-boot'][1])
            self.assertTrue(result.endswith(identity))
            self.assertFalse(audit['kernel_replaced'])
            for n in defaults.FILES:
                (p/n).write_bytes(b'')
                with self.assertRaises(ValueError):
                    defaults.integrate(raw, ROOT, p, cpio)
                (p/n).write_bytes(('custom-'+n).encode())
            with self.assertRaises(ValueError):
                defaults.integrate(raw, ROOT, p, cpio, 0)

    def test_standard_and_profile_entry_defaults(self):
        # Current boot is preserved as a checked 6.18 bundle, not rebuilt through old overlays.
        for name in ['build.py', 'build-surface-pro-x.py', 'build-source-boot.py', 'build-source-disk.py', 'build-sq2-default.py']:
            text = (ROOT/"scripts"/name).read_text()
            self.assertIn("build-lts.py", text)
            self.assertNotIn("6.3.8", text)


    def test_apply_device_info_sets_poipu_and_ota_build_display_id(self):
        source = (ROOT/'probe/test2/apply-device-info').read_text()
        shell = source.replace('mount --bind "$target" "$source"', 'cp "$target" "$source"')
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            props = {
                'system/build.prop': (
                    'ro.product.system.brand=Microsoft\n'
                    'ro.product.system.device=desktop\n'
                    'ro.product.system.manufacturer=Microsoft\n'
                    'ro.product.system.model=Surface Pro X\n'
                    'ro.product.system.name=desktop_system\n'
                    'ro.system.build.fingerprint=Android/desktop_system/desktop:17/CL3B.260622.273.R1/16471258:user/release-keys\n'
                    'ro.build.id=CL3B.260622.273.R1\n'
                    'ro.build.display.id=CL3B.260622.273.R1\n'
                    'ro.build.flavor=desktop_system-user\n'
                    'ro.build.description=desktop_system-user 17 CL3B.260622.273.R1 16471258 release-keys\n'
                ),
                'vendor/build.prop': (
                    'ro.product.vendor.brand=Microsoft\n'
                    'ro.product.vendor.device=mica\n'
                    'ro.product.vendor.manufacturer=Microsoft\n'
                    'ro.product.vendor.model=Surface Pro X\n'
                    'ro.product.vendor.name=mica\n'
                    'ro.vendor.build.fingerprint=Dell/mica/mica:17/CL3B.260622.273.R1/16471258:user/release-keys\n'
                ),
                'product/etc/build.prop': (
                    'ro.product.product.brand=Microsoft\n'
                    'ro.product.product.device=mica\n'
                    'ro.product.product.manufacturer=Microsoft\n'
                    'ro.product.product.model=Surface Pro X\n'
                    'ro.product.product.name=mica\n'
                    'ro.product.build.fingerprint=Dell/mica/mica:17/CL3B.260622.273.R1/16471258:user/release-keys\n'
                    'ro.build.id=CL3B.260622.273.R1\n'
                    'ro.build.display.id=CL3B.260622.273.R1\n'
                    'ro.build.product=mica\n'
                    'ro.build.flavor=mica-user\n'
                    'ro.build.description=mica-user 17 CL3B.260622.273.R1 16471258 release-keys\n'
                ),
                'vendor/odm/etc/build.prop': (
                    'ro.product.odm.brand=Dell\n'
                    'ro.product.odm.device=mica\n'
                    'ro.product.odm.manufacturer=Dell\n'
                    'ro.product.odm.model=XPS Googlebook\n'
                    'ro.product.odm.name=mica\n'
                    'ro.odm.build.fingerprint=Dell/mica/mica:17/CL3B.260622.273.R1/16471258:user/release-keys\n'
                ),
                'system_ext/etc/build.prop': (
                    'ro.product.system_ext.device=mica\n'
                    'ro.product.system_ext.name=mica\n'
                    'ro.system_ext.build.fingerprint=Dell/mica/mica:17/CL3B.260622.273.R1/16471258:user/release-keys\n'
                ),
                'system_dlkm/etc/build.prop': (
                    'ro.product.system_dlkm.device=mica\n'
                    'ro.product.system_dlkm.name=mica\n'
                    'ro.system_dlkm.build.fingerprint=Dell/mica/mica:17/CL3B.260622.273.R1/16471258:user/release-keys\n'
                ),
                'vendor_dlkm/etc/build.prop': (
                    'ro.product.vendor_dlkm.device=mica\n'
                    'ro.product.vendor_dlkm.name=mica\n'
                    'ro.vendor_dlkm.build.fingerprint=Dell/mica/mica:17/CL3B.260622.273.R1/16471258:user/release-keys\n'
                ),
            }

            def reset_props():
                for rel, body in props.items():
                    fp = root / rel
                    fp.parent.mkdir(parents=True, exist_ok=True)
                    fp.write_text(body)

            # 1) Without SPX_OTA_INFO_FILE: codename -> poipu, ro.build.display.id preserved.
            reset_props()
            env_no_ota = dict(os.environ, SPX_OTA_INFO_FILE=str(root / 'missing-ota-info'))
            r1 = subprocess.run(['sh', '-c', shell, 'device-info', str(root)], env=env_no_ota, capture_output=True, text=True)
            self.assertEqual(r1.returncode, 0, r1.stderr)
            self.assertIn('ro.build.display.id=CL3B.260622.273.R1', (root / 'system/build.prop').read_text())
            self.assertIn('ro.build.display.id=CL3B.260622.273.R1', (root / 'product/etc/build.prop').read_text())

            # 2) With SPX_OTA_INFO_FILE: codename -> poipu, desktop preserved, display.id updated.
            reset_props()
            ota_info = root / 'spx-ota-info'
            ota_info.write_text('SPX_OTA_VERSION=v0.7.6\nSPX_OTA_BUILD_DISPLAY=v0.7.6.261119\n')
            env_ota = dict(os.environ, SPX_OTA_INFO_FILE=str(ota_info))
            r2 = subprocess.run(['sh', '-c', shell, 'device-info', str(root)], env=env_ota, capture_output=True, text=True)
            self.assertEqual(r2.returncode, 0, r2.stderr)

            sys_txt = (root / 'system/build.prop').read_text()
            self.assertIn('ro.product.system.device=desktop\n', sys_txt)
            self.assertIn('ro.product.system.name=desktop_system\n', sys_txt)
            self.assertIn('ro.system.build.fingerprint=Android/desktop_system/desktop:', sys_txt)
            self.assertIn('ro.build.flavor=desktop_system-user\n', sys_txt)
            expected_disp = 'CL3B.260622.273.R1.v0.7.6.261119'
            self.assertIn(f'ro.build.display.id={expected_disp}\n', sys_txt)

            prod_txt = (root / 'product/etc/build.prop').read_text()
            self.assertIn('ro.product.product.device=poipu\n', prod_txt)
            self.assertIn('ro.product.product.name=poipu\n', prod_txt)
            self.assertIn('ro.build.product=poipu\n', prod_txt)
            self.assertIn('ro.build.flavor=poipu-user\n', prod_txt)
            self.assertIn('ro.build.description=poipu-user 17 CL3B.260622.273.R1 16471258 release-keys\n', prod_txt)
            self.assertIn('ro.product.build.fingerprint=Microsoft/poipu/poipu:', prod_txt)
            self.assertIn(f'ro.build.display.id={expected_disp}\n', prod_txt)

            odm_txt = (root / 'vendor/odm/etc/build.prop').read_text()
            self.assertIn('ro.product.odm.brand=Microsoft\n', odm_txt)
            self.assertIn('ro.product.odm.manufacturer=Microsoft\n', odm_txt)
            self.assertIn('ro.product.odm.model=Surface Pro X\n', odm_txt)
            self.assertIn('ro.product.odm.device=poipu\n', odm_txt)
            self.assertIn('ro.product.odm.name=poipu\n', odm_txt)
            self.assertIn('ro.odm.build.fingerprint=Microsoft/poipu/poipu:', odm_txt)

            for rel in ('vendor/build.prop', 'system_ext/etc/build.prop', 'system_dlkm/etc/build.prop', 'vendor_dlkm/etc/build.prop'):
                txt = (root / rel).read_text()
                self.assertNotIn('mica', txt, rel)
                self.assertIn('=poipu\n', txt, rel)
                self.assertIn('Microsoft/poipu/poipu:', txt, rel)

if __name__ == '__main__':
    unittest.main()
