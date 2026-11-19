import io
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))

import build_ksu_next as ksun


def make_aarch64_elf(machine=183):
    hdr = bytearray(64)
    hdr[0:4] = b'\x7fELF'
    hdr[4] = 2
    hdr[5] = 1
    hdr[6] = 1
    struct.pack_into('<H', hdr, 18, machine)
    return bytes(hdr) + b'\x00' * 16


def make_apk():
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w') as zf:
        zf.writestr('AndroidManifest.xml', b'manifest')
        zf.writestr('classes.dex', b'dex')
    return buf.getvalue()


class KsuNextUpgradeTests(unittest.TestCase):
    def test_parse_assets_and_validators(self):
        assets = [
            {'name': 'KernelSU_Next_v3.4.2-spoofed_33355-release.apk', 'browser_download_url': 'https://example/spoofed.apk'},
            {'name': 'KernelSU_Next_v3.4.2_33355-release.apk', 'browser_download_url': 'https://example/official.apk'},
        ]
        ver, name, url = ksun.parse_manager_apk_asset(assets)
        self.assertEqual(ver, 33355)
        self.assertEqual(name, 'KernelSU_Next_v3.4.2_33355-release.apk')
        self.assertEqual(url, 'https://example/official.apk')

        self.assertEqual(len(ksun.validate_ksud_bytes(make_aarch64_elf())), 64)
        with self.assertRaisesRegex(ValueError, 'EM_AARCH64'):
            ksun.validate_ksud_bytes(make_aarch64_elf(machine=62))
        self.assertEqual(len(ksun.validate_manager_apk_bytes(make_apk())), 64)

    def test_apply_to_kernel_source_stamps_fallback_version(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            kdir = root / 'kernel'
            (kdir / 'drivers' / 'kernelsu').mkdir(parents=True)
            (kdir / 'drivers' / 'kernelsu' / 'old.c').write_text('old\n')
            (kdir / 'drivers' / 'Makefile').write_text('obj-$(CONFIG_KSU) += kernelsu/\n')
            (kdir / 'drivers' / 'Kconfig').write_text('menu "Drivers"\nsource "drivers/kernelsu/Kconfig"\nendmenu\n')

            ksu_src = root / 'KernelSU-Next'
            (ksu_src / 'kernel').mkdir(parents=True)
            (ksu_src / 'kernel' / 'Kbuild').write_text(
                'KSU_VERSION_FALLBACK := 1\nKSU_VERSION_TAG_FALLBACK := v0.0.1\n'
            )
            (ksu_src / 'kernel' / 'Kconfig').write_text('config KSU\n\ttristate "KSU"\n')
            (ksu_src / 'kernel' / 'core.c').write_text('// v3.4.2\n')

            dst = ksun.apply_to_kernel_source(kdir, ksu_src, tag='v3.4.2', version_code=33355)
            self.assertFalse((dst / 'old.c').exists())
            self.assertTrue((dst / 'core.c').is_file())
            kbuild = (dst / 'Kbuild').read_text()
            self.assertIn('KSU_VERSION_FALLBACK := 33355', kbuild)
            self.assertIn('KSU_VERSION_TAG_FALLBACK := v3.4.2', kbuild)

    def test_ksu_framework_boot_upgrades_existing_ksud_and_manager_once(self):
        script = ROOT / 'probe' / 'test2' / 'ksu-framework-boot'
        subprocess.run(['sh', '-n', str(script)], check=True)
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p / 'run').mkdir()
            (p / 'payload').mkdir()
            (p / 'bin').mkdir()
            (p / 'data' / 'local' / 'tmp').mkdir(parents=True)
            (p / 'data' / 'adb').mkdir(parents=True)
            (p / 'data' / 'adb' / 'ksud').write_text('old-v3.2-ksud')
            (p / 'manager').write_text('old-v3.2-apk')
            (p / 'user0').write_text('enabled')
            (p / 'cmdline').write_text('suspend.screen_only=1')

            (p / 'payload' / 'ksud').write_text('new-v3.4.2-ksud')
            (p / 'payload' / 'manager-verified.apk').write_text('new-v3.4.2-apk')
            (p / 'payload' / 'expected-version').write_text('33355\n')

            source = script.read_text()
            source = source.replace('/proc/cmdline', str(p / 'cmdline'))
            source = source.replace('/run/ksu-bootstrap-lock', str(p / 'run/lock'))
            source = source.replace('/etc/sq2-ksu', str(p / 'payload'))
            source = source.replace('/proc/1/root/data', str(p / 'data'))
            source = source.replace('export PATH=/bin:/sbin', f'export PATH={p / "bin"}:/usr/bin:/bin')

            fake = p / 'bin' / 'nsenter'
            fake.write_text('''#!/bin/sh
while [ "$1" != -- ]; do shift; done
shift
case "$*" in
 */getprop*) echo 1;;
 */cmd*) echo 10;;
 *'pm list packages -u'*) echo package:com.rifsxd.ksunext;;
 *'pm list users'*) printf 'UserInfo{0:Owner:13}\\nUserInfo{10:User:10}\\n';;
 *'pm list packages --user 0'*) echo package:com.rifsxd.ksunext;;
 *'pm list packages --user 10'*) echo package:com.rifsxd.ksunext;;
 *'pm install -r --user 10'*) echo manager-upgrade >> ROOT/actions;;
 *'debug version'*) echo 'Kernel Version: 33355';;
 *' install') echo new-installed > ROOT/data/adb/ksud; echo daemon-upgrade >> ROOT/actions;;
 *) exit 7;;
esac
exit 0
'''.replace('ROOT', str(p)))
            fake.chmod(0o755)

            for _ in range(2):
                subprocess.run(['sh', '-c', source], check=True, capture_output=True)

            self.assertEqual((p / 'actions').read_text(), 'daemon-upgrade\nmanager-upgrade\n')
            self.assertEqual((p / 'data' / 'adb' / '.sq2-ksu-version').read_text().strip(), '33355')

    def test_build_lts_exposes_ksu_upgrade_flags(self):
        res = subprocess.run(
            [sys.executable, str(ROOT / 'scripts/build-lts.py'), 'full-image', '--help'],
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertIn('--ksu-next-src', res.stdout)
        self.assertIn('--skip-ksu-upgrade', res.stdout)


if __name__ == '__main__':
    unittest.main()
