"""Host-only end-to-end tests for Surface Pro X Googlebook OS OTA system:
- spx-ota-tool (C utility: sha256, detect, inspect-gpt, extract-stream)
- build_ota_package (auto/delta/full manifest generation, blob deduplication)
- full_image_ota (initramfs wiring + ARM64 ELF build)
- spx-ota-boot (Android root bridge daemon: Settings intent claim, online ESP apply, offline staging)
- spx-ota-stage (initramfs offline applier: manifest + full .img.gz + keep-data + clean-wipe)
- prepare-image-ssd (allows larger existing partitions for cross-layout OTA)
- OtaPolicy & bilingual Strings Java verification + APK manifest intent check.
"""
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))

import build_ota_package as ota_pkg
import disk_image as d
import full_image_advanced_control as adv_ctrl
import full_image_ota as fi_ota


def make_synthetic_gpt_image(path, super_bytes_data, esp_bytes_data):
    """Create a valid minimal GPT disk image containing super and SQ2PROBE."""
    m = 1024 * 1024
    super_off = m
    super_len = ((len(super_bytes_data) + m - 1) // m) * m
    esp_off = super_off + super_len
    esp_len = ((len(esp_bytes_data) + m - 1) // m) * m
    total_bytes = esp_off + esp_len + m
    parts = [
        {'index': 1, 'name': 'super', 'offset': super_off, 'bytes': super_len},
        {'index': 2, 'name': 'SQ2PROBE', 'offset': esp_off, 'bytes': esp_len},
    ]
    path = Path(path)
    with path.open('wb') as f:
        f.truncate(total_bytes)
        f.seek(super_off)
        f.write(super_bytes_data)
        f.seek(esp_off)
        f.write(esp_bytes_data)
    d.write_minimal_gpt(path, parts)
    return parts


def make_esp_tree(root, kernel=b'KERNEL-V1', initramfs=b'INITRAMFS-V1', fw_extra=b'FW-A', release='v1.0'):
    root = Path(root)
    for sub in ('EFI/BOOT', 'EFI/SQ2Nat', 'EFI/SQ2Res', 'firmware/qcom'):
        (root / sub).mkdir(parents=True, exist_ok=True)
    (root / 'SPX-IMAGE.txt').write_text(f'SPX_IMAGE_FORMAT=1\nSPX_RELEASE={release}\n')
    (root / 'EFI/BOOT/BOOTAA64.EFI').write_bytes(b'GRUB-BIN')
    (root / 'EFI/BOOT/grub.cfg').write_text(
        "set timeout=5\nset default=spx-single-user\n"
        "menuentry 'Googlebook OS' --id spx-single-user { chainloader /EFI/SQ2Nat/start.efi }\n"
        "menuentry 'Windows' --id windows { chainloader /EFI/Microsoft/Boot/bootmgfw.efi }\n"
    )
    for dname in ('EFI/SQ2Nat', 'EFI/SQ2Res'):
        (root / dname / 'Image').write_bytes(kernel)
        (root / dname / 'initramfs.cpio.gz').write_bytes(initramfs)
        (root / dname / 'start.efi').write_bytes(b'START-' + dname.encode())
    (root / 'firmware/qcom/modem.bin').write_bytes(fw_extra)
    return root


class OtaSystemTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='spx-ota-tests-')
        cls.work = Path(cls.tmp.name)
        cls.host_tool = cls.work / 'spx-ota-tool'
        subprocess.run(
            ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
             str(ROOT / 'probe/ota/spx-ota-tool.c'), '-o', str(cls.host_tool)],
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_ota_tool_sha256_detect_gpt_and_stream_extract(self):
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            super_data = b'SUPER_PAYLOAD_BLOCK_' * 65536  # ~1.3 MB
            esp_data = b'ESP_FAT_PAYLOAD_BLK_' * 32768    # ~640 KB
            img = td / 'disk.img'
            parts = make_synthetic_gpt_image(img, super_data, esp_data)
            img_gz = td / 'disk.img.gz'
            img_gz.write_bytes(gzip.compress(img.read_bytes(), compresslevel=1))

            # sha256 file & stdin
            expected_sha = hashlib.sha256(img.read_bytes()).hexdigest()
            got_file = subprocess.check_output([str(self.host_tool), 'sha256', str(img)], text=True).strip()
            self.assertEqual(got_file, expected_sha)
            got_stdin = subprocess.check_output(
                [str(self.host_tool), 'sha256', '-'], input=img.read_bytes(),
            ).decode().strip()
            self.assertEqual(got_stdin, expected_sha)

            # detect raw img, gzip, and inner stream
            self.assertEqual(subprocess.check_output([str(self.host_tool), 'detect', str(img)], text=True).strip(), 'full-img')
            self.assertEqual(subprocess.check_output([str(self.host_tool), 'detect', str(img_gz)], text=True).strip(), 'gzip')
            inner = subprocess.check_output(
                f'gzip -dc "{img_gz}" | "{self.host_tool}" detect -', shell=True, text=True,
            ).strip()
            self.assertEqual(inner, 'full-img')

            # inspect-gpt on raw and piped gzip stream
            gpt_out = subprocess.check_output([str(self.host_tool), 'inspect-gpt', str(img)], text=True)
            gpt_gz_out = subprocess.check_output(
                f'gzip -dc "{img_gz}" | "{self.host_tool}" inspect-gpt -', shell=True, text=True,
            )
            self.assertEqual(gpt_out, gpt_gz_out)
            rows = {line.split('\t')[0]: tuple(map(int, line.split('\t')[1:3])) for line in gpt_out.splitlines()}
            self.assertEqual(rows['super'], (parts[0]['offset'], parts[0]['bytes']))
            self.assertEqual(rows['SQ2PROBE'], (parts[1]['offset'], parts[1]['bytes']))

            # extract-stream single-pass from gzip stream
            out_super = td / 'out-super.bin'
            out_esp = td / 'out-esp.bin'
            ext_out = subprocess.check_output(
                f'gzip -dc "{img_gz}" | "{self.host_tool}" extract-stream '
                f'"{out_super}" {parts[0]["offset"]} {parts[0]["bytes"]} '
                f'"{out_esp}" {parts[1]["offset"]} {parts[1]["bytes"]}',
                shell=True, text=True, stderr=subprocess.PIPE,
            )
            kv = dict(line.split('=', 1) for line in ext_out.strip().splitlines())
            self.assertEqual(out_super.stat().st_size, parts[0]['bytes'])
            self.assertEqual(out_esp.stat().st_size, parts[1]['bytes'])
            self.assertEqual(kv['SUPER_SHA256'], hashlib.sha256(out_super.read_bytes()).hexdigest())
            self.assertEqual(kv['ESP_SHA256'], hashlib.sha256(out_esp.read_bytes()).hexdigest())
            self.assertTrue(out_super.read_bytes().startswith(super_data))
            self.assertTrue(out_esp.read_bytes().startswith(esp_data))

    def test_build_ota_package_auto_delta_and_full_modes(self):
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            super_v1 = b'A' * (1024 * 1024)
            super_v2 = b'B' * (1024 * 1024)
            esp_raw = b'E' * (1024 * 1024)

            img_v1 = td / 'v1.img'
            make_synthetic_gpt_image(img_v1, super_v1, esp_raw)
            esp_v1 = make_esp_tree(td / 'esp-v1', kernel=b'K1', initramfs=b'I1', fw_extra=b'FW1', release='v1.0')
            (esp_v1 / 'firmware/qcom/obsolete.bin').write_bytes(b'OLD-FW')

            # Build v1 OTA (super unchanged hint -> ESP-only, deduplicated blobs, no grub.cfg)
            ota_v1 = ota_pkg.build_ota_package(
                output_dir=td / 'ota-v1',
                esp_tree=esp_v1,
                image=img_v1,
                mode='auto',
                super_unchanged_hint=True,
                version='v1.0',
            )
            self.assertFalse(ota_v1['super_included'])
            self.assertEqual(ota_v1['esp_mode'], 'sync')
            # SQ2Nat & SQ2Res share Image & initramfs -> deduplicated count strictly smaller than PUT count
            self.assertLess(ota_v1['deduplicated_blob_count'], ota_v1['update_put_count'])
            with tarfile.open(ota_v1['archive'], 'r:gz') as tar:
                names = tar.getnames()
                self.assertIn('ota-manifest.json', names)
                self.assertIn('ota-manifest.kv', names)
                self.assertIn('ota-files.tsv', names)
                self.assertNotIn('payload/super.img', names)
                self.assertNotIn('payload/esp/EFI/BOOT/grub.cfg', names)

            # Build v1.1 where super is UNCHANGED, only initramfs changed and obsolete.bin deleted
            esp_v11 = make_esp_tree(td / 'esp-v11', kernel=b'K1', initramfs=b'I1.1-NEW', fw_extra=b'FW1', release='v1.1')
            ota_v11 = ota_pkg.build_ota_package(
                output_dir=td / 'ota-v11',
                esp_tree=esp_v11,
                image=img_v1,
                previous_manifest=td / 'ota-v1/OTA-MANIFEST.json',
                mode='auto',
                version='v1.1',
            )
            self.assertFalse(ota_v11['super_included'])
            self.assertEqual(ota_v11['esp_mode'], 'delta')
            self.assertEqual(ota_v11['update_del_count'], 1)
            # Only SPX-IMAGE.txt + SQ2Nat/initramfs + SQ2Res/initramfs changed -> 3 PUTs, 2 unique blobs
            self.assertEqual(ota_v11['update_put_count'], 3)
            self.assertEqual(ota_v11['deduplicated_blob_count'], 2)

            # Build v2.0 where super CHANGED -> auto switches to full OTA including payload/super.img
            img_v2 = td / 'v2.img'
            make_synthetic_gpt_image(img_v2, super_v2, esp_raw)
            esp_v2 = make_esp_tree(td / 'esp-v2', kernel=b'K2', initramfs=b'I2', fw_extra=b'FW2', release='v2.0')
            ota_v2 = ota_pkg.build_ota_package(
                output_dir=td / 'ota-v2',
                esp_tree=esp_v2,
                image=img_v2,
                previous_manifest=td / 'ota-v11/OTA-MANIFEST.json',
                mode='auto',
                version='v2.0',
            )
            self.assertTrue(ota_v2['super_included'])
            with tarfile.open(ota_v2['archive'], 'r:gz') as tar:
                self.assertIn('payload/super.img', tar.getnames())
                self.assertEqual(tar.extractfile('payload/super.img').read(), super_v2)

    def test_ota_boot_daemon_online_delta_offline_staging_and_intent_claim(self):
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            data_root = td / 'android-root'
            app_files = data_root / 'data/user_de/0/org.surface.advancedcontrol/files'
            app_files.mkdir(parents=True)
            esp_dir = make_esp_tree(td / 'live-esp', kernel=b'K1', initramfs=b'I1', fw_extra=b'FW1', release='v1.0')
            (esp_dir / 'firmware/qcom/obsolete.bin').write_bytes(b'OLD-FW')
            (esp_dir / 'SPX-USERDATA-GROWN.txt').write_text('grown-marker\n')
            orig_grub = (esp_dir / 'EFI/BOOT/grub.cfg').read_text()

            etc_dir = td / 'etc'
            etc_dir.mkdir()
            super_dev = td / 'super.dev'
            super_v1 = b'S' * (1024 * 1024)
            super_dev.write_bytes(super_v1)
            super_v1_sha = hashlib.sha256(super_v1).hexdigest()
            (etc_dir / 'spx-ota-info').write_text(
                f'SPX_OTA_VERSION=v1.0\nSPX_OTA_COMMIT=abc123\nSPX_OTA_KERNEL=6.18.60-sq2-android1\nSPX_OTA_BUILD_DISPLAY=v1.0.261119\nSPX_OTA_SUPER_SHA256={super_v1_sha}\n'
            )

            # Build a delta OTA package (super unchanged)
            img_v1 = td / 'v1.img'
            make_synthetic_gpt_image(img_v1, super_v1, b'E' * (1024 * 1024))
            base_ota = ota_pkg.build_ota_package(
                output_dir=td / 'base-ota', esp_tree=esp_dir, image=img_v1,
                mode='auto', super_unchanged_hint=True, version='v1.0',
            )
            esp_v11 = make_esp_tree(td / 'esp-v11', kernel=b'K1', initramfs=b'I1.1-UPDATED', fw_extra=b'FW1', release='v1.1')
            delta_ota = ota_pkg.build_ota_package(
                output_dir=td / 'delta-ota', esp_tree=esp_v11, image=img_v1,
                previous_manifest=Path(base_ota['manifest']), mode='auto', version='v1.1',
            )

            # Mock Android wrapper recording pm disable calls
            android_log = td / 'android-calls.log'
            wrapper = td / 'android-wrap.sh'
            wrapper.write_text(
                '#!/bin/sh\n'
                f'echo "$*" >> "{android_log}"\n'
                'if [ "$1" = "cmd" ] && [ "$2" = "package" ] && [ "$3" = "query-activities" ]; then\n'
                '  echo "priority=0 preferredOrder=0 match=0x108000 specificIndex=-1 isDefault=true"\n'
                '  echo "com.google.android.gms/.update.SystemUpdateActivity"\n'
                '  echo "org.surface.advancedcontrol/.SystemUpdateActivity"\n'
                'fi\n'
            )
            wrapper.chmod(0o755)

            def run_once(req_body=None):
                if req_body is not None:
                    (app_files / 'ota-request').write_text(req_body)
                env = dict(
                    os.environ,
                    SPX_OTA_BOOT_WAIT='0',
                    SPX_OTA_ONCE='1',
                    SPX_OTA_TEST_USER='0',
                    SPX_OTA_ROOT=str(data_root),
                    SPX_OTA_ESP=str(esp_dir),
                    SPX_OTA_RUN=str(td / 'run'),
                    SPX_OTA_ETC=str(etc_dir),
                    SPX_OTA_TOOL=str(self.host_tool),
                    SPX_OTA_SUPER_DEV=str(super_dev),
                    SPX_OTA_LOCK_DIR=str(td / 'lock'),
                    SPX_OTA_TMP_REQ=str(td / 'tmp-req'),
                    SPX_OTA_ANDROID_WRAPPER=str(wrapper),
                    SPX_OTA_NO_REBOOT='1',
                )
                (td / 'run').mkdir(exist_ok=True)
                p = subprocess.run(['sh', str(ROOT / 'probe/ota/spx-ota-boot')], env=env, text=True, capture_output=True)
                self.assertEqual(p.returncode, 0, p.stderr)
                return (app_files / 'ota-reply').read_text() if (app_files / 'ota-reply').is_file() else ''

            # 1) Refresh + verify competing GMS handler disabled while our handler enabled
            reply = run_once('ota-refresh\nnonce-1\n')
            self.assertTrue(reply.startswith('nonce-1\nOK\n'), reply)
            calls = android_log.read_text()
            self.assertIn('pm enable --user 0 org.surface.advancedcontrol/.SystemUpdateActivity', calls)
            self.assertIn('pm disable --user 0 com.google.android.gms/.update.SystemUpdateActivity', calls)
            self.assertNotIn('pm disable --user 0 org.surface.advancedcontrol', calls)
            state_kv = dict(line.split('=', 1) for line in (app_files / 'ota-state').read_text().splitlines() if '=' in line)
            self.assertTrue(state_kv.get('data_free_mb', '').isdigit() and int(state_kv['data_free_mb']) > 0, state_kv)
            self.assertEqual(state_kv.get('current_build_display'), 'v1.0.261119', state_kv)

            # 2) Inspect delta OTA package
            (app_files / 'ota-upload.pkg').write_bytes(Path(delta_ota['archive']).read_bytes())
            reply = run_once('ota-inspect\nnonce-2\nota-upload.pkg\n')
            self.assertTrue(reply.startswith('nonce-2\nOK\n'), reply)
            self.assertIn('package_kind=manifest', reply)
            self.assertIn('super_included=0', reply)
            self.assertIn('super_compatible=1', reply)
            self.assertIn('version=v1.1', reply)

            # 3) Apply delta OTA online (keep data = 0)
            reply = run_once(f'ota-install\nnonce-3\n{delta_ota["archive_sha256"]}\n0\n')
            self.assertTrue(reply.startswith('nonce-3\nOK\nONLINE_APPLIED'), reply)
            self.assertEqual((esp_dir / 'EFI/SQ2Nat/initramfs.cpio.gz').read_bytes(), b'I1.1-UPDATED')
            self.assertEqual((esp_dir / 'EFI/SQ2Res/initramfs.cpio.gz').read_bytes(), b'I1.1-UPDATED')
            self.assertFalse((esp_dir / 'firmware/qcom/obsolete.bin').exists())
            self.assertEqual((esp_dir / 'EFI/BOOT/grub.cfg').read_text(), orig_grub)
            self.assertEqual((esp_dir / 'SPX-USERDATA-GROWN.txt').read_text(), 'grown-marker\n')
            self.assertIn('STATUS=APPLIED_PENDING_REBOOT', (esp_dir / 'SPX-OTA-RESULT.txt').read_text())

            # 4) Same delta OTA with clean install (wipe_data=1) -> must stage OFFLINE_STAGED!
            (app_files / 'ota-upload.pkg').write_bytes(Path(delta_ota['archive']).read_bytes())
            reply = run_once('ota-inspect\nnonce-4\nota-upload.pkg\n')
            self.assertTrue(reply.startswith('nonce-4\nOK\n'), reply)
            reply = run_once(f'ota-install\nnonce-5\n{delta_ota["archive_sha256"]}\n1\n')
            self.assertTrue(reply.startswith('nonce-5\nOK\nOFFLINE_STAGED'), reply)
            pending = (esp_dir / 'SPX-OTA-PENDING').read_text()
            self.assertIn('PACKAGE_KIND=manifest', pending)
            self.assertIn('WIPE_DATA=1', pending)
            self.assertIn('APPLY_SUPER=0', pending)

            # 5) Cancel pending update
            reply = run_once('ota-cancel\nnonce-6\n')
            self.assertTrue(reply.startswith('nonce-6\nOK\n'), reply)
            self.assertFalse((esp_dir / 'SPX-OTA-PENDING').exists())

            # 6) Inspect & stage full .img.gz package
            img_gz = td / 'full.img.gz'
            img_gz.write_bytes(gzip.compress(img_v1.read_bytes(), compresslevel=1))
            img_gz_sha = hashlib.sha256(img_gz.read_bytes()).hexdigest()
            (app_files / 'ota-upload.pkg').write_bytes(img_gz.read_bytes())
            reply = run_once('ota-inspect\nnonce-7\nota-upload.pkg\n')
            self.assertTrue(reply.startswith('nonce-7\nOK\n'), reply)
            self.assertIn('package_kind=full-img-gz', reply)
            self.assertIn('super_included=1', reply)
            reply = run_once(f'ota-install\nnonce-8\n{img_gz_sha}\n0\n')
            self.assertTrue(reply.startswith('nonce-8\nOK\nOFFLINE_STAGED'), reply)
            self.assertIn('APPLY_SUPER=1', (esp_dir / 'SPX-OTA-PENDING').read_text())

    def test_ota_stage_offline_full_keep_and_clean_wipe(self):
        for scenario in ('manifest_full_keep', 'full_img_gz_wipe', 'bad_sha_no_loop'):
            with self.subTest(scenario=scenario), tempfile.TemporaryDirectory() as td:
                td = Path(td)
                esp_dir = make_esp_tree(td / 'esp', kernel=b'OLD-K', initramfs=b'OLD-I', fw_extra=b'OLD-FW', release='v1.0')
                (esp_dir / 'SPX-USERDATA-GROWN.txt').write_text('grown\n')
                orig_grub = (esp_dir / 'EFI/BOOT/grub.cfg').read_text()

                data_dir = td / 'userdata'
                (data_dir / 'lost+found').mkdir(parents=True)
                (data_dir / 'vendor/sq2-touch-calibration').mkdir(parents=True)
                (data_dir / 'vendor/sq2-touch-calibration/dev.profile').write_text('CALIB=KEEP\n')
                (data_dir / '.hidden-dotfile').write_text('secret\n')
                (data_dir / 'ota').mkdir()
                (data_dir / 'system/package_cache/1').mkdir(parents=True)
                (data_dir / 'system/package_cache/1/SurfaceAdvancedControl-abc123').write_text('stale-pkg-cache\n')
                (data_dir / 'system/packages.xml').write_text('<packages/>\n')
                (data_dir / 'dalvik-cache/arm64').mkdir(parents=True)
                (data_dir / 'dalvik-cache/arm64/vendor@app@SurfaceAdvancedControl@SurfaceAdvancedControl.apk@classes.dex').write_text('stale-dex\n')
                (data_dir / 'dalvik-cache/arm64/vendor@overlay@spx-systemui-overlay.apk@classes.vdex').write_text('stale-vdex\n')
                (data_dir / 'dalvik-cache/arm64/system@framework@boot.art').write_text('keep-boot-art\n')
                (data_dir / 'misc/apexdata/com.android.art/dalvik-cache/arm64').mkdir(parents=True)
                (data_dir / 'misc/apexdata/com.android.art/dalvik-cache/arm64/vendor@app@SurfaceAdvancedControl@SurfaceAdvancedControl.apk@classes.odex').write_text('stale-odex\n')

                meta_dir = td / 'metadata'
                (meta_dir / 'lost+found').mkdir(parents=True)
                (meta_dir / 'vold').mkdir()
                (meta_dir / 'vold/state').write_text('meta\n')

                super_dev = td / 'super.dev'
                super_dev.write_bytes(b'0' * (1024 * 1024))
                misc_dev = td / 'misc.dev'
                misc_dev.write_bytes(b'M' * (1024 * 1024))

                new_super = b'N' * (1024 * 1024)
                new_esp_tree = make_esp_tree(td / 'new-esp', kernel=b'NEW-K', initramfs=b'NEW-I', fw_extra=b'NEW-FW', release='v2.0')
                new_img = td / 'new.img'
                make_synthetic_gpt_image(new_img, new_super, b'F' * (1024 * 1024))

                env = dict(
                    os.environ,
                    SPX_OTA_ESP=str(esp_dir),
                    SPX_OTA_RUN=str(td / 'run'),
                    SPX_OTA_TOOL=str(self.host_tool),
                    SPX_OTA_SUPER_DEV=str(super_dev),
                    SPX_OTA_DATA_DIR=str(data_dir),
                    SPX_OTA_META_DIR=str(meta_dir),
                    SPX_OTA_MISC_DEV=str(misc_dev),
                    SPX_OTA_NO_REBOOT='1',
                )

                if scenario == 'manifest_full_keep':
                    ota = ota_pkg.build_ota_package(
                        output_dir=td / 'ota-out', esp_tree=new_esp_tree, image=new_img, mode='full', version='v2.0',
                    )
                    (data_dir / 'ota/pending.pkg').write_bytes(Path(ota['archive']).read_bytes())
                    (esp_dir / 'SPX-OTA-PENDING').write_text(
                        f'PACKAGE_REL=ota/pending.pkg\nPACKAGE_SHA256={ota["archive_sha256"]}\n'
                        'PACKAGE_KIND=manifest\nAPPLY_SUPER=1\nWIPE_DATA=0\nVERSION=v2.0\n'
                    )
                    p = subprocess.run(['sh', str(ROOT / 'probe/ota/spx-ota-stage')], env=env, text=True, capture_output=True)
                    self.assertEqual(p.returncode, 0, p.stderr + p.stdout)
                    self.assertEqual(super_dev.read_bytes(), new_super)
                    self.assertEqual((esp_dir / 'EFI/SQ2Nat/Image').read_bytes(), b'NEW-K')
                    self.assertEqual((esp_dir / 'EFI/BOOT/grub.cfg').read_text(), orig_grub)
                    self.assertEqual((data_dir / 'vendor/sq2-touch-calibration/dev.profile').read_text(), 'CALIB=KEEP\n')
                    self.assertFalse((data_dir / 'ota/pending.pkg').exists())
                    self.assertTrue((data_dir / 'system/package_cache').is_dir())
                    self.assertEqual(list((data_dir / 'system/package_cache').iterdir()), [])
                    self.assertFalse((data_dir / 'dalvik-cache/arm64/vendor@app@SurfaceAdvancedControl@SurfaceAdvancedControl.apk@classes.dex').exists())
                    self.assertFalse((data_dir / 'dalvik-cache/arm64/vendor@overlay@spx-systemui-overlay.apk@classes.vdex').exists())
                    self.assertFalse((data_dir / 'misc/apexdata/com.android.art/dalvik-cache/arm64/vendor@app@SurfaceAdvancedControl@SurfaceAdvancedControl.apk@classes.odex').exists())
                    self.assertEqual((data_dir / 'system/packages.xml').read_text(), '<packages/>\n')
                    self.assertFalse((data_dir / 'dalvik-cache/arm64/system@framework@boot.art').exists())
                    self.assertIn('STATUS=OK', (esp_dir / 'SPX-OTA-RESULT.txt').read_text())

                elif scenario == 'full_img_gz_wipe':
                    pkg = data_dir / 'ota/pending.pkg'
                    pkg.write_bytes(gzip.compress(new_img.read_bytes(), compresslevel=1))
                    pkg_sha = hashlib.sha256(pkg.read_bytes()).hexdigest()
                    (esp_dir / 'SPX-OTA-PENDING').write_text(
                        f'PACKAGE_REL=ota/pending.pkg\nPACKAGE_SHA256={pkg_sha}\n'
                        'PACKAGE_KIND=full-img-gz\nAPPLY_SUPER=1\nWIPE_DATA=1\nVERSION=v2.0-full\n'
                    )
                    env['SPX_OTA_ESP_SRC_DIR'] = str(new_esp_tree)
                    p = subprocess.run(['sh', str(ROOT / 'probe/ota/spx-ota-stage')], env=env, text=True, capture_output=True)
                    self.assertEqual(p.returncode, 0, p.stderr + p.stdout)
                    self.assertEqual(super_dev.read_bytes(), new_super)
                    self.assertEqual((esp_dir / 'EFI/SQ2Nat/Image').read_bytes(), b'NEW-K')
                    self.assertEqual((esp_dir / 'EFI/BOOT/grub.cfg').read_text(), orig_grub)
                    # Clean wipe verified: only lost+found remains in userdata & metadata; misc zeroed
                    self.assertEqual([x.name for x in data_dir.iterdir()], ['lost+found'])
                    self.assertEqual([x.name for x in meta_dir.iterdir()], ['lost+found'])
                    self.assertEqual(misc_dev.read_bytes(), b'\x00' * (1024 * 1024))
                    self.assertFalse((esp_dir / 'SPX-USERDATA-GROWN.txt').exists())
                    res = (esp_dir / 'SPX-OTA-RESULT.txt').read_text()
                    self.assertIn('STATUS=OK', res)
                    self.assertIn('WIPE_DATA=1', res)

                else:
                    (data_dir / 'ota/pending.pkg').write_bytes(b'corrupted')
                    (esp_dir / 'SPX-OTA-PENDING').write_text(
                        f'PACKAGE_REL=ota/pending.pkg\nPACKAGE_SHA256={"a"*64}\n'
                        'PACKAGE_KIND=manifest\nAPPLY_SUPER=0\nWIPE_DATA=1\nVERSION=bad\n'
                    )
                    p = subprocess.run(['sh', str(ROOT / 'probe/ota/spx-ota-stage')], env=env, text=True, capture_output=True)
                    self.assertNotEqual(p.returncode, 0)
                    self.assertFalse((esp_dir / 'SPX-OTA-PENDING').exists())
                    self.assertFalse((esp_dir / 'SPX-OTA-INPROGRESS').exists())
                    self.assertIn('STATUS=ERROR', (esp_dir / 'SPX-OTA-RESULT.txt').read_text())
                    self.assertIn('ERROR=package_sha256_mismatch', (esp_dir / 'SPX-OTA-RESULT.txt').read_text())
                    # Touch calibration and package_cache untouched on error!
                    self.assertTrue((data_dir / 'vendor/sq2-touch-calibration/dev.profile').is_file())
                    self.assertTrue((data_dir / 'system/package_cache/1/SurfaceAdvancedControl-abc123').is_file())

    def test_full_image_ota_initramfs_install_and_arm64_build(self):
        arm64_tool = fi_ota.build_ota_tool_bytes()
        self.assertEqual(arm64_tool[:6], b'\x7fELF\x02\x01')
        init_src = (ROOT / 'probe/test2/init-googlebook-native').read_bytes()
        bodies = {'init': init_src}
        records = {'init': [0, [0o100755, 0, 0, 1, 0, 0, 0, 0, 0, 0]]}
        fi_ota.install(records, bodies, lambda m: [0, [m, 0, 0, 1, 0, 0, 0, 0, 0, 0]],
                       compiled_ota_tool=arm64_tool, version='v0.8.0', source_commit='deadbeef')
        self.assertIn('bin/spx-ota-tool', bodies)
        self.assertIn('bin/spx-ota-stage', bodies)
        self.assertIn('bin/spx-ota-boot', bodies)
        self.assertIn('etc/spx-ota-info', bodies)
        self.assertIn(b'/bin/spx-ota-stage', bodies['init'])
        self.assertIn(b'SPX-OTA-RESULT.txt', bodies['init'])
        legacy_init = init_src.replace(fi_ota.EARLY_ANCHOR, fi_ota.LEGACY_EARLY_HOOK + fi_ota.EARLY_ANCHOR, 1)
        upgraded_init = fi_ota.patch_init_for_ota(legacy_init)
        self.assertIn(b'SPX-OTA-RESULT.txt', upgraded_init)
        self.assertNotIn(fi_ota.LEGACY_EARLY_HOOK, upgraded_init)
        self.assertIn(b'/bin/spx-ota-boot', bodies['init'])
        self.assertLess(bodies['init'].index(b'/bin/spx-ota-stage'), bodies['init'].index(b'base=/media/data/googlebook-stage'))
        self.assertLess(bodies['init'].index(b'/bin/spx-ota-boot'), bodies['init'].index(b'exec native-handoff /newroot'))
        self.assertEqual(fi_ota.format_ota_build_suffix('v0.7.6', '20261119.1515', source_commit='abcdef123456', super_sha256='012345' + '0' * 58),
                         'v0.7.6.261119')
        self.assertEqual(fi_ota.format_ota_build_suffix('v0.7.5-8-gabcdef1', '261119', source_commit='abcdef123456'),
                         'v0.7.5-8-gabcdef1.261119')
        self.assertEqual(fi_ota.format_ota_build_suffix('dev', '261119', source_commit='abcdef123456'),
                         'dev.261119.abcdef1')

    def test_prepare_image_ssd_allows_larger_super_and_esp_for_ota(self):
        source = (ROOT / 'probe/test2/prepare-image-ssd').read_text()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / 'etc').mkdir(); (root / 'dev').mkdir(); (root / 'sys/class/block').mkdir(parents=True)
            (root / 'esp-tree/EFI/SQ2Nat').mkdir(parents=True)
            (root / 'esp-tree/EFI/SQ2Nat/start.efi').touch()
            (root / 'esp-tree/SPX-IMAGE.txt').write_text('SPX_IMAGE_FORMAT=1\n')
            (root / 'esp-tree/SPX-BOOT-SOURCE.txt').write_text('a' * 64 + '\n')
            (root / 'cmdline').write_text('spx.boot_token=' + 'a' * 64 + '\n')
            (root / 'etc/sq2-ssd-partitions').write_text('userdata 1 32\nsuper 3 2048\nSQ2PROBE 6 1024\n')
            for name, num, sec in (('userdata', 1, 128), ('super', 3, 4096), ('SQ2PROBE', 6, 2048)):
                k = f'nvme0n1p{num}'
                p = root / 'devices/nvme0n1' / k
                p.mkdir(parents=True)
                (p / 'partition').write_text(f'{num}\n')
                (p / 'size').write_text(f'{sec}\n')
                (p / 'uevent').write_text(f'PARTNAME={name}\n')
                (root / 'sys/class/block' / k).symlink_to(p)
                (root / 'dev' / k).write_bytes(b'x')
            code = source.replace('/proc/cmdline', str(root / 'cmdline'))
            for old in ('/etc/sq2-ssd', '/sys/class/block', '/dev/', '/run/', '/media/'):
                code = code.replace(old, str(root) + old)
            stub = (
                'fail() { echo "FAIL:$1" >&2; exit 1; }\n'
                'mount() { for last; do :; done; mkdir -p "$last"; cp -a "$FIXTURE/esp-tree/." "$last/"; }\n'
                'umount() { return 0; }\n'
                'sleep() { return 0; }\n'
            )
            p = subprocess.run(['sh', '-c', stub + code], env=dict(os.environ, FIXTURE=td), text=True, capture_output=True)
            self.assertEqual(p.returncode, 0, p.stderr)

    def test_java_ota_policy_strings_and_apk_system_update_intent(self):
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            runner = td / 'OtaJavaTest.java'
            runner.write_text(r'''
import java.util.Locale;
import org.surface.advancedcontrol.OtaPolicy;
import org.surface.advancedcontrol.Strings;

public class OtaJavaTest {
    static void check(boolean ok, String m) { if (!ok) throw new AssertionError(m); }
    public static void main(String[] args) {
        String sha = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        check(OtaPolicy.refreshRequest("n-1").equals("ota-refresh\nn-1\n"), "refresh");
        check(OtaPolicy.inspectRequest("n-2", "ota-upload.pkg").equals("ota-inspect\nn-2\nota-upload.pkg\n"), "inspect");
        check(OtaPolicy.installRequest("n-3", sha, false).equals("ota-install\nn-3\n" + sha + "\n0\n"), "keep");
        check(OtaPolicy.installRequest("n-4", sha, true).equals("ota-install\nn-4\n" + sha + "\n1\n"), "wipe");
        check(OtaPolicy.formatBytes(1536L * 1024L).equals("1.5 MiB"), "fmt");
        for (String bad : new String[]{"../x", "/abs", "a;b\nc", "bad$x"}) {
            try { OtaPolicy.validateSafeRelPath(bad); throw new AssertionError(bad); }
            catch (IllegalArgumentException expected) {}
        }
        Strings zh = new Strings(Locale.SIMPLIFIED_CHINESE);
        Strings en = new Strings(Locale.US);
        check(zh.otaTitle().contains("系统更新"), "zh title");
        check(en.otaTitle().toLowerCase(Locale.US).contains("system update"), "en title");
        check(zh.otaWipeDataTitle().contains("全新安装"), "zh wipe");
        check(en.otaWipeDataTitle().contains("Clean install"), "en wipe");
    }
}
''')
            cls_dir = td / 'classes'
            cls_dir.mkdir()
            src = ROOT / 'probe/advanced-control'
            subprocess.run(
                ['javac', '--release', '8', '-encoding', 'UTF-8', '-d', str(cls_dir),
                 str(src / 'DriverPolicy.java'), str(src / 'OtaPolicy.java'),
                 str(src / 'Strings.java'), str(runner)],
                check=True,
            )
            subprocess.run(['java', '-cp', str(cls_dir), 'OtaJavaTest'], check=True)

            # Build real APK and verify SYSTEM_UPDATE_SETTINGS intent filter is registered
            apk_path = td / 'SurfaceAdvancedControl.apk'
            adv_ctrl.build_apk(apk_path)
            aapt2 = adv_ctrl._find_sdk() / 'android-15/aapt2'
            dump = subprocess.check_output([str(aapt2), 'dump', 'xmltree', str(apk_path), '--file', 'AndroidManifest.xml'], text=True)
            self.assertIn('android.settings.SYSTEM_UPDATE_SETTINGS', dump)
            self.assertIn('.SystemUpdateActivity', dump)
            ver_match = re.search(r'versionCode\(0x0101021b\)=(\d+)', dump)
            self.assertIsNotNone(ver_match, dump)
            self.assertGreaterEqual(int(ver_match.group(1)), 3)

    def test_linux_adb_upgrade_script_end_to_end(self):
        script = ROOT / 'tools/upgrade-googlebook-via-adb.sh'
        subprocess.run(['sh', '-n', str(script)], check=True)
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            super_data = b'S' * (1024 * 1024)
            esp_data = b'E' * (1024 * 1024)
            img = td / 'full.img'
            parts = make_synthetic_gpt_image(img, super_data, esp_data)
            img_gz = td / 'full.img.gz'
            img_gz.write_bytes(gzip.compress(img.read_bytes(), compresslevel=1))

            esp_tree = make_esp_tree(td / 'esp', release='v0.8.0')
            delta_ota = ota_pkg.build_ota_package(
                output_dir=td / 'delta', esp_tree=esp_tree, image=img,
                mode='auto', super_unchanged_hint=True, version='v0.8.0',
            )

            log_file = td / 'adb-calls.log'
            mock_adb = td / 'mock-adb'
            mock_adb.write_text(f'''#!/bin/sh
set -eu
echo "$*" >> "{log_file}"
if [ "${{1:-}}" = "-s" ]; then shift 2; fi
case "${{1:-}}" in
    devices)
        printf 'List of devices attached\\nSPX12345\\tdevice\\n'
        ;;
    root|wait-for-device)
        exit 0
        ;;
    push)
        exit 0
        ;;
    shell)
        shift
        cmd="$*"
        case "$cmd" in
            "id -u") echo 0 ;;
            *"chmod 755"*) exit 0 ;;
            *"probe"*)
                printf 'SUPER_BYTES=12884901888\\nESP_BYTES=2147483648\\nDATA_FREE_BYTES=64424509440\\nHAS_OTA_INFO=%s\\nCURRENT_SUPER_SHA256=%s\\nKERNEL=6.18.60-sq2-android1\\n' "${{MOCK_HAS_OTA:-0}}" "${{MOCK_SUPER_SHA:-}}"
                ;;
            *"check-staged-pkg"*)
                if [ -n "${{MOCK_STAGED_HIT:-}}" ]; then
                    echo "HIT=${{MOCK_STAGED_HIT}}"
                else
                    echo "MISS"
                fi
                ;;
            *"stage-pkg"*)
                echo "OK_STAGED=verified"
                ;;
            *"extract-esp-on-device"*)
                echo "OK_ESP_EXTRACTED=dummy"
                ;;
            *"apply-full-esp-and-pending"*)
                echo "OK_OFFLINE_STAGED"
                ;;
            *"apply-manifest"*)
                echo "OK_ONLINE_APPLIED"
                ;;
            *"reboot"*)
                echo "REBOOTING"
                ;;
        esac
        ;;
esac
''')
            mock_adb.chmod(0o755)

            # 1) Legacy v0.7.0 (HAS_OTA_INFO=0) MUST reject ESP-only delta archive!
            p_reject = subprocess.run(
                ['sh', str(script), '--adb', str(mock_adb), '-p', delta_ota['archive'], '--yes'],
                env=dict(os.environ, MOCK_HAS_OTA='0'), text=True, capture_output=True,
            )
            self.assertNotEqual(p_reject.returncode, 0)
            self.assertIn('legacy build', p_reject.stderr)

            # 2) Legacy v0.7.0 accepts full .img.gz with --keep-data and schedules APPLY_SUPER=1, WIPE_DATA=0
            log_file.write_text('')
            p_full = subprocess.run(
                ['sh', str(script), '--adb', str(mock_adb), '-p', str(img_gz), '--version', 'v0.8.0', '--yes'],
                env=dict(os.environ, MOCK_HAS_OTA='0'), text=True, capture_output=True,
            )
            self.assertEqual(p_full.returncode, 0, p_full.stderr + p_full.stdout)
            calls = log_file.read_text()
            self.assertIn(f'extract-esp-on-device full-img-gz {parts[1]["offset"]} {parts[1]["bytes"]}', calls)
            self.assertIn('apply-full-esp-and-pending', calls)
            self.assertIn(' full-img-gz 0 v0.8.0 ', calls)
            self.assertIn('reboot', calls)

            # 3) Full .img.gz with --wipe-data passes WIPE_DATA=1
            log_file.write_text('')
            p_wipe = subprocess.run(
                ['sh', str(script), '--adb', str(mock_adb), '-p', str(img_gz), '--wipe-data', '--no-reboot', '--yes'],
                env=dict(os.environ, MOCK_HAS_OTA='0'), text=True, capture_output=True,
            )
            self.assertEqual(p_wipe.returncode, 0, p_wipe.stderr + p_wipe.stdout)
            calls = log_file.read_text()
            self.assertIn(' full-img-gz 1 full-image ', calls)
            self.assertNotIn('spx-adb-helper.sh reboot', calls)

            # 4) Device with matching super SHA accepts delta OTA online
            log_file.write_text('')
            p_delta = subprocess.run(
                ['sh', str(script), '--adb', str(mock_adb), '-p', delta_ota['archive'], '--yes'],
                env=dict(os.environ, MOCK_HAS_OTA='1', MOCK_SUPER_SHA=delta_ota['super_sha256']),
                text=True, capture_output=True,
            )
            self.assertEqual(p_delta.returncode, 0, p_delta.stderr + p_delta.stdout)
            self.assertIn(f'apply-manifest {delta_ota["archive_sha256"]} 0 0 v0.8.0', log_file.read_text())

            # 5) Already-staged matching package skips redundant adb push
            log_file.write_text('')
            img_gz_sha = hashlib.sha256(img_gz.read_bytes()).hexdigest()
            p_hit = subprocess.run(
                ['sh', str(script), '--adb', str(mock_adb), '-p', str(img_gz), '--no-reboot', '--yes'],
                env=dict(os.environ, MOCK_HAS_OTA='0', MOCK_STAGED_HIT=img_gz_sha),
                text=True, capture_output=True,
            )
            self.assertEqual(p_hit.returncode, 0, p_hit.stderr + p_hit.stdout)
            calls_hit = log_file.read_text()
            self.assertNotIn('/data/local/tmp/spx-ota-upload.pkg', calls_hit)
            self.assertIn('apply-full-esp-and-pending', calls_hit)

    def test_grub_cfg_repair_and_dualboot_preservation(self):
        """Ensure legacy/trial grub.cfg (e.g. SQ2N19 + grubenv) is repaired to SQ2Nat while Windows dual-boot is preserved."""
        ps1_text = (ROOT / 'tools/Upgrade-GooglebookViaAdb.ps1').read_text(encoding='utf-8')
        m = re.search(r"(?s)\$deviceHelper = @'\r?\n(.*?)\r?\n'@", ps1_text)
        self.assertIsNotNone(m, 'Missing embedded $deviceHelper in Upgrade-GooglebookViaAdb.ps1')
        helper_src = m.group(1)
        # Extract helper functions up to `case "$CMD" in`
        fn_block = helper_src.split('case "$CMD" in', 1)[0]

        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            src_cfg = td / 'src-grub.cfg'
            src_cfg.write_text(
                "set timeout=5\nset default=spx-single-user\n"
                "menuentry 'Surface Pro X Linux 6.18' --id spx-single-user {\n"
                " chainloader /EFI/SQ2Nat/start.efi\n}\n"
                "menuentry 'Rescue' --id spx-rescue {\n"
                " chainloader /EFI/SQ2Res/start.efi\n}\n"
            )

            def run_ensure(esp_dir):
                p = subprocess.run(
                    ['sh', '-c', f'{fn_block}\nensure_grub_boots_sq2nat "$1" "$2"', '--', 'dummy-cmd', str(esp_dir), str(src_cfg)],
                    text=True, capture_output=True,
                )
                self.assertEqual(p.returncode, 0, p.stderr + p.stdout)
                return p.stdout.strip()

            # Case 1: Legacy/trial grub.cfg (SQ2N19 default) + stale grubenv (SQ2KSU next_entry)
            esp1 = td / 'esp1'
            (esp1 / 'EFI/BOOT').mkdir(parents=True)
            legacy_cfg = (
                "set timeout=3\nset default=spx-hybrid-5194544\n"
                "menuentry 'Surface Pro X Hybrid 5194544' --id spx-hybrid-5194544 {\n"
                " chainloader /EFI/SQ2N19/start.efi\n}\n"
            )
            (esp1 / 'EFI/BOOT/grub.cfg').write_text(legacy_cfg)
            (esp1 / 'EFI/BOOT/grubenv').write_text('# GRUB Environment Block\nnext_entry=spx-ksu342-trial\n')
            out1 = run_ensure(esp1)
            self.assertIn('GRUB_ACTION=replaced_legacy_grub', out1)
            self.assertIn('/EFI/SQ2Nat/start.efi', (esp1 / 'EFI/BOOT/grub.cfg').read_text())
            self.assertEqual((esp1 / 'EFI/BOOT/grub.cfg.ota-bak').read_text(), legacy_cfg)
            self.assertFalse((esp1 / 'EFI/BOOT/grubenv').exists())
            self.assertTrue((esp1 / 'EFI/BOOT/grubenv.ota-bak').is_file())

            # Case 2: Windows dual-boot grub.cfg already pointing to SQ2Nat -> preserved untouched
            esp2 = td / 'esp2'
            (esp2 / 'EFI/BOOT').mkdir(parents=True)
            dual_cfg = (
                "set timeout=7\nset default=windows\n"
                "menuentry 'Googlebook OS' --id spx-single-user {\n"
                " chainloader /EFI/SQ2Nat/start.efi\n}\n"
                "menuentry 'Windows' --id windows {\n"
                " chainloader /EFI/Microsoft/Boot/bootmgfw.efi\n}\n"
            )
            (esp2 / 'EFI/BOOT/grub.cfg').write_text(dual_cfg)
            out2 = run_ensure(esp2)
            self.assertIn('GRUB_ACTION=preserved_dualboot', out2)
            self.assertEqual((esp2 / 'EFI/BOOT/grub.cfg').read_text(), dual_cfg)

            # Case 3: Windows dual-boot grub.cfg with legacy SQ2N19 + SQ2Res -> rewrites only SQ2N19
            esp3 = td / 'esp3'
            (esp3 / 'EFI/BOOT').mkdir(parents=True)
            dual_legacy = (
                "set timeout=5\nset default=spx-single-user\n"
                "menuentry 'Googlebook OS' --id spx-single-user {\n"
                " chainloader /EFI/SQ2N19/start.efi\n}\n"
                "menuentry 'Rescue' --id spx-rescue {\n"
                " chainloader /EFI/SQ2Res/start.efi\n}\n"
                "menuentry 'Windows' --id windows {\n"
                " chainloader /EFI/Microsoft/Boot/bootmgfw.efi\n}\n"
            )
            (esp3 / 'EFI/BOOT/grub.cfg').write_text(dual_legacy)
            out3 = run_ensure(esp3)
            self.assertIn('GRUB_ACTION=repaired_dualboot', out3)
            repaired3 = (esp3 / 'EFI/BOOT/grub.cfg').read_text()
            self.assertIn('/EFI/SQ2Nat/start.efi', repaired3)
            self.assertIn('/EFI/SQ2Res/start.efi', repaired3)
            self.assertIn('/EFI/Microsoft/Boot/bootmgfw.efi', repaired3)
            self.assertNotIn('SQ2N19', repaired3)

            # Case 4: Custom single-OS grub.cfg already defaulting to spx-single-user -> preserved
            esp4 = td / 'esp4'
            (esp4 / 'EFI/BOOT').mkdir(parents=True)
            custom_sq2nat = (
                "set timeout=1\nset default=spx-single-user\n"
                "menuentry 'My Custom Googlebook' --id spx-single-user {\n"
                " chainloader /EFI/SQ2Nat/start.efi\n}\n"
            )
            (esp4 / 'EFI/BOOT/grub.cfg').write_text(custom_sq2nat)
            out4 = run_ensure(esp4)
            self.assertIn('GRUB_ACTION=preserved_sq2nat', out4)
            self.assertEqual((esp4 / 'EFI/BOOT/grub.cfg').read_text(), custom_sq2nat)

            # Case 5: copy_esp_atomic must NEVER clobber caller's _exp_sha (POSIX sh shares globals!)
            src_f = td / 'dummy-kernel'
            src_f.write_bytes(b'KERNEL-BYTES')
            esp5 = td / 'esp5'
            esp5.mkdir()
            p5 = subprocess.run(
                ['sh', '-c', f'{fn_block}\n_exp_sha="abcdef0123456789"\ncopy_esp_atomic "$1" "$2" "EFI/SQ2Nat/Image" ""\necho "AFTER=$_exp_sha"', '--', 'dummy-cmd', str(src_f), str(esp5)],
                text=True, capture_output=True,
            )
            self.assertEqual(p5.returncode, 0, p5.stderr + p5.stdout)
            self.assertIn('AFTER=abcdef0123456789', p5.stdout)

    def test_windows_powershell_adb_upgrade_script(self):
        ps1 = ROOT / 'tools/Upgrade-GooglebookViaAdb.ps1'
        cmd = ROOT / 'tools/Upgrade-GooglebookViaAdb.cmd'
        self.assertTrue(ps1.is_file() and cmd.is_file())
        ps_exe = Path('/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe')
        if not ps_exe.is_file():
            self.skipTest('Windows powershell.exe not available on this host')

        # Use a Windows-accessible temp dir for powershell.exe interoperability
        win_tmp = Path('/mnt/c/Windows/Temp')
        if not win_tmp.is_dir():
            self.skipTest('/mnt/c/Windows/Temp unavailable')

        with tempfile.TemporaryDirectory(dir=win_tmp, prefix='spx-ps-ota-') as td:
            td = Path(td)
            super_data = b'P' * (1024 * 1024)
            esp_data = b'Q' * (1024 * 1024)
            img = td / 'test.img'
            parts = make_synthetic_gpt_image(img, super_data, esp_data)
            img_gz = td / 'test.img.gz'
            img_gz.write_bytes(gzip.compress(img.read_bytes(), compresslevel=1))

            esp_tree = make_esp_tree(td / 'esp', release='v0.8.0')
            delta_ota = ota_pkg.build_ota_package(
                output_dir=td / 'ota', esp_tree=esp_tree, image=img,
                mode='auto', super_unchanged_hint=True, version='v0.8.0',
            )

            ps1_copy = td / 'Upgrade-GooglebookViaAdb.ps1'
            ps1_copy.write_bytes(ps1.read_bytes())

            def to_win(p):
                rel = Path(p).resolve().relative_to('/mnt/c')
                return 'C:\\' + str(rel).replace('/', '\\')

            test_ps = td / 'run-test.ps1'
            out_esp = td / 'extracted-esp.fat'
            test_ps.write_text(
                '$ErrorActionPreference = "Stop"\n'
                f'$ps1 = "{to_win(ps1_copy)}"\n'
                '$tokens = $null; $errs = $null\n'
                '$ast = [System.Management.Automation.Language.Parser]::ParseFile($ps1, [ref]$tokens, [ref]$errs)\n'
                'if ($errs -and $errs.Count -gt 0) { throw ("PS1 parse error: " + ($errs | Out-String)) }\n'
                # Extract and compile embedded C# SpxAdbOtaNative directly
                '$raw = Get-Content -LiteralPath $ps1 -Raw\n'
                '$m = [regex]::Match($raw, "(?s)\\$nativeCode = @\'\\r?\\n(.*?)\\r?\\n\'@")\n'
                'if (-not $m.Success) { throw "Could not find embedded C# block" }\n'
                'Add-Type -TypeDefinition $m.Groups[1].Value -Language CSharp\n'
                f'$g = [SpxAdbOtaNative]::InspectPackage("{to_win(img_gz)}")\n'
                f'if ($g.Kind -ne "full-img-gz" -or $g.SuperOffset -ne {parts[0]["offset"]} -or $g.EspBytes -ne {parts[1]["bytes"]}) {{ throw "Bad GPT inspect" }}\n'
                f'$t = [SpxAdbOtaNative]::InspectPackage("{to_win(delta_ota["archive"])}")\n'
                'if ($t.Kind -ne "manifest" -or $t.Version -ne "v0.8.0" -or $t.SuperIncluded) { throw "Bad manifest inspect" }\n'
                f'[SpxAdbOtaNative]::ExtractRegion("{to_win(img_gz)}", $true, {parts[1]["offset"]}, {parts[1]["bytes"]}, "{to_win(out_esp)}")\n'
                'Write-Output "POWERSHELL_OTA_TEST_OK"\n',
                encoding='utf-8',
            )
            p = subprocess.run(
                [str(ps_exe), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', to_win(test_ps)],
                text=True, capture_output=True,
            )
            self.assertEqual(p.returncode, 0, p.stderr + p.stdout)
            self.assertIn('POWERSHELL_OTA_TEST_OK', p.stdout)
            self.assertEqual(out_esp.read_bytes(), esp_data)


    def test_ota_stage_promotes_applied_pending_reboot_to_ok(self):
        with tempfile.TemporaryDirectory() as td:
            esp_dir = Path(td)
            res_file = esp_dir / 'SPX-OTA-RESULT.txt'
            res_file.write_text(
                'STATUS=APPLIED_PENDING_REBOOT\n'
                'VERSION=v0.7.6-test\n'
                'PACKAGE_KIND=manifest\n'
                'APPLY_SUPER=0\n'
                'WIPE_DATA=0\n'
                'ERROR=\n',
                encoding='utf-8',
            )
            p = subprocess.run(
                ['sh', str(ROOT / 'probe/ota/spx-ota-stage')],
                env=dict(os.environ, SPX_OTA_ESP=str(esp_dir), SPX_OTA_RUN=str(esp_dir / 'run')),
                text=True,
                capture_output=True,
            )
            self.assertEqual(p.returncode, 0, p.stderr + p.stdout)
            updated = res_file.read_text(encoding='utf-8')
            self.assertIn('STATUS=OK\n', updated)
            self.assertNotIn('APPLIED_PENDING_REBOOT', updated)
            self.assertIn('VERSION=v0.7.6-test\n', updated)

    def test_ota_stage_online_pending_reboot_refreshes_package_and_vendor_dex_cache(self):
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            esp_dir = td / 'esp'
            esp_dir.mkdir(parents=True)
            res_file = esp_dir / 'SPX-OTA-RESULT.txt'
            res_file.write_text(
                'STATUS=APPLIED_PENDING_REBOOT\n'
                'VERSION=v0.9.1-test\n'
                'PACKAGE_KIND=manifest\n'
                'APPLY_SUPER=0\n'
                'WIPE_DATA=0\n'
                'ERROR=\n',
                encoding='utf-8',
            )
            data_dir = td / 'userdata'
            (data_dir / 'vendor/sq2-touch-calibration').mkdir(parents=True)
            (data_dir / 'vendor/sq2-touch-calibration/dev.profile').write_text('CALIB=KEEP\n')
            (data_dir / 'system/package_cache/1').mkdir(parents=True)
            (data_dir / 'system/package_cache/1/SurfaceAdvancedControl-abc123').write_text('stale-pkg-cache\n')
            (data_dir / 'system/packages.xml').write_text('<packages/>\n')
            (data_dir / 'dalvik-cache/arm64').mkdir(parents=True)
            (data_dir / 'dalvik-cache/arm64/vendor@app@SurfaceAdvancedControl@SurfaceAdvancedControl.apk@classes.dex').write_text('stale-dex\n')
            (data_dir / 'dalvik-cache/arm64/vendor@overlay@spx-systemui-overlay.apk@classes.vdex').write_text('stale-vdex\n')
            (data_dir / 'dalvik-cache/arm64/system@framework@boot.art').write_text('keep-boot-art\n')
            (data_dir / 'misc/apexdata/com.android.art/dalvik-cache/arm64').mkdir(parents=True)
            (data_dir / 'misc/apexdata/com.android.art/dalvik-cache/arm64/vendor@app@SurfaceAdvancedControl@SurfaceAdvancedControl.apk@classes.odex').write_text('stale-odex\n')

            env = dict(
                os.environ,
                SPX_OTA_ESP=str(esp_dir),
                SPX_OTA_RUN=str(td / 'run'),
                SPX_OTA_DATA_DIR=str(data_dir),
            )
            p = subprocess.run(['sh', str(ROOT / 'probe/ota/spx-ota-stage')], env=env, text=True, capture_output=True)
            self.assertEqual(p.returncode, 0, p.stderr + p.stdout)
            updated = res_file.read_text(encoding='utf-8')
            self.assertIn('STATUS=OK\n', updated)
            self.assertNotIn('APPLIED_PENDING_REBOOT', updated)
            self.assertTrue((data_dir / 'system/package_cache').is_dir())
            self.assertEqual(list((data_dir / 'system/package_cache').iterdir()), [])
            self.assertFalse((data_dir / 'dalvik-cache/arm64/vendor@app@SurfaceAdvancedControl@SurfaceAdvancedControl.apk@classes.dex').exists())
            self.assertFalse((data_dir / 'dalvik-cache/arm64/vendor@overlay@spx-systemui-overlay.apk@classes.vdex').exists())
            self.assertFalse((data_dir / 'misc/apexdata/com.android.art/dalvik-cache/arm64/vendor@app@SurfaceAdvancedControl@SurfaceAdvancedControl.apk@classes.odex').exists())
            self.assertEqual((data_dir / 'system/packages.xml').read_text(), '<packages/>\n')
            self.assertEqual((data_dir / 'dalvik-cache/arm64/system@framework@boot.art').read_text(), 'keep-boot-art\n')
            self.assertEqual((data_dir / 'vendor/sq2-touch-calibration/dev.profile').read_text(), 'CALIB=KEEP\n')

            # Normal boots (STATUS=OK already) must NOT purge package_cache.
            sentinel = data_dir / 'system/package_cache/keep-on-normal-boot'
            sentinel.write_text('keep\n')
            p2 = subprocess.run(['sh', str(ROOT / 'probe/ota/spx-ota-stage')], env=env, text=True, capture_output=True)
            self.assertEqual(p2.returncode, 0, p2.stderr + p2.stdout)
            self.assertTrue(sentinel.is_file())

    def test_adb_helper_probe_ota_info_fallbacks_and_syntax(self):
        ps1_text = (ROOT / 'tools/Upgrade-GooglebookViaAdb.ps1').read_text(encoding='utf-8')
        m_ps1 = re.search(r"(?s)\$deviceHelper = @'\r?\n(.*?)\r?\n'@", ps1_text)
        self.assertIsNotNone(m_ps1)
        sh_text = (ROOT / 'tools/upgrade-googlebook-via-adb.sh').read_text(encoding='utf-8')
        m_sh = re.search(r"(?s)<<'DEVICE_EOF'\n(.*?)\nDEVICE_EOF", sh_text)
        self.assertIsNotNone(m_sh)
        for label, helper_src in (('ps1', m_ps1.group(1).replace('\r\n', '\n')), ('sh', m_sh.group(1))):
            p = subprocess.run(['sh', '-n'], input=helper_src, text=True, capture_output=True)
            self.assertEqual(p.returncode, 0, f'{label} sh -n failed: {p.stderr}')
            self.assertIn('/data/user_de/0/org.surface.advancedcontrol/files/ota-state', helper_src)
            self.assertIn('/proc/[0-9]*/root/etc/spx-ota-info', helper_src)
            self.assertIn('EFI/SPX/LTE/*', helper_src)

    def test_ota_preserves_device_lte_fsg_cache_on_esp(self):
        self.assertTrue(ota_pkg.is_preserved_esp_path('EFI/SPX/LTE/12345678-1234-1234-1234-123456789abc.bin'))
        for rel in ('probe/ota/spx-ota-stage', 'probe/ota/spx-ota-boot'):
            src = (ROOT / rel).read_text(encoding='utf-8')
            self.assertIn('EFI/SPX/LTE/*', src)
            p = subprocess.run(['sh', '-n'], input=src, text=True, capture_output=True)
            self.assertEqual(p.returncode, 0, f'{rel} sh -n failed: {p.stderr}')

    def test_full_image_zero_tail_truncation_dalvik_refresh_and_nonzero_rejection(self):
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            mib = 1024 * 1024
            payload_2m = b'S' * (2 * mib)
            padded_super_4m = payload_2m + b'\x00' * (2 * mib)
            esp_raw = b'E' * mib

            img_zero_tail = td / 'padded-4m.img'
            parts = make_synthetic_gpt_image(img_zero_tail, padded_super_4m, esp_raw)
            img_zero_gz = td / 'padded-4m.img.gz'
            img_zero_gz.write_bytes(gzip.compress(img_zero_tail.read_bytes(), compresslevel=1))
            img_zero_sha = hashlib.sha256(img_zero_gz.read_bytes()).hexdigest()

            # 1) spx-ota-tool capabilities & inspect-super-used
            caps = subprocess.check_output([str(self.host_tool), 'capabilities'], text=True).splitlines()
            self.assertIn('inspect-super-used', caps)
            self.assertIn('extract-stream-zero-tail', caps)

            used_out = subprocess.check_output(
                f'gzip -dc "{img_zero_gz}" | "{self.host_tool}" inspect-super-used - {parts[0]["offset"]} {parts[0]["bytes"]}',
                shell=True, text=True,
            )
            used_kv = dict(line.split('=', 1) for line in used_out.strip().splitlines())
            self.assertEqual(int(used_kv['SUPER_TOTAL_BYTES']), 4 * mib)
            self.assertEqual(int(used_kv['SUPER_NONZERO_END']), 2 * mib)
            self.assertEqual(int(used_kv['SUPER_USED_BYTES']), 2 * mib)
            self.assertEqual(int(used_kv['SUPER_TAIL_ZERO_BYTES']), 2 * mib)

            # 2) build_ota_package --mode full trims zero-padded super to compacted size
            esp_src = make_esp_tree(td / 'esp-src', kernel=b'K-FULL', initramfs=b'I-FULL', fw_extra=b'FW-FULL', release='v-full')
            full_ota = ota_pkg.build_ota_package(
                output_dir=td / 'ota-full',
                esp_tree=esp_src,
                image=img_zero_tail,
                mode='full',
                version='v-full',
            )
            self.assertTrue(full_ota['super_included'])
            self.assertEqual(full_ota['super_bytes'], 2 * mib)
            self.assertEqual(full_ota['super_sha256'], hashlib.sha256(payload_2m).hexdigest())
            with tarfile.open(full_ota['archive'], 'r:gz') as tar:
                self.assertEqual(tar.extractfile('payload/super.img').read(), payload_2m)

            # 3) spx-ota-boot accepts 4 MiB zero-tail full-img-gz on 3 MiB super device
            data_root = td / 'android-root'
            app_files = data_root / 'data/user_de/0/org.surface.advancedcontrol/files'
            app_files.mkdir(parents=True)
            live_esp = make_esp_tree(td / 'live-esp', kernel=b'K-OLD', initramfs=b'I-OLD', fw_extra=b'FW-OLD', release='v-old')
            etc_dir = td / 'etc'
            etc_dir.mkdir()
            (etc_dir / 'spx-ota-info').write_text('SPX_OTA_VERSION=v-old\nSPX_OTA_SUPER_SHA256=old\n')
            super_dev = td / 'super-3m.dev'
            orig_super = b'O' * (3 * mib)
            super_dev.write_bytes(orig_super)

            wrapper = td / 'android-wrap.sh'
            wrapper.write_text('#!/bin/sh\nexit 0\n')
            wrapper.chmod(0o755)
            boot_env = dict(
                os.environ,
                SPX_OTA_BOOT_WAIT='0',
                SPX_OTA_ONCE='1',
                SPX_OTA_TEST_USER='0',
                SPX_OTA_ROOT=str(data_root),
                SPX_OTA_ESP=str(live_esp),
                SPX_OTA_RUN=str(td / 'boot-run'),
                SPX_OTA_ETC=str(etc_dir),
                SPX_OTA_TOOL=str(self.host_tool),
                SPX_OTA_SUPER_DEV=str(super_dev),
                SPX_OTA_LOCK_DIR=str(td / 'boot-lock'),
                SPX_OTA_TMP_REQ=str(td / 'boot-req'),
                SPX_OTA_ANDROID_WRAPPER=str(wrapper),
                SPX_OTA_NO_REBOOT='1',
                SPX_OTA_CLAIM_INTENT='0',
            )
            (td / 'boot-run').mkdir()
            (app_files / 'full.img.gz').write_bytes(img_zero_gz.read_bytes())
            (app_files / 'ota-request').write_text('ota-inspect\nn-ok\nfull.img.gz\n')
            p_boot = subprocess.run(['sh', str(ROOT / 'probe/ota/spx-ota-boot')], env=boot_env, text=True, capture_output=True)
            self.assertEqual(p_boot.returncode, 0, p_boot.stderr)
            reply_ok = (app_files / 'ota-reply').read_text()
            self.assertTrue(reply_ok.startswith('n-ok\nOK\n'), reply_ok)
            self.assertIn(f'super_bytes={2 * mib}\n', reply_ok)

            # 4) spx-ota-stage applies 4 MiB zero-tail full-img-gz to 3 MiB super.dev + clears full dalvik-cache
            stage_data = td / 'stage-userdata'
            (stage_data / 'ota').mkdir(parents=True)
            (stage_data / 'ota/pending.pkg').write_bytes(img_zero_gz.read_bytes())
            (stage_data / 'user-keep.txt').write_text('KEEP-ME\n')
            (stage_data / 'dalvik-cache/arm64').mkdir(parents=True)
            (stage_data / 'dalvik-cache/arm64/system@framework@boot.art').write_text('stale-boot-art\n')
            (live_esp / 'SPX-OTA-PENDING').write_text(
                'PACKAGE_REL=ota/pending.pkg\n'
                f'PACKAGE_SHA256={img_zero_sha}\n'
                'PACKAGE_KIND=full-img-gz\n'
                'APPLY_SUPER=1\n'
                'WIPE_DATA=0\n'
                'VERSION=v-full\n'
            )
            stage_env = dict(
                os.environ,
                SPX_OTA_ESP=str(live_esp),
                SPX_OTA_RUN=str(td / 'stage-run'),
                SPX_OTA_TOOL=str(self.host_tool),
                SPX_OTA_SUPER_DEV=str(super_dev),
                SPX_OTA_DATA_DIR=str(stage_data),
                SPX_OTA_ESP_SRC_DIR=str(esp_src),
                SPX_OTA_NO_REBOOT='1',
            )
            p_stage = subprocess.run(['sh', str(ROOT / 'probe/ota/spx-ota-stage')], env=stage_env, text=True, capture_output=True)
            self.assertEqual(p_stage.returncode, 0, p_stage.stderr + p_stage.stdout)
            self.assertIn('STATUS=OK\n', (live_esp / 'SPX-OTA-RESULT.txt').read_text())
            self.assertEqual(super_dev.read_bytes(), payload_2m + b'\x00' * mib)
            self.assertEqual((live_esp / 'EFI/SQ2Nat/initramfs.cpio.gz').read_bytes(), b'I-FULL')
            self.assertEqual((stage_data / 'user-keep.txt').read_text(), 'KEEP-ME\n')
            self.assertFalse((stage_data / 'dalvik-cache/arm64/system@framework@boot.art').exists())

            # 5) Non-zero tail past 3 MiB must be rejected read-only without modifying super_dev
            bad_super_4m = payload_2m + b'\x00' * (mib + 4096) + b'X' + b'\x00' * (mib - 4097)
            img_bad = td / 'bad-4m.img'
            bad_parts = make_synthetic_gpt_image(img_bad, bad_super_4m, esp_raw)
            img_bad_gz = td / 'bad-4m.img.gz'
            img_bad_gz.write_bytes(gzip.compress(img_bad.read_bytes(), compresslevel=1))
            img_bad_sha = hashlib.sha256(img_bad_gz.read_bytes()).hexdigest()

            (app_files / 'bad.img.gz').write_bytes(img_bad_gz.read_bytes())
            (app_files / 'ota-request').write_text('ota-inspect\nn-bad\nbad.img.gz\n')
            p_boot_bad = subprocess.run(['sh', str(ROOT / 'probe/ota/spx-ota-boot')], env=boot_env, text=True, capture_output=True)
            self.assertEqual(p_boot_bad.returncode, 0, p_boot_bad.stderr)
            reply_bad = (app_files / 'ota-reply').read_text()
            self.assertTrue(reply_bad.startswith('n-bad\nERROR\nsuper_partition_too_small'), reply_bad)

            sentinel_super = b'Z' * (3 * mib)
            super_dev.write_bytes(sentinel_super)
            (stage_data / 'ota/pending.pkg').write_bytes(img_bad_gz.read_bytes())
            (live_esp / 'SPX-OTA-PENDING').write_text(
                'PACKAGE_REL=ota/pending.pkg\n'
                f'PACKAGE_SHA256={img_bad_sha}\n'
                'PACKAGE_KIND=full-img-gz\n'
                'APPLY_SUPER=1\n'
                'WIPE_DATA=0\n'
                'VERSION=v-bad\n'
            )
            p_stage_bad = subprocess.run(['sh', str(ROOT / 'probe/ota/spx-ota-stage')], env=stage_env, text=True, capture_output=True)
            self.assertNotEqual(p_stage_bad.returncode, 0)
            self.assertIn('ERROR=super_partition_too_small\n', (live_esp / 'SPX-OTA-RESULT.txt').read_text())
            self.assertEqual(super_dev.read_bytes(), sentinel_super)

            # Direct extract-stream also rejects non-zero tail
            p_ext_bad = subprocess.run(
                f'gzip -dc "{img_bad_gz}" | "{self.host_tool}" extract-stream "{td / "out-bad.bin"}" '
                f'{bad_parts[0]["offset"]} {bad_parts[0]["bytes"]} - 0 0 {3 * mib}',
                shell=True, text=True, capture_output=True,
            )
            self.assertNotEqual(p_ext_bad.returncode, 0)
            self.assertIn('non-zero byte in truncated SUPER tail', p_ext_bad.stderr)


if __name__ == '__main__':
    unittest.main()
