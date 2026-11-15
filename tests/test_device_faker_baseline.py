import argparse
import gzip
import hashlib
import io
import json
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
import full_image


def make_aarch64_elf(machine=183):
    hdr = bytearray(64)
    hdr[0:4] = b'\x7fELF'
    hdr[4] = 2  # ELFCLASS64
    hdr[5] = 1  # ELFDATA2LSB
    hdr[6] = 1
    struct.pack_into('<H', hdr, 18, machine)
    return bytes(hdr) + b'\x00' * 16


def make_device_faker_zip(mod_id='device_faker', machine=183, omit=()):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as zf:
        entries = {
            'module.prop': f'id={mod_id}\nname=Device Faker\nversion=v2.4.0\n'.encode(),
            'customize.sh': b'#!/system/bin/sh\necho ok\n',
            'uninstall.sh': b'#!/system/bin/sh\nrm -rf /data/adb/device_faker\n',
            'zygisk/arm64-v8a.so': make_aarch64_elf(machine=machine),
        }
        for name, data in entries.items():
            if name not in omit:
                zf.writestr(name, data)
    return buf.getvalue()


def parse_cpio_gz(blob):
    stream = io.BytesIO(blob)
    entries = []
    while True:
        pos = stream.tell()
        if pos >= len(blob):
            break
        with gzip.GzipFile(fileobj=stream, mode='rb') as gz:
            raw = gz.read()
        # gzip.GzipFile reads up to end of first member; advance to gzip member boundary
        # by decompressing incrementally via zlib when multiple concatenated members exist.
        break
    import zlib
    members = []
    remaining = blob
    while remaining:
        d = zlib.decompressobj(16 + zlib.MAX_WBITS)
        raw = d.decompress(remaining) + d.flush()
        members.append(raw)
        remaining = d.unused_data
    for raw in members:
        off = 0
        member_files = {}
        while off + 110 <= len(raw):
            hdr = raw[off:off + 110]
            if hdr[:6] != b'070701':
                raise AssertionError(f'Bad cpio magic at {off}: {hdr[:6]!r}')
            namesize = int(hdr[94:102], 16)
            filesize = int(hdr[54:62], 16)
            off += 110
            name = raw[off:off + namesize - 1].decode('utf-8')
            off = (off + namesize + 3) & ~3
            data = raw[off:off + filesize]
            off = (off + filesize + 3) & ~3
            if name == 'TRAILER!!!':
                break
            member_files[name] = data
        entries.append(member_files)
    return entries


def mock_touch_module(orig_module):
    class FakeTouch:
        @staticmethod
        def build(_root):
            return {
                'bin/touch-calibration-boot': (0o100755, b'#!/bin/busybox sh\n'),
                'etc/sq2-touch-calibration/wizard.apk': (0o100644, b'PKmockapk'),
            }, {'mock': True}

    def wrapper(name, filename):
        if name == 'full_image_touch':
            return FakeTouch
        return orig_module(name, filename)

    return wrapper


def stage_mock_private(root, fake_zip=None):
    runtime = root / 'full-image-runtime'
    files = runtime / 'files'
    files.mkdir(parents=True)

    init_bytes = (
        b'#!/bin/busybox sh\n'
        b'echo init\n'
        + full_image.ANCHOR
        + full_image.HOOK
    )
    fixture_files = {
        'init': (0o100755, init_bytes),
        'bin/dropbear': (0o100755, b'dropbear'),
        'bin/start-ssh': (0o100755, b'#!/bin/sh\n'),
        'etc/shadow': (0o100600, b'root:*:0:0:99999:7:::\n'),
        'etc/renderer/shader_cache': (0o100644, b'renderer-data'),
    }
    records = {}
    for rel, (mode, data) in fixture_files.items():
        p = files / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
        records[rel] = full_image.new_record(mode)
    for d in ('bin', 'etc', 'etc/renderer'):
        records[d] = full_image.new_record(0o040755)

    baseline_sha = '\n'.join(
        f"{hashlib.sha256(data).hexdigest()}  {rel}"
        for rel, (_, data) in sorted(fixture_files.items())
        if rel not in {'bin/dropbear', 'bin/start-ssh', 'etc/shadow'}
    ) + '\n'
    (files / 'BASELINE.sha256').write_text(baseline_sha)
    records['BASELINE.sha256'] = full_image.new_record(0o100644)
    (runtime / 'records.json').write_text(json.dumps(records))

    (root / 'template.img').write_bytes(b'template')
    efi = root / 'kernel-6.18' / 'EFI' / 'SQ2Nat'
    efi.mkdir(parents=True)
    for name in ('start.efi', 'el2launch.efi', 'qebspilaa64.efi', 'tcblaunch.exe', 'Image', 'android-el2.dtb'):
        (efi / name).write_bytes(name.encode())
    (efi / 'kernel-options.txt').write_text('root=/dev/ram0 sq2.vulkan=qcom\n')

    if fake_zip is not None:
        ksu_dir = root / 'ksu-modules'
        ksu_dir.mkdir(parents=True)
        (ksu_dir / 'device_faker.zip').write_bytes(fake_zip)


