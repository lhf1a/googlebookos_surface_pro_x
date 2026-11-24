import hashlib
import io
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))

import build_device_faker_fork as df_mod
import build_ksu_modules as ksu_mod


def make_aarch64_elf(machine=183):
    hdr = bytearray(64)
    hdr[0:4] = b'\x7fELF'
    hdr[4] = 2
    hdr[5] = 1
    hdr[6] = 1
    struct.pack_into('<H', hdr, 18, machine)
    return bytes(hdr) + b'\x00' * 16


def make_zip(entries):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as zf:
        for name, data in entries.items():
            zf.writestr(name, data)
    return buf.getvalue()


def make_zygisk_next_zip(mod_id='zygisksu', machine=183, version_code=880):
    return make_zip({
        'module.prop': f'id={mod_id}\nname=Zygisk Next\nversion=1.5.3\nversionCode={version_code}\n'.encode(),
        'customize.sh': b'#!/system/bin/sh\n',
        'lib/arm64-v8a/libzygisk.so': make_aarch64_elf(machine),
        'bin/arm64-v8a/zygiskd': make_aarch64_elf(machine),
    })


def make_game_opt_zip(mod_id='sq2-game-opt', machine=183, version_code=100):
    return make_zip({
        'module.prop': f'id={mod_id}\nname=SQ2 UE/Unity Game Optimizer\nversion=v1.0.0\nversionCode={version_code}\n'.encode(),
        'customize.sh': b'#!/system/bin/sh\n',
        'bin/sq2-game-optd': make_aarch64_elf(machine),
        'zygisk/arm64-v8a.so': make_aarch64_elf(machine),
    })


def make_device_faker_zip(mod_id='device_faker', with_lkm=True, version_code=20261119):
    entries = {
        'module.prop': f'id={mod_id}\nname=Device Faker GFX\nversion=v1.7.0\nversionCode={version_code}\nauthor=GitHub@lhf1a\n'.encode(),
        'customize.sh': b'#!/system/bin/sh\n',
        'uninstall.sh': b'#!/system/bin/sh\n',
        'zygisk/arm64-v8a.so': make_aarch64_elf(),
    }
    if with_lkm:
        entries['lkm/df_gpu_vfs.ko'] = make_aarch64_elf()
    return make_zip(entries)


