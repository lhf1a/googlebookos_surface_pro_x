import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))

import build_ota_package as bop
import disk_image as di
import full_image as fi
import spx_super_delta as ssd
import track_mica_ota as tmo

M = 1048576
SLICE_NAMES = ('system_a', 'vendor_a', 'product_a', 'system_ext_a', 'system_dlkm_a', 'vendor_dlkm_a')


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
    return h.hexdigest()


def make_fake_erofs(path, blocks, seed, mutate=None):
    """Create a deterministic synthetic EROFS-header image with valid magic at +1024."""
    buf = bytearray(blocks * 4096)
    for b in range(blocks):
        tag = hashlib.sha256(f'{seed}:{b}'.encode()).digest()
        blk = (tag * 128)[:4096]
        buf[b * 4096:(b + 1) * 4096] = blk
    # Plant EROFS superblock magic at offset 1024.
    buf[1024:1028] = b'\xe2\xe1\xf5\xe0'
    if mutate is not None:
        mutate(buf)
    Path(path).write_bytes(bytes(buf))
    return bytes(buf)


def build_mock_esp_tree(esp_dir, marker_extra='v286'):
    esp = Path(esp_dir)
    (esp / 'EFI/SQ2Nat').mkdir(parents=True, exist_ok=True)
    (esp / 'EFI/BOOT').mkdir(parents=True, exist_ok=True)
    (esp / 'SPX-IMAGE.txt').write_text(f'SPX_IMAGE_FORMAT=1\nSPX_RELEASE={marker_extra}\n', encoding='utf-8')
    (esp / 'EFI/SQ2Nat/Image').write_bytes(b'KERNEL-' + marker_extra.encode())
    (esp / 'EFI/SQ2Nat/initramfs.cpio.gz').write_bytes(b'INITRD-' + marker_extra.encode())
    (esp / 'EFI/BOOT/grub.cfg').write_text("set default=spx-single-user\nchainloader /EFI/SQ2Nat/start.efi\n", encoding='utf-8')
    return esp


class SpxSuperDeltaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory(prefix='spx-super-delta-tests-')
        cls.host_tool = Path(cls._tmp.name) / 'spx-ota-tool'
        subprocess.run(
            ['gcc', '-std=c11', '-Os', '-Wall', '-Wextra', '-Werror',
             str(ROOT / 'probe/ota/spx-ota-tool.c'), '-o', str(cls.host_tool)],
            check=True,
        )

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def test_spxdlt01_roundtrip_and_negative_cases(self):
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            src = td / 'src.img'
            dst = td / 'dst.img'
            delta = td / 'slice.spxdlt'
            rebuilt = td / 'rebuilt.img'

            # 16 blocks = 64 KiB source; destination moves blocks, inserts zero run, mutates blocks, and adds non-aligned tail.
            src_data = make_fake_erofs(src, 16, 'slice-base')
            dst_buf = bytearray()
            dst_buf.extend(src_data[0:4 * 4096])            # identical prefix (4 blocks)
            dst_buf.extend(b'\x00' * (3 * 4096))            # zero run (3 blocks)
            dst_buf.extend(src_data[10 * 4096:14 * 4096])   # moved contiguous run (4 blocks)
            dst_buf.extend(b'\xab' * 4096)                  # novel literal block
            dst_buf.extend(src_data[4 * 4096:6 * 4096])     # moved earlier run (2 blocks)
            dst_buf.extend(b'TAIL-UNALIGNED-123')           # non-4KiB tail
            dst.write_bytes(bytes(dst_buf))

            stats = ssd.generate_slice_delta(src, dst, delta)
            self.assertEqual(stats['copy_blocks'], 10)
            self.assertEqual(stats['zero_blocks'], 3)
            self.assertEqual(stats['literal_bytes'], 4096 + len(b'TAIL-UNALIGNED-123'))
            self.assertLess(stats['delta_bytes'], len(dst_buf) // 3)

            # Apply from stdin stream using host-compiled spx-ota-tool.
            proc = subprocess.run(
                [str(self.host_tool), 'apply-delta', str(src), '0', str(len(src_data)), str(rebuilt), '-'],
                input=delta.read_bytes(),
                capture_output=True,
                check=True,
            )
            out_text = proc.stdout.decode()
            self.assertIn('DELTA_VERIFIED=1', out_text)
            self.assertIn(f"DELTA_DST_SHA256={stats['dst_sha256']}", out_text)
            self.assertEqual(rebuilt.read_bytes(), bytes(dst_buf))

            # Negative 1: wrong source bytes -> fail closed before writing destination.
            bad_src = td / 'bad_src.img'
            bad_bytes = bytearray(src_data)
            bad_bytes[4096] ^= 0xff
            bad_src.write_bytes(bytes(bad_bytes))
            rebuilt_bad = td / 'rebuilt_bad.img'
            p_bad = subprocess.run(
                [str(self.host_tool), 'apply-delta', str(bad_src), '0', str(len(src_data)), str(rebuilt_bad), str(delta)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(p_bad.returncode, 0)
            self.assertIn('DELTA_SRC_SHA256_MISMATCH', p_bad.stderr)
            self.assertFalse(rebuilt_bad.exists())

            # Negative 2: corrupted literal byte in delta -> fail closed & unlink partial output.
            corrupt_delta = td / 'corrupt.spxdlt'
            db = bytearray(delta.read_bytes())
            db[-5] ^= 0x01
            corrupt_delta.write_bytes(bytes(db))
            rebuilt_corrupt = td / 'rebuilt_corrupt.img'
            p_corrupt = subprocess.run(
                [str(self.host_tool), 'apply-delta', str(src), '0', str(len(src_data)), str(rebuilt_corrupt), str(corrupt_delta)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(p_corrupt.returncode, 0)
            self.assertIn('DELTA_DST_SHA256_MISMATCH', p_corrupt.stderr)
            self.assertFalse(rebuilt_corrupt.exists())

    def _make_6_slice_fixture(self, root):
        base_dir = root / 'base_slices'
        target_dir = root / 'target_slices'
        base_dir.mkdir(parents=True)
        target_dir.mkdir(parents=True)

        # Compact synthetic 6-slice images (each 64 KiB..128 KiB -> 1 MiB aligned = 7 MiB total super).
        for idx, name in enumerate(SLICE_NAMES):
            base_blocks = 16 + idx * 2
            base_bytes = make_fake_erofs(base_dir / f'{name}.img', base_blocks, f'base-{name}')
            if name == 'vendor_a':
                # Simulate trimmed vendor: slightly smaller than base, different content.
                def _mut(b):
                    b[4096:8192] = b'\x5a' * 4096
                make_fake_erofs(target_dir / f'{name}.img', base_blocks - 2, f'target-{name}', mutate=_mut)
            else:
                # Most blocks preserved, 2 blocks mutated.
                tbuf = bytearray(base_bytes)
                tbuf[2 * 4096:4 * 4096] = bytes([0x33 + idx]) * (2 * 4096)
                (target_dir / f'{name}.img').write_bytes(bytes(tbuf))

        base_super_bytes, base_plan, base_slices_txt = di.compact_super_slices_from_dir(base_dir, SLICE_NAMES)
        target_super_bytes, target_plan, target_slices_txt = di.compact_super_slices_from_dir(target_dir, SLICE_NAMES)
        target_super_sha = di.compute_super_sha256_from_files(target_super_bytes, target_plan)

        # Build an initial super image file on "device" using base_plan.
        dev_super = root / 'device_super.img'
        with dev_super.open('wb') as f:
            f.truncate(max(base_super_bytes, target_super_bytes))
            for _, src_p, off, _ in base_plan:
                f.seek(off)
                f.write(src_p.read_bytes())

        return {
            'base_dir': base_dir,
            'target_dir': target_dir,
            'base_super_bytes': base_super_bytes,
            'base_slices_txt': base_slices_txt,
            'target_super_bytes': target_super_bytes,
            'target_slices_txt': target_slices_txt,
            'target_super_sha': target_super_sha,
            'dev_super': dev_super,
        }

    def test_end_to_end_spx_ota_stage_super_delta_and_prebootstrapped_offsets(self):
        for prebootstrapped in (False, True):
            with self.subTest(prebootstrapped=prebootstrapped), tempfile.TemporaryDirectory() as td:
                root = Path(td)
                fx = self._make_6_slice_fixture(root)
                esp_src = build_mock_esp_tree(root / 'esp-src', '286.R2')
                ota_out = root / 'ota-out'
                report = bop.build_ota_package(
                    output_dir=ota_out,
                    esp_tree=esp_src,
                    mode='super-delta',
                    version='v0.9.4-286.R2',
                    base_slices_dir=fx['base_dir'],
                    target_slices_dir=fx['target_dir'],
                    chain_path=root / 'no-chain.json',
                )
                self.assertEqual(report['package_type'], 'super-delta')
                self.assertEqual(report['super_mode'], 'delta')
                self.assertTrue(report['super_included'])
                self.assertEqual(report['super_sha256'], fx['target_super_sha'])

                # Set up simulated device ESP, userdata (with ART dalvik-cache & user app), and super.
                dev_esp = build_mock_esp_tree(root / 'dev-esp', '16471258')
                dev_data = root / 'dev-data'
                (dev_data / 'ota').mkdir(parents=True)
                shutil.copy2(report['archive'], dev_data / 'ota/pending.pkg')
                (dev_data / 'app/user.apk').parent.mkdir(parents=True)
                (dev_data / 'app/user.apk').write_bytes(b'USER_APP_KEEP')
                (dev_data / 'system/package_cache/stale').parent.mkdir(parents=True)
                (dev_data / 'system/package_cache/stale').write_text('old')
                (dev_data / 'dalvik-cache/arm64').mkdir(parents=True)
                (dev_data / 'dalvik-cache/arm64/system@framework@services.jar@classes.odex').write_text('old-sys')
                (dev_data / 'dalvik-cache/arm64/vendor@app@Test@Test.apk@classes.odex').write_text('old-vnd')

                # Write SPX-OTA-PENDING
                (dev_esp / 'SPX-OTA-PENDING').write_text(
                    'PACKAGE_REL=ota/pending.pkg\n'
                    f"PACKAGE_SHA256={report['archive_sha256']}\n"
                    'PACKAGE_KIND=manifest\n'
                    'APPLY_SUPER=1\n'
                    'WIPE_DATA=0\n'
                    'VERSION=v0.9.4-286.R2\n',
                    encoding='utf-8',
                )
                slices_file = root / 'sq2-ssd-super-slices'
                # In prebootstrapped mode (ADB upgrade), initramfs already carries NEW target slice offsets.
                slices_file.write_bytes(fx['target_slices_txt'] if prebootstrapped else fx['base_slices_txt'])

                env = dict(os.environ)
                env.update(
                    SPX_OTA_ESP=str(dev_esp),
                    SPX_OTA_RUN=str(root / 'run'),
                    SPX_OTA_TOOL=str(self.host_tool),
                    SPX_OTA_SUPER_DEV=str(fx['dev_super']),
                    SPX_OTA_SUPER_SLICES=str(slices_file),
                    SPX_OTA_DATA_DIR=str(dev_data),
                    SPX_OTA_NO_REBOOT='1',
                )
                proc = subprocess.run(
                    ['sh', str(ROOT / 'probe/ota/spx-ota-stage')],
                    env=env, capture_output=True, text=True,
                )
                self.assertEqual(proc.returncode, 0, f'stdout={proc.stdout}\nstderr={proc.stderr}')
                result_txt = (dev_esp / 'SPX-OTA-RESULT.txt').read_text()
                self.assertIn('STATUS=OK', result_txt)
                self.assertIn('VERSION=v0.9.4-286.R2', result_txt)
                # Verify assembled super matches target SHA-256 bit-for-bit!
                got_super_sha = sha256_bytes(fx['dev_super'].read_bytes()[:fx['target_super_bytes']])
                self.assertEqual(got_super_sha, fx['target_super_sha'])
                # Verify ESP updated, user app preserved, full dalvik-cache cleared, staging cleaned.
                self.assertIn('286.R2', (dev_esp / 'SPX-IMAGE.txt').read_text())
                self.assertEqual((dev_data / 'app/user.apk').read_bytes(), b'USER_APP_KEEP')
                self.assertFalse((dev_data / 'dalvik-cache/arm64/system@framework@services.jar@classes.odex').exists())
                self.assertFalse((dev_data / 'dalvik-cache/arm64/vendor@app@Test@Test.apk@classes.odex').exists())
                self.assertFalse((dev_data / 'system/package_cache/stale').exists())
                self.assertFalse((dev_data / 'ota/super-stage').exists())
                self.assertFalse((dev_data / 'ota/pending.pkg').exists())

    def test_spx_ota_stage_fails_closed_on_source_mismatch_and_small_super(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            fx = self._make_6_slice_fixture(root)
            esp_src = build_mock_esp_tree(root / 'esp-src', '286.R2')
            report = bop.build_ota_package(
                output_dir=root / 'ota-out',
                esp_tree=esp_src,
                mode='super-delta',
                version='v0.9.4-286.R2',
                base_slices_dir=fx['base_dir'],
                target_slices_dir=fx['target_dir'],
                chain_path=root / 'no-chain.json',
            )

            # Corrupt a byte inside device super system_a -> must fail closed with zero writes.
            corrupt_super = root / 'corrupt_super.img'
            sb = bytearray(fx['dev_super'].read_bytes())
            sb[M + 4096] ^= 0xff
            corrupt_super.write_bytes(bytes(sb))
            before_super_sha = sha256_file(corrupt_super)

            dev_esp = build_mock_esp_tree(root / 'dev-esp', '16471258')
            dev_data = root / 'dev-data'
            (dev_data / 'ota').mkdir(parents=True)
            shutil.copy2(report['archive'], dev_data / 'ota/pending.pkg')
            (dev_esp / 'SPX-OTA-PENDING').write_text(
                'PACKAGE_REL=ota/pending.pkg\n'
                f"PACKAGE_SHA256={report['archive_sha256']}\n"
                'PACKAGE_KIND=manifest\nAPPLY_SUPER=1\nWIPE_DATA=0\nVERSION=v0.9.4-286.R2\n',
                encoding='utf-8',
            )
            slices_file = root / 'sq2-ssd-super-slices'
            slices_file.write_bytes(fx['base_slices_txt'])

            env = dict(os.environ,
                       SPX_OTA_ESP=str(dev_esp), SPX_OTA_RUN=str(root / 'run'),
                       SPX_OTA_TOOL=str(self.host_tool), SPX_OTA_SUPER_DEV=str(corrupt_super),
                       SPX_OTA_SUPER_SLICES=str(slices_file), SPX_OTA_DATA_DIR=str(dev_data),
                       SPX_OTA_NO_REBOOT='1')
            proc = subprocess.run(['sh', str(ROOT / 'probe/ota/spx-ota-stage')], env=env, capture_output=True, text=True)
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn('STATUS=ERROR', (dev_esp / 'SPX-OTA-RESULT.txt').read_text())
            self.assertIn('super_delta_source_mismatch:system_a', (dev_esp / 'SPX-OTA-RESULT.txt').read_text())
            self.assertEqual(sha256_file(corrupt_super), before_super_sha)
            self.assertIn('16471258', (dev_esp / 'SPX-IMAGE.txt').read_text())

            # Too-small super partition -> must fail with super_partition_too_small.
            small_super = root / 'small_super.img'
            small_super.write_bytes(b'\x00' * (fx['target_super_bytes'] - M))
            (dev_esp / 'SPX-OTA-PENDING').write_text(
                'PACKAGE_REL=ota/pending.pkg\n'
                f"PACKAGE_SHA256={report['archive_sha256']}\n"
                'PACKAGE_KIND=manifest\nAPPLY_SUPER=1\nWIPE_DATA=0\nVERSION=v0.9.4-286.R2\n',
                encoding='utf-8',
            )
            env['SPX_OTA_SUPER_DEV'] = str(small_super)
            proc2 = subprocess.run(['sh', str(ROOT / 'probe/ota/spx-ota-stage')], env=env, capture_output=True, text=True)
            self.assertNotEqual(proc2.returncode, 0)
            self.assertIn('ERROR=super_partition_too_small', (dev_esp / 'SPX-OTA-RESULT.txt').read_text())

    def test_spx_ota_boot_inspect_and_install_super_delta(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            fx = self._make_6_slice_fixture(root)
            esp_src = build_mock_esp_tree(root / 'esp-src', '286.R2')
            report = bop.build_ota_package(
                output_dir=root / 'ota-out',
                esp_tree=esp_src,
                mode='super-delta',
                version='v0.9.4-286.R2',
                base_slices_dir=fx['base_dir'],
                target_slices_dir=fx['target_dir'],
                chain_path=root / 'no-chain.json',
            )

            data_root = root / 'android-root'
            app_files = data_root / 'data/user_de/0/org.surface.advancedcontrol/files'
            app_files.mkdir(parents=True)
            (data_root / 'data/ota').mkdir(parents=True)
            dev_esp = build_mock_esp_tree(root / 'dev-esp', '16471258')
            etc_dir = root / 'etc'
            etc_dir.mkdir()
            (etc_dir / 'spx-ota-info').write_text('SPX_OTA_VERSION=v0.9.3\n', encoding='utf-8')
            (etc_dir / 'sq2-ssd-super-slices').write_bytes(fx['base_slices_txt'])
            (root / 'run').mkdir()
            wrapper = root / 'android-wrap.sh'
            wrapper.write_text('#!/bin/sh\nexit 0\n', encoding='utf-8')
            wrapper.chmod(0o755)

            def run_boot(req_body, super_dev):
                (app_files / 'ota-request').write_text(req_body, encoding='utf-8')
                env = dict(
                    os.environ,
                    SPX_OTA_BOOT_WAIT='0',
                    SPX_OTA_ONCE='1',
                    SPX_OTA_TEST_USER='0',
                    SPX_OTA_CLAIM_INTENT='0',
                    SPX_OTA_ROOT=str(data_root),
                    SPX_OTA_ESP=str(dev_esp),
                    SPX_OTA_RUN=str(root / 'run'),
                    SPX_OTA_ETC=str(etc_dir),
                    SPX_OTA_TOOL=str(self.host_tool),
                    SPX_OTA_SUPER_DEV=str(super_dev),
                    SPX_OTA_LOCK_DIR=str(root / 'lock'),
                    SPX_OTA_TMP_REQ=str(root / 'tmp-req'),
                    SPX_OTA_ANDROID_WRAPPER=str(wrapper),
                    SPX_OTA_NO_REBOOT='1',
                )
                p = subprocess.run(['sh', str(ROOT / 'probe/ota/spx-ota-boot')], env=env, text=True, capture_output=True)
                self.assertEqual(p.returncode, 0, f'stdout={p.stdout}\nstderr={p.stderr}')
                return (app_files / 'ota-reply').read_text(encoding='utf-8')

            # 1) Matching baseline super -> inspect returns super_compatible=1 & super_mode=delta, install stages OFFLINE_STAGED.
            shutil.copy2(report['archive'], app_files / 'ota-upload.pkg')
            rep = run_boot('ota-inspect\nn-delta-1\nota-upload.pkg\n', fx['dev_super'])
            self.assertTrue(rep.startswith('n-delta-1\nOK\n'), rep)
            self.assertIn('super_included=1', rep)
            self.assertIn('super_mode=delta', rep)
            self.assertIn('super_compatible=1', rep)

            rep2 = run_boot(f"ota-install\nn-delta-2\n{report['archive_sha256']}\n0\n", fx['dev_super'])
            self.assertTrue(rep2.startswith('n-delta-2\nOK\nOFFLINE_STAGED'), rep2)
            pending = (dev_esp / 'SPX-OTA-PENDING').read_text(encoding='utf-8')
            self.assertIn('APPLY_SUPER=1', pending)
            self.assertIn('WIPE_DATA=0', pending)
            self.assertIn('VERSION=v0.9.4-286.R2', pending)

            # 2) Mismatched baseline super -> inspect reports super_compatible=0, install fails closed with super_baseline_mismatch.
            bad_super = root / 'bad_super.img'
            sb = bytearray(fx['dev_super'].read_bytes())
            sb[M + 4096] ^= 0xff
            bad_super.write_bytes(bytes(sb))
            (dev_esp / 'SPX-OTA-PENDING').unlink()
            shutil.copy2(report['archive'], app_files / 'ota-upload.pkg')
            rep3 = run_boot('ota-inspect\nn-delta-3\nota-upload.pkg\n', bad_super)
            self.assertTrue(rep3.startswith('n-delta-3\nOK\n'), rep3)
            self.assertIn('super_compatible=0', rep3)
            rep4 = run_boot(f"ota-install\nn-delta-4\n{report['archive_sha256']}\n0\n", bad_super)
            self.assertTrue(rep4.startswith('n-delta-4\nERROR\nsuper_baseline_mismatch'), rep4)
            self.assertFalse((dev_esp / 'SPX-OTA-PENDING').exists())

    def test_adapt_vendor_build_prop_preserves_286_fixes_and_spx_overrides(self):
        sample_286 = (
            'ro.vendor.build.fingerprint=Dell/mica/mica:17/CL3B.260622.286.R2/16833189:user/release-keys\n'
            'ro.vendor.build.security_patch=2026-11-01\n'
            'ro.hwui.use_vulkan=true\n'
            'ro.gfx.angle.supported=true\n'
            'ro.hardware.egl=angle\n'
            'ro.hardware.vulkan=adreno\n'
            'ro.boot.hypervisor.protected_vm.supported=1\n'
            'vendor.gralloc.disable_ubwc=0\n'
            'ro.surface_flinger.force_hole_punch=true\n'
            'bluetooth.hfp.software_datapath.enabled=true\n'
            'ro.bluetooth.a2dp_offload.supported=false\n'
            'persist.bluetooth.a2dp_offload.disabled=true\n'
            'debug.stagefright.ccodec=4\n'
        ).encode('utf-8')
        adapted = ssd.adapt_vendor_build_prop(sample_286).decode('utf-8')
        lines = set(adapted.splitlines())
        for req in fi.HYBRID_REQUIRED_PROPS:
            self.assertIn(req, lines)
        self.assertIn('ro.vendor.build.security_patch=2026-11-01', lines)
        self.assertIn('ro.vendor.build.fingerprint=Dell/mica/mica:17/CL3B.260622.286.R2/16833189:user/release-keys', lines)
        self.assertIn('bluetooth.hfp.software_datapath.enabled=true', lines)
        self.assertIn('ro.bluetooth.a2dp_offload.supported=false', lines)
        self.assertIn('ro.boot.hypervisor.protected_vm.supported=0', lines)
        self.assertIn('debug.stagefright.ccodec=1', lines)
        self.assertNotIn('debug.stagefright.ccodec=4', lines)
        self.assertNotIn('ro.hardware.egl=angle', lines)
        self.assertNotIn('ro.hardware.vulkan=adreno', lines)

    def test_hygiene_disables_ota_recovery_updater_on_non_dell(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            rc_rel = Path('vendor/etc/init/ota_recovery_updater.rc')
            rc_path = root / rc_rel
            rc_path.parent.mkdir(parents=True)
            rc_path.write_text(
                'service vendor.google.desktop.ota_recovery_updater /vendor/bin/ota_recovery_updater --slot-suffix ${ro.boot.slot_suffix}\n'
                '    class main\n'
                '    user root\n'
                '    disabled\n'
                '    oneshot\n\n'
                'on property:sys.boot_completed=1\n'
                '    start vendor.google.desktop.ota_recovery_updater\n',
                encoding='utf-8',
            )
            sel_dir = root / 'vendor/etc/selinux'
            sel_dir.mkdir(parents=True)
            (sel_dir / 'vendor_file_contexts').write_text(
                '/vendor/bin/hw/ec_battery_limits u:object_r:hal_health_ec_battery_limits_exec:s0\n'
                '/vendor/etc/recovery(/.*)? u:object_r:vendor_ota_recovery_file:s0\n'
                '/vendor/bin/ota_recovery_updater u:object_r:ota_recovery_updater_exec:s0\n',
                encoding='utf-8',
            )
            (sel_dir / 'precompiled_sepolicy').write_bytes(b'SELinux\x00hal_health_ec_battery_limits_exec\x00')
            stage = root / 'stage'
            sysfs = root / 'sys'
            (sysfs / 'firmware/devicetree/base').mkdir(parents=True)
            (sysfs / 'firmware/devicetree/base/model').write_bytes(b'Microsoft Surface Pro X\x00')
            fake_bin = root / 'bin'
            fake_bin.mkdir()
            mount_stub = fake_bin / 'mount'
            mount_stub.write_text('#!/bin/sh\nexit 0\n', encoding='utf-8')
            mount_stub.chmod(0o755)

            env = dict(os.environ)
            env.update(
                PATH=f'{fake_bin}:{os.environ["PATH"]}',
                SPX_HYGIENE_ROOT=str(root),
                SPX_HYGIENE_STAGE=str(stage),
                SPX_HYGIENE_DEV=str(root / 'dev'),
                SPX_HYGIENE_SYSFS=str(sysfs),
                SPX_HYGIENE_KREL='6.18.60-sq2-android1',
            )
            proc = subprocess.run(['sh', str(ROOT / 'probe/test2/spx-service-hygiene-stage')],
                                  env=env, capture_output=True, text=True, check=True)
            self.assertIn('SPX_HYGIENE_DISABLE_RC=vendor/etc/init/ota_recovery_updater.rc', proc.stdout)
            self.assertIn('SPX_HYGIENE_SANITIZE_FILE_CONTEXTS=vendor/etc/selinux/vendor_file_contexts', proc.stdout)
            staged_rc = (stage / 'vendor_etc_init_ota_recovery_updater_rc.rc').read_text()
            self.assertIn('# spx-hygiene (non_dell_hardware):     start vendor.google.desktop.ota_recovery_updater', staged_rc)
            staged_vfc = (stage / 'vendor_file_contexts').read_text()
            self.assertIn('/vendor/bin/hw/ec_battery_limits u:object_r:hal_health_ec_battery_limits_exec:s0', staged_vfc)
            self.assertIn('# spx-hygiene (missing_sepolicy_type): /vendor/etc/recovery(/.*)?', staged_vfc)
            self.assertIn('# spx-hygiene (missing_sepolicy_type): /vendor/bin/ota_recovery_updater', staged_vfc)

    def test_mica_ota_chain_schema_and_tracker_analysis(self):
        chain = tmo.load_chain()
        self.assertEqual(chain['board'], 'mica')
        self.assertEqual(chain['base']['build'], '16471258')
        self.assertEqual(chain['hops'][-1]['to_build'], '16833189')
        self.assertEqual(chain['hops'][-1]['to_build_id'], 'CL3B.260622.286.R2')
        self.assertEqual(chain['hops'][0]['from_build'], chain['base']['build'])
        self.assertEqual(chain['hops'][1]['from_build'], chain['hops'][0]['to_build'])
        for part in chain['dynamic_partitions']:
            self.assertEqual(len(chain['base']['partitions'][part]['sha256']), 64)
            for hop in chain['hops']:
                self.assertEqual(len(hop['partitions'][part]['sha256']), 64)

        mock_live = [
            {'id': 2851, 'fingerprint': 'Dell/mica/mica:17/CL3B.260622.273.R1/16471258:user/release-keys', 'size': 37946755, 'incremental': True},
            {'id': 2850, 'fingerprint': 'Dell/mica/mica:17/CL3B.260622.276.R2/16560795:user/release-keys', 'size': 37231030, 'incremental': True},
            {'id': 2927, 'fingerprint': 'Dell/mica/mica:17/CL3B.260622.276.R5/16693588:user/release-keys', 'size': 180866017, 'incremental': True},
            {'id': 3100, 'fingerprint': 'Dell/mica/mica:17/CL3B.260622.286.R2/16833189:user/release-keys', 'size': 99000000, 'incremental': True},
        ]
        status = tmo.analyze_chain_status(chain, live_entries=mock_live)
        self.assertEqual([r['id'] for r in status['new_untracked_otas']], [3100])


if __name__ == '__main__':
    unittest.main()