class DeviceFakerBaselineTests(unittest.TestCase):
    def test_validate_device_faker_zip(self):
        good = make_device_faker_zip()
        digest = df_mod.validate_device_faker_zip(good)
        self.assertEqual(digest, hashlib.sha256(good).hexdigest())

        with self.assertRaisesRegex(ValueError, 'missing required entries'):
            df_mod.validate_device_faker_zip(make_device_faker_zip(omit=('module.prop',)))

        with self.assertRaisesRegex(ValueError, 'missing id=device_faker'):
            df_mod.validate_device_faker_zip(make_device_faker_zip(mod_id='other_mod'))

        with self.assertRaisesRegex(ValueError, 'EM_AARCH64'):
            df_mod.validate_device_faker_zip(make_device_faker_zip(machine=62))

    def test_resolve_or_build_device_faker_zip_precedence(self):
        zip_a = make_device_faker_zip()
        zip_b = zip_a  # identical schema, distinct bytes by adding padding comment via new entry
        buf = io.BytesIO(zip_a)
        with zipfile.ZipFile(buf, 'a') as zf:
            zf.writestr('extra.txt', b'variant')
        zip_b = buf.getvalue()

        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            explicit = root / 'explicit.zip'
            explicit.write_bytes(zip_b)
            priv = root / 'private'
            (priv / 'ksu-modules').mkdir(parents=True)
            (priv / 'ksu-modules' / 'device_faker.zip').write_bytes(zip_a)
            cached = root / 'out' / 'device_faker-gfx.zip'
            cached.parent.mkdir(parents=True)
            cached.write_bytes(zip_a)

            data, meta = df_mod.resolve_or_build_device_faker_zip(
                private=priv, explicit_zip=explicit, out_zip=cached, allow_fetch=False
            )
            self.assertEqual(data, zip_b)
            self.assertEqual(meta['source'], 'explicit-zip')

            data, meta = df_mod.resolve_or_build_device_faker_zip(
                private=priv, out_zip=cached, allow_fetch=False
            )
            self.assertEqual(data, zip_a)
            self.assertEqual(meta['source'], 'private-ksu-modules')

            data, meta = df_mod.resolve_or_build_device_faker_zip(
                private=None, out_zip=cached, allow_fetch=False
            )
            self.assertEqual(data, zip_a)
            self.assertEqual(meta['source'], 'cached-out-zip')

            with self.assertRaisesRegex(FileNotFoundError, 'Forked device_faker zip not staged'):
                df_mod.resolve_or_build_device_faker_zip(
                    private=None, out_zip=root / 'missing.zip', allow_fetch=False
                )

    def test_sq2_ksu_modules_boot_script_behavior(self):
        script = ROOT / 'probe' / 'test2' / 'sq2-ksu-modules-boot'
        subprocess.run(['sh', '-n', str(script)], check=True)

        fake_zip = make_device_faker_zip()
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
    echo "INSTALL:$4" >> "{log_file}"
    mkdir -p "{root}/data/adb/modules/device_faker"
    exit 0