class DefaultKsuModulesTests(unittest.TestCase):
    def test_validate_zygisk_next_and_game_opt_zips(self):
        zn = make_zygisk_next_zip()
        sha, props = ksu_mod.validate_zygisk_next_zip(zn)
        self.assertEqual(sha, hashlib.sha256(zn).hexdigest())
        self.assertEqual(props['id'], 'zygisksu')

        with self.assertRaisesRegex(ValueError, 'expected id=zygisksu'):
            ksu_mod.validate_zygisk_next_zip(make_zygisk_next_zip(mod_id='other'))

        with self.assertRaisesRegex(ValueError, 'EM_AARCH64'):
            ksu_mod.validate_zygisk_next_zip(make_zygisk_next_zip(machine=62))

        go = make_game_opt_zip()
        go_sha, go_props = ksu_mod.validate_game_opt_zip(go)
        self.assertEqual(go_sha, hashlib.sha256(go).hexdigest())
        self.assertEqual(go_props['id'], 'sq2-game-opt')

        with self.assertRaisesRegex(ValueError, 'expected id=sq2-game-opt'):
            ksu_mod.validate_game_opt_zip(make_game_opt_zip(mod_id='wrong'))

    def test_device_faker_defaults_to_latest_main_without_commit_lock(self):
        self.assertFalse(hasattr(df_mod, 'FORK_COMMIT'))
        self.assertEqual(df_mod.FORK_BRANCH, 'main')

        fake_zip = make_device_faker_zip(with_lkm=True)
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            fake_src = root / 'src'
            fake_src.mkdir()
            out_zip = root / 'out.zip'
            kdir = root / 'kernel-build'
            kdir.mkdir()

            with (
                mock.patch.object(df_mod, 'clone_or_update_fork', return_value=fake_src) as clone_mock,
                mock.patch.object(df_mod, 'build_from_source', return_value=fake_zip) as build_mock,
                mock.patch.object(df_mod, '_read_git_head', return_value='deadbeef'),
            ):
                data, meta = df_mod.resolve_or_build_device_faker_zip(
                    out_zip=out_zip,
                    allow_fetch=True,
                    kernel_dir=kdir,
                    cross_compile='aarch64-linux-gnu-',
                )
            self.assertEqual(data, fake_zip)
            self.assertEqual(meta['source'], 'fork-git-main-build')
            self.assertEqual(meta['fork_branch'], 'main')
            self.assertEqual(meta['fork_commit'], 'deadbeef')
            self.assertTrue(meta['lkm_bundled'])
            clone_mock.assert_called_once_with(df_mod.DEFAULT_SRC_DIR, df_mod.FORK_URL, branch='main')
            build_mock.assert_called_once_with(
                fake_src, out_zip=out_zip, kernel_dir=kdir, cross_compile='aarch64-linux-gnu-'
            )

    def test_resolve_default_ksu_modules_orders_zygisk_game_opt_device_faker(self):
        zn = make_zygisk_next_zip()
        go = make_game_opt_zip()
        df = make_device_faker_zip()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            zn_p = root / 'zn.zip'
            go_p = root / 'go.zip'
            df_p = root / 'df.zip'
            zn_p.write_bytes(zn)
            go_p.write_bytes(go)
            df_p.write_bytes(df)

            modules, meta = ksu_mod.resolve_default_ksu_modules(
                zygisk_next_zip=zn_p,
                game_opt_zip=go_p,
                device_faker_zip=df_p,
                allow_fetch=False,
            )
            self.assertEqual(
                sorted(modules.keys()),
                ['10-zygisk-next.zip', '20-sq2-game-opt.zip', '30-device_faker.zip'],
            )
            self.assertEqual(meta['zygisk_next']['version'], '1.5.3')
            self.assertEqual(meta['sq2_game_opt']['version'], 'v1.0.0')
            self.assertTrue(meta['device_faker_fork']['lkm_bundled'])

    def test_sq2_ksu_modules_boot_installs_in_order_and_enables_all(self):
        script = ROOT / 'probe' / 'test2' / 'sq2-ksu-modules-boot'
        subprocess.run(['sh', '-n', str(script)], check=True)

        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            mock_bin = root / 'mockbin'
            mock_bin.mkdir()
            log_file = root / 'ksud.log'
            nsenter_mock = mock_bin / 'nsenter'
            nsenter_mock.write_text(
                f"""#!/bin/sh
while [ $# -gt 0 ]; do
    case "$1" in
        --) shift; break;;
        *) shift;;
    esac
done
if [ "$1" = /system/bin/getprop ] && [ "$2" = sys.boot_completed ]; then
    echo 1
    exit 0
fi
if [ "$1" = /data/adb/ksud ] && [ "$2" = module ] && [ "$3" = install ]; then
    mod_id=$(unzip -p "{root}/data/local/tmp/sq2-ksu-module.zip" module.prop | sed -n 's/^id=//p' | head -n 1)
    echo "INSTALL:$mod_id" >> "{log_file}"
    mkdir -p "{root}/data/adb/modules_update/$mod_id"
    touch "{root}/data/adb/modules_update/$mod_id/disable"
    exit 0
fi
exit 1
"""
            )
            nsenter_mock.chmod(0o755)
            for tool in ('grep', 'mkdir', 'rmdir', 'seq', 'sleep', 'timeout', 'test', 'unzip', 'sed', 'tr', 'head', 'cp', 'rm', 'cat', 'touch'):
                for cand in (Path('/bin') / tool, Path('/usr/bin') / tool):
                    if cand.exists():
                        (mock_bin / tool).symlink_to(cand)
                        break

            cmdline = root / 'cmdline'
            cmdline.write_text('root=/dev/ram0 sq2.vulkan=qcom\n')
            mod_dir = root / 'etc' / 'sq2-ksu-modules'
            mod_dir.mkdir(parents=True)
            (mod_dir / '10-zygisk-next.zip').write_bytes(make_zygisk_next_zip())
            (mod_dir / '20-sq2-game-opt.zip').write_bytes(make_game_opt_zip())
            (mod_dir / '30-device_faker.zip').write_bytes(make_device_faker_zip())

            data_dir = root / 'data'
            (data_dir / 'adb').mkdir(parents=True)
            ksud = data_dir / 'adb' / 'ksud'
            ksud.write_text('#!/bin/sh\n')
            ksud.chmod(0o755)

            env = dict(
                os.environ,
                SQ2_TEST_PATH=f'{mock_bin}:/bin:/usr/bin',
                SQ2_CMDLINE=str(cmdline),
                SQ2_KSU_MODULES_LOCK=str(root / 'lock'),
                SQ2_KSU_MODULES_DIR=str(mod_dir),
                SQ2_DATA_DIR=str(data_dir),
                SQ2_KSU_EXPECTED_VERSION_FILE=str(root / 'nonexistent-expected-ver'),
                SQ2_BOOT_POLL_RETRIES='2',
                SQ2_BOOT_POLL_SLEEP='0',
                SQ2_KSUD_POLL_RETRIES='2',
                SQ2_KSUD_POLL_SLEEP='0',
            )
            res = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True, check=True)
            self.assertEqual(
                log_file.read_text().strip().splitlines(),
                ['INSTALL:zygisksu', 'INSTALL:sq2-game-opt', 'INSTALL:device_faker'],
            )
            self.assertEqual(
                res.stdout.strip().splitlines(),
                [
                    'KSU_MODULE_INSTALLED:zygisksu',
                    'KSU_MODULE_INSTALLED:sq2-game-opt',
                    'KSU_MODULE_INSTALLED:device_faker',
                ],
            )
            for mod_id in ('zygisksu', 'sq2-game-opt', 'device_faker'):
                self.assertFalse((data_dir / 'adb' / 'modules_update' / mod_id / 'disable').exists())

    def test_sq2_ksu_modules_boot_upgrades_older_and_skips_current(self):
        script = ROOT / 'probe' / 'test2' / 'sq2-ksu-modules-boot'
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            mock_bin = root / 'mockbin'
            mock_bin.mkdir()
            log_file = root / 'ksud.log'
            nsenter_mock = mock_bin / 'nsenter'
            nsenter_mock.write_text(
                f"""#!/bin/sh
while [ $# -gt 0 ]; do
    case "$1" in
        --) shift; break;;
        *) shift;;
    esac
done
if [ "$1" = /system/bin/getprop ] && [ "$2" = sys.boot_completed ]; then
    echo 1
    exit 0
fi
if [ "$1" = /data/adb/ksud ] && [ "$2" = module ] && [ "$3" = install ]; then
    mod_id=$(unzip -p "{root}/data/local/tmp/sq2-ksu-module.zip" module.prop | sed -n 's/^id=//p' | head -n 1)
    echo "INSTALL:$mod_id" >> "{log_file}"
    mkdir -p "{root}/data/adb/modules_update/$mod_id"
    unzip -p "{root}/data/local/tmp/sq2-ksu-module.zip" module.prop > "{root}/data/adb/modules_update/$mod_id/module.prop"
    exit 0
fi
exit 1
"""
            )
            nsenter_mock.chmod(0o755)
            for tool in ('grep', 'mkdir', 'rmdir', 'seq', 'sleep', 'timeout', 'test', 'unzip', 'sed', 'tr', 'head', 'cp', 'rm', 'cat', 'touch'):
                for cand in (Path('/bin') / tool, Path('/usr/bin') / tool):
                    if cand.exists():
                        (mock_bin / tool).symlink_to(cand)
                        break

            cmdline = root / 'cmdline'
            cmdline.write_text('root=/dev/ram0\n')
            mod_dir = root / 'etc' / 'sq2-ksu-modules'
            mod_dir.mkdir(parents=True)
            (mod_dir / '10-zygisk-next.zip').write_bytes(make_zygisk_next_zip())
            (mod_dir / '20-sq2-game-opt.zip').write_bytes(make_game_opt_zip())
            (mod_dir / '30-device_faker.zip').write_bytes(make_device_faker_zip())

            data_dir = root / 'data'
            (data_dir / 'adb' / 'modules' / 'zygisksu').mkdir(parents=True)
            (data_dir / 'adb' / 'modules' / 'zygisksu' / 'module.prop').write_text('id=zygisksu\nversionCode=864\n')
            (data_dir / 'adb' / 'modules' / 'sq2-game-opt').mkdir(parents=True)
            (data_dir / 'adb' / 'modules' / 'sq2-game-opt' / 'module.prop').write_text('id=sq2-game-opt\nversionCode=100\n')
            (data_dir / 'adb' / 'modules' / 'device_faker').mkdir(parents=True)
            (data_dir / 'adb' / 'modules' / 'device_faker' / 'module.prop').write_text('id=device_faker\nversionCode=20261109\n')
            ksud = data_dir / 'adb' / 'ksud'
            ksud.write_text('#!/bin/sh\n')
            ksud.chmod(0o755)
            expected_ver = root / 'expected-version'
            expected_ver.write_text('33355\n')
            (data_dir / 'adb' / '.sq2-ksu-version').write_text('33355\n')

            env = dict(
                os.environ,
                SQ2_TEST_PATH=f'{mock_bin}:/bin:/usr/bin',
                SQ2_CMDLINE=str(cmdline),
                SQ2_KSU_MODULES_LOCK=str(root / 'lock'),
                SQ2_KSU_MODULES_DIR=str(mod_dir),
                SQ2_DATA_DIR=str(data_dir),
                SQ2_KSU_EXPECTED_VERSION_FILE=str(expected_ver),
                SQ2_BOOT_POLL_RETRIES='2',
                SQ2_BOOT_POLL_SLEEP='0',
                SQ2_KSUD_POLL_RETRIES='2',
                SQ2_KSUD_POLL_SLEEP='0',
            )
            res = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True, check=True)
            # zygisksu (880 > 864) and device_faker (20261119 > 20261109) upgrade; sq2-game-opt (100 == 100) skipped.
            self.assertEqual(
                log_file.read_text().strip().splitlines(),
                ['INSTALL:zygisksu', 'INSTALL:device_faker'],
            )
            self.assertEqual(
                res.stdout.strip().splitlines(),
                ['KSU_MODULE_UPGRADED:zygisksu', 'KSU_MODULE_UPGRADED:device_faker'],
            )
            # Second run must be a no-op because modules_update already matches bundled versionCode.
            res2 = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True, check=True)
            self.assertEqual(res2.stdout.strip(), '')

    def test_sq2_ksu_modules_boot_reinstalls_on_lkm_vermagic_mismatch(self):
        script = ROOT / 'probe' / 'test2' / 'sq2-ksu-modules-boot'
        subprocess.run(['sh', '-n', str(script)], check=True)
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            mock_bin = root / 'mockbin'
            mock_bin.mkdir()
            log_file = root / 'ksud.log'
            nsenter_mock = mock_bin / 'nsenter'
            nsenter_mock.write_text(
                f"""#!/bin/sh
while [ $# -gt 0 ]; do
    case "$1" in
        --) shift; break;;
        *) shift;;
    esac
done
if [ "$1" = /system/bin/getprop ] && [ "$2" = sys.boot_completed ]; then
    echo 1
    exit 0
fi
if [ "$1" = /data/adb/ksud ] && [ "$2" = module ] && [ "$3" = install ]; then
    mod_id=$(unzip -p "{root}/data/local/tmp/sq2-ksu-module.zip" module.prop | sed -n 's/^id=//p' | head -n 1)
    echo "INSTALL:$mod_id" >> "{log_file}"
    mkdir -p "{root}/data/adb/modules_update/$mod_id/lkm"
    unzip -p "{root}/data/local/tmp/sq2-ksu-module.zip" module.prop > "{root}/data/adb/modules_update/$mod_id/module.prop"
    unzip -p "{root}/data/local/tmp/sq2-ksu-module.zip" lkm/df_gpu_vfs.ko > "{root}/data/adb/modules_update/$mod_id/lkm/df_gpu_vfs.ko"
    exit 0
fi
exit 1
"""
            )
            nsenter_mock.chmod(0o755)
            for tool in ('grep', 'mkdir', 'rmdir', 'seq', 'sleep', 'timeout', 'test', 'unzip', 'sed', 'tr', 'head', 'cp', 'rm', 'cat', 'touch'):
                for cand in (Path('/bin') / tool, Path('/usr/bin') / tool):
                    if cand.exists():
                        (mock_bin / tool).symlink_to(cand)
                        break

            cmdline = root / 'cmdline'
            cmdline.write_text('root=/dev/ram0\n')
            mod_dir = root / 'etc' / 'sq2-ksu-modules'
            mod_dir.mkdir(parents=True)
            df_zip = make_zip({
                'module.prop': b'id=device_faker\nname=Device Faker GFX\nversion=v1.7.0\nversionCode=20261119\n',
                'customize.sh': b'#!/system/bin/sh\n',
                'uninstall.sh': b'#!/system/bin/sh\n',
                'zygisk/arm64-v8a.so': make_aarch64_elf(),
                'lkm/df_gpu_vfs.ko': make_aarch64_elf() + b'\x00vermagic=6.18.60-sq2-android1 SMP preempt mod_unload aarch64\x00',
            })
            (mod_dir / '30-device_faker.zip').write_bytes(df_zip)

            data_dir = root / 'data'
            installed_lkm = data_dir / 'adb' / 'modules' / 'device_faker' / 'lkm'
            installed_lkm.mkdir(parents=True)
            (installed_lkm.parent / 'module.prop').write_text('id=device_faker\nversionCode=20261119\n')
            (installed_lkm / 'df_gpu_vfs.ko').write_bytes(
                make_aarch64_elf() + b'\x00vermagic=6.18.60-sq2-android1 SMP mod_unload aarch64\x00'
            )
            ksud = data_dir / 'adb' / 'ksud'
            ksud.write_text('#!/bin/sh\n')
            ksud.chmod(0o755)

            env = dict(
                os.environ,
                SQ2_TEST_PATH=f'{mock_bin}:/bin:/usr/bin',
                SQ2_CMDLINE=str(cmdline),
                SQ2_KSU_MODULES_LOCK=str(root / 'lock'),
                SQ2_KSU_MODULES_DIR=str(mod_dir),
                SQ2_DATA_DIR=str(data_dir),
                SQ2_KSU_EXPECTED_VERSION_FILE=str(root / 'nonexistent-expected-ver'),
                SQ2_BOOT_POLL_RETRIES='2',
                SQ2_BOOT_POLL_SLEEP='0',
                SQ2_KSUD_POLL_RETRIES='2',
                SQ2_KSUD_POLL_SLEEP='0',
            )
            res = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True, check=True)
            self.assertEqual(log_file.read_text().strip().splitlines(), ['INSTALL:device_faker'])
            self.assertEqual(res.stdout.strip().splitlines(), ['KSU_MODULE_REINSTALLED_ABI:device_faker'])

            res2 = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True, check=True)
            self.assertEqual(res2.stdout.strip(), '')

    def test_sq2_ksu_modules_boot_reinstalls_on_bin_sha_mismatch(self):
        script = ROOT / 'probe' / 'test2' / 'sq2-ksu-modules-boot'
        subprocess.run(['sh', '-n', str(script)], check=True)
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            mock_bin = root / 'mockbin'
            mock_bin.mkdir()
            log_file = root / 'ksud.log'
            nsenter_mock = mock_bin / 'nsenter'
            nsenter_mock.write_text(
                f"""#!/bin/sh
while [ $# -gt 0 ]; do
    case "$1" in
        --) shift; break;;
        *) shift;;
    esac
done
if [ "$1" = /system/bin/getprop ] && [ "$2" = sys.boot_completed ]; then
    echo 1
    exit 0
fi
if [ "$1" = /data/adb/ksud ] && [ "$2" = module ] && [ "$3" = install ]; then
    mod_id=$(unzip -p "{root}/data/local/tmp/sq2-ksu-module.zip" module.prop | sed -n 's/^id=//p' | head -n 1)
    echo "INSTALL:$mod_id" >> "{log_file}"
    mkdir -p "{root}/data/adb/modules_update/$mod_id/bin"
    unzip -p "{root}/data/local/tmp/sq2-ksu-module.zip" module.prop > "{root}/data/adb/modules_update/$mod_id/module.prop"
    unzip -p "{root}/data/local/tmp/sq2-ksu-module.zip" bin/sq2-game-optd > "{root}/data/adb/modules_update/$mod_id/bin/sq2-game-optd"
    exit 0
fi
exit 1
"""
            )
            nsenter_mock.chmod(0o755)
            for tool in ('grep', 'mkdir', 'rmdir', 'seq', 'sleep', 'timeout', 'test', 'unzip', 'sed', 'tr', 'head', 'cp', 'rm', 'cat', 'touch', 'cmp'):
                for cand in (Path('/bin') / tool, Path('/usr/bin') / tool):
                    if cand.exists():
                        (mock_bin / tool).symlink_to(cand)
                        break

            cmdline = root / 'cmdline'
            cmdline.write_text('root=/dev/ram0\n')
            mod_dir = root / 'etc' / 'sq2-ksu-modules'
            mod_dir.mkdir(parents=True)
            new_bin = make_aarch64_elf() + b'NEW_GAME_OPTD_BINARY'
            go_zip = make_zip({
                'module.prop': b'id=sq2-game-opt\nname=SQ2 Game Opt\nversion=v1.0.1\nversionCode=101\n',
                'customize.sh': b'#!/system/bin/sh\n',
                'bin/sq2-game-optd': new_bin,
                'zygisk/arm64-v8a.so': make_aarch64_elf(),
            })
            (mod_dir / '20-sq2-game-opt.zip').write_bytes(go_zip)

            data_dir = root / 'data'
            installed_bin = data_dir / 'adb' / 'modules' / 'sq2-game-opt' / 'bin'
            installed_bin.mkdir(parents=True)
            (installed_bin.parent / 'module.prop').write_text('id=sq2-game-opt\nversionCode=101\n')
            (installed_bin / 'sq2-game-optd').write_bytes(make_aarch64_elf() + b'OLD_GAME_OPTD_BINARY')
            ksud = data_dir / 'adb' / 'ksud'
            ksud.write_text('#!/bin/sh\n')
            ksud.chmod(0o755)

            env = dict(
                os.environ,
                SQ2_TEST_PATH=f'{mock_bin}:/bin:/usr/bin',
                SQ2_CMDLINE=str(cmdline),
                SQ2_KSU_MODULES_LOCK=str(root / 'lock'),
                SQ2_KSU_MODULES_DIR=str(mod_dir),
                SQ2_DATA_DIR=str(data_dir),
                SQ2_KSU_EXPECTED_VERSION_FILE=str(root / 'nonexistent-expected-ver'),
                SQ2_BOOT_POLL_RETRIES='2',
                SQ2_BOOT_POLL_SLEEP='0',
                SQ2_KSUD_POLL_RETRIES='2',
                SQ2_KSUD_POLL_SLEEP='0',
            )
            res = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True, check=True)
            self.assertEqual(log_file.read_text().strip().splitlines(), ['INSTALL:sq2-game-opt'])
            self.assertEqual(res.stdout.strip().splitlines(), ['KSU_MODULE_REINSTALLED_BIN:sq2-game-opt'])

            res2 = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True, check=True)
            self.assertEqual(res2.stdout.strip(), '')
            import runtime_boot_policy as rbp_mod
            rbp_mod.apply({'bin/sq2-ksu-modules-boot': script.read_bytes()})

    def test_build_lts_full_image_cli_exposes_default_ksu_module_flags(self):
        res = subprocess.run(
            [sys.executable, str(ROOT / 'scripts/build-lts.py'), 'full-image', '--help'],
            capture_output=True,
            text=True,
            check=True,
        )
        for flag in (
            '--zygisk-next-zip',
            '--skip-zygisk-next',
            '--game-opt-zip',
            '--skip-game-opt',
            '--device-faker-zip',
            '--device-faker-src',
            '--skip-device-faker',
        ):
            self.assertIn(flag, res.stdout)


if __name__ == '__main__':
    unittest.main()