fi
exit 1
"""
            )
            nsenter_mock.chmod(0o755)
            # Symlink busybox tools needed by script alongside mock nsenter.
            for tool in ('grep', 'mkdir', 'rmdir', 'seq', 'sleep', 'timeout', 'test', 'unzip', 'sed', 'tr', 'head', 'cp', 'rm', 'cat'):
                for cand in (Path('/bin') / tool, Path('/usr/bin') / tool):
                    if cand.exists():
                        (mock_bin / tool).symlink_to(cand)
                        break

            cmdline = root / 'cmdline'
            cmdline.write_text('root=/dev/ram0 sq2.vulkan=qcom\n')
            mod_dir = root / 'etc' / 'sq2-ksu-modules'
            mod_dir.mkdir(parents=True)
            (mod_dir / 'device_faker.zip').write_bytes(fake_zip)
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
                SQ2_BOOT_POLL_RETRIES='2',
                SQ2_BOOT_POLL_SLEEP='0',
            )
            res = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True, check=True)
            self.assertIn('KSU_MODULE_INSTALLED:device_faker', res.stdout)
            self.assertEqual(log_file.read_text().strip(), 'INSTALL:/data/local/tmp/sq2-ksu-module.zip')

            # Second run should be idempotent because /data/adb/modules/device_faker now exists.
            res2 = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True, check=True)
            self.assertEqual(res2.stdout.strip(), '')
            self.assertEqual(len(log_file.read_text().strip().splitlines()), 1)

            # sq2.rescue=1 and sq2.ksu=0 must skip provisioning cleanly.
            (data_dir / 'adb' / 'modules' / 'device_faker').rmdir()
            for flag in ('sq2.rescue=1', 'sq2.ksu=0'):
                cmdline.write_text(f'root=/dev/ram0 {flag}\n')
                res_skip = subprocess.run(['sh', str(script)], env=env, capture_output=True, text=True, check=True)
                self.assertEqual(res_skip.stdout.strip(), '')
                self.assertEqual(len(log_file.read_text().strip().splitlines()), 1)

    def test_assemble_runtime_embeds_ksu_module_on_baseline(self):
        fake_zip = make_device_faker_zip()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            private = root / 'private'
            stage_mock_private(private)
            out_cpio = root / 'initramfs.cpio.gz'

            with mock.patch.object(full_image, 'module', side_effect=mock_touch_module(full_image.module)):
                report = full_image.assemble_runtime(
                    private,
                    out_cpio,
                    graphics='baseline',
                    ksu_modules={'device_faker.zip': fake_zip},
                )
            self.assertIsNone(report)
            members = parse_cpio_gz(out_cpio.read_bytes())
            self.assertEqual(len(members), 2)
            main_files, cred_files = members
            self.assertEqual(main_files['etc/sq2-ksu-modules/device_faker.zip'], fake_zip)
            self.assertIn('bin/sq2-ksu-modules-boot', main_files)
            init_txt = main_files['init']
            self.assertIn(full_image.KSU_MODULES_HOOK, init_txt)
            self.assertLess(init_txt.index(full_image.KSU_MODULES_HOOK), init_txt.index(full_image.ANCHOR))
            self.assertEqual(set(cred_files), {'bin/dropbear', 'bin/start-ssh', 'etc/shadow'})
            self.assertIn('BASELINE.sha256', main_files)

    def test_full_image_build_baseline_integrates_device_faker_fork(self):
        fake_zip = make_device_faker_zip()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            private = root / 'private'
            stage_mock_private(private, fake_zip=fake_zip)

            def fake_disk_build(disk_args):
                return {'disk_built': True, 'graphics_argument': disk_args.graphics_argument}

            with (
                mock.patch('disk_image.build', side_effect=fake_disk_build),
                mock.patch.object(full_image, 'module', side_effect=mock_touch_module(full_image.module)),
            ):
                args = argparse.Namespace(
                    output=root / 'out-baseline',
                    private_root=private,
                    template=None,
                    build_dir=None,
                    kernel_mode='prebuilt',
                    kernel_patches=None,
                    cross_compile=None,
                    jobs=1,
                    graphics='baseline',
                    device_faker_zip=None,
                    device_faker_src=None,
                    skip_device_faker=False,
                    esp_only=True,
                )
                report = full_image.build(args, lambda _: {})

                self.assertEqual(report['graphics'], 'baseline')
                self.assertEqual(report['device_faker_fork']['source'], 'private-ksu-modules')
                self.assertEqual(report['device_faker_fork']['fork_url'], df_mod.FORK_URL)
                recorded = json.loads((args.output / 'FULL-IMAGE.json').read_text())
                self.assertEqual(recorded['device_faker_fork']['sha256'], hashlib.sha256(fake_zip).hexdigest())
                members = parse_cpio_gz((args.output / 'paired/EFI/SQ2Nat/initramfs.cpio.gz').read_bytes())
                self.assertEqual(members[0]['etc/sq2-ksu-modules/device_faker.zip'], fake_zip)

                skip_args = argparse.Namespace(**{**vars(args), 'output': root / 'out-skip', 'skip_device_faker': True})
                skip_report = full_image.build(skip_args, lambda _: {})
                self.assertIsNone(skip_report['device_faker_fork'])
                skip_members = parse_cpio_gz((skip_args.output / 'paired/EFI/SQ2Nat/initramfs.cpio.gz').read_bytes())
                self.assertNotIn('etc/sq2-ksu-modules/device_faker.zip', skip_members[0])

    def test_build_lts_full_image_cli_defaults_to_baseline_with_device_faker_flags(self):
        res = subprocess.run(
            [sys.executable, str(ROOT / 'scripts/build-lts.py'), 'full-image', '--help'],
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertIn('--device-faker-zip', res.stdout)
        self.assertIn('--device-faker-src', res.stdout)
        self.assertIn('--skip-device-faker', res.stdout)
        self.assertIn("baseline", res.stdout)


if __name__ == '__main__':
    unittest.main()
