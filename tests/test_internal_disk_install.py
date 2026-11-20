import hashlib
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))

import full_image_advanced_control
import full_image_ota
import runtime_boot_policy

OTA_TOOL_SRC = ROOT / 'probe/ota/spx-ota-tool.c'
INSTALLER_SCRIPT = ROOT / 'probe/ota/spx-internal-install'
OTA_BOOT_SCRIPT = ROOT / 'probe/ota/spx-ota-boot'


def parse_kv(text):
    out = {}
    for line in text.splitlines():
        if '=' in line:
            k, v = line.split('=', 1)
            out[k.strip()] = v.strip()
    return out


class InternalDiskInstallTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='spx-disk-install-test-')
        cls.tmp_path = Path(cls.tmp.name)
        # Compile native host binary for fast execution, plus ARM64 static ELF for cross-compile check
        cls.host_tool = cls.tmp_path / 'spx-ota-tool-host'
        subprocess.run(
            ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
             str(OTA_TOOL_SRC), '-o', str(cls.host_tool)],
            check=True,
        )
        cls.arm64_bytes = full_image_ota.build_ota_tool_bytes()
        cls.arm64_tool = cls.tmp_path / 'spx-ota-tool-arm64'
        cls.arm64_tool.write_bytes(cls.arm64_bytes)
        cls.arm64_tool.chmod(0o755)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def _make_fake_env(self, root, *, boot_is_usb=True, legacy_39part=False,
                       internal_size_gib=16, include_internal=True):
        sysfs = root / 'sys'
        etc = root / 'etc'
        esp = root / 'esp'
        run = root / 'run'
        dev = root / 'dev'
        for d in (sysfs / 'class/block', etc, esp, run, dev):
            d.mkdir(parents=True, exist_ok=True)

        # Create realistic USB boot disk sysfs hierarchy
        usb_phys = sysfs / 'devices/pci0/usb1/1-1/host0/block/sda'
        usb_phys.mkdir(parents=True)
        (sysfs / 'devices/pci0/usb1/1-1/idVendor').write_text('0781\n')
        (usb_phys / 'removable').write_text('1\n')
        (usb_phys / 'ro').write_text('0\n')
        (usb_phys / 'size').write_text(str(32 * 1024 * 1024 * 2) + '\n')
        (usb_phys / 'queue').mkdir()
        (usb_phys / 'queue/logical_block_size').write_text('512\n')
        (usb_phys / 'device').mkdir()
        (usb_phys / 'device/model').write_text('SanDisk Extreme\n')
        (sysfs / 'class/block/sda').symlink_to(usb_phys)

        # Second external USB disk that MUST NEVER be offered as an internal target
        usb2_phys = sysfs / 'devices/pci0/usb2/2-1/host1/block/sdb'
        usb2_phys.mkdir(parents=True)
        (sysfs / 'devices/pci0/usb2/2-1/idVendor').write_text('0bda\n')
        (usb2_phys / 'removable').write_text('0\n')
        (usb2_phys / 'ro').write_text('0\n')
        (usb2_phys / 'size').write_text(str(64 * 1024 * 1024 * 2) + '\n')
        (usb2_phys / 'queue').mkdir()
        (usb2_phys / 'queue/logical_block_size').write_text('512\n')
        (usb2_phys / 'device').mkdir()
        (usb2_phys / 'device/model').write_text('External USB SSD\n')
        (sysfs / 'class/block/sdb').symlink_to(usb2_phys)

        # Virtual loop0 that MUST be ignored
        loop_phys = sysfs / 'devices/virtual/block/loop0'
        loop_phys.mkdir(parents=True)
        (loop_phys / 'removable').write_text('0\n')
        (loop_phys / 'ro').write_text('0\n')
        (loop_phys / 'size').write_text('1048576\n')
        (sysfs / 'class/block/loop0').symlink_to(loop_phys)

        if include_internal:
            nvme_phys = sysfs / 'devices/pci0/0000:01:00.0/nvme/nvme0/nvme0n1'
            nvme_phys.mkdir(parents=True)
            (nvme_phys / 'removable').write_text('0\n')
            (nvme_phys / 'ro').write_text('0\n')
            (nvme_phys / 'size').write_text(str(internal_size_gib * 1024 * 1024 * 2) + '\n')
            (nvme_phys / 'queue').mkdir()
            (nvme_phys / 'queue/logical_block_size').write_text('512\n')
            (nvme_phys / 'device').mkdir()
            (nvme_phys / 'device/model').write_text('KBG40ZNS256G KIOXIA\n')
            (sysfs / 'class/block/nvme0n1').symlink_to(nvme_phys)
            # Also add a partition entry to ensure partitions are skipped
            nvme_p1 = nvme_phys / 'nvme0n1p1'
            nvme_p1.mkdir()
            (nvme_p1 / 'partition').write_text('1\n')
            (nvme_p1 / 'size').write_text('204800\n')
            (sysfs / 'class/block/nvme0n1p1').symlink_to(nvme_p1)

        boot_disk_file = run / 'sq2-ssd-device'
        boot_disk_file.write_text(f'{dev}/sda\n' if boot_is_usb else f'{dev}/nvme0n1\n')

        # Source super image (4 MiB for fast test)
        super_file = root / 'source-super.img'
        super_bytes = 4 * 1024 * 1024
        super_data = os.urandom(super_bytes)
        super_file.write_bytes(super_data)

        if legacy_39part:
            (etc / 'sq2-ssd-partitions').write_text(
                f'super 3 {super_bytes // 512}\n'
                'metadata 11 131072\n'
                'misc 12 8192\n'
                'SQ2PROBE 6 2097152\n'
                'userdata 38 7409624\n'
                'ota_recovery_b 39 131072\n'
            )
        else:
            (etc / 'sq2-ssd-partitions').write_text(
                f'super 1 {super_bytes // 512}\n'
                'metadata 2 131072\n'
                'misc 3 8192\n'
                'SQ2PROBE 4 2097152\n'
                'userdata 5 7409624\n'
            )
        (etc / 'sq2-ssd-super-slices').write_text(
            f'system_a 1048576 {super_bytes - 1048576}\n'
        )

        # Source ESP with valid boot payload + stale runtime files that must NOT be copied
        (esp / 'EFI/BOOT').mkdir(parents=True)
        (esp / 'EFI/SQ2Nat').mkdir(parents=True)
        (esp / 'EFI/SQ2Res').mkdir(parents=True)
        (esp / 'firmware').mkdir(parents=True)
        (esp / 'SPX-IMAGE.txt').write_text('SPX_IMAGE_FORMAT=1\nSPX_RELEASE=6.18-hybrid-single-user\n')
        (esp / 'EFI/BOOT/BOOTAA64.EFI').write_bytes(b'MZ-BOOTAA64-EFI-PAYLOAD')
        (esp / 'EFI/BOOT/grub.cfg').write_text('# stale dual-boot config with bootmgfw.efi\n')
        (esp / 'EFI/BOOT/grubenv').write_text('# stale grubenv\n')
        (esp / 'EFI/SQ2Nat/start.efi').write_bytes(b'MZ-START-EFI')
        (esp / 'EFI/SQ2Nat/Image').write_bytes(b'LINUX-KERNEL-IMAGE')
        (esp / 'EFI/SQ2Nat/initramfs.cpio.gz').write_bytes(b'\x1f\x8bINITRAMFS')
        (esp / 'EFI/SQ2Res/start.efi').write_bytes(b'MZ-RESCUE-START-EFI')
        (esp / 'firmware/board.bin').write_bytes(b'BOARD-FW-DATA')
        (esp / 'SPX-BOOT-SOURCE.txt').write_text('a' * 64 + '\n')
        (esp / 'SPX-USERDATA-GROWN.txt').write_text('123:5:100:200\n')
        (esp / 'SPX-OTA-RESULT.txt').write_text('STATUS=OK\n')

        empty_mounts = root / 'mounts'
        empty_mounts.write_text('')
        empty_swaps = root / 'swaps'
        empty_swaps.write_text('Filename\tType\tSize\tUsed\tPriority\n')

        env = os.environ.copy()
        env.update({
            'SPX_INSTALL_SYSFS': str(sysfs),
            'SPX_INSTALL_ETC': str(etc),
            'SPX_INSTALL_ESP': str(esp),
            'SPX_INSTALL_RUN': str(run / 'work'),
            'SPX_INSTALL_BOOT_DISK_FILE': str(boot_disk_file),
            'SPX_INSTALL_SUPER_DEV': str(super_file),
            'SPX_INSTALL_OTA_TOOL': str(self.host_tool),
            'SPX_INSTALL_MOUNTS': str(empty_mounts),
            'SPX_INSTALL_MOUNTINFO': str(empty_mounts),
            'SPX_INSTALL_SWAPS': str(empty_swaps),
            'SPX_INSTALL_DEV_PREFIX': str(dev),
            'SPX_INSTALL_LOG': str(root / 'install.log'),
        })
        return env, super_data

    def test_ota_tool_write_gpt_5part_and_copy_verify_arm64(self):
        with tempfile.TemporaryDirectory(prefix='spx-gpt-arm64-') as td:
            d = Path(td)
            disk = d / 'disk.img'
            total_bytes = 4 * 1024 * 1024 * 1024  # 4 GiB sparse
            with disk.open('wb') as f:
                f.truncate(total_bytes)
                # Seed stale signature at metadata/misc/esp/userdata regions
                f.seek(1048576 + 8 * 1048576)
                f.write(b'STALE_NTFS_SIGNATURE' * 16)

            super_sz = 8 * 1024 * 1024
            meta_sz = 64 * 1024 * 1024
            misc_sz = 4 * 1024 * 1024
            esp_sz = 1024 * 1024 * 1024

            runner = ['qemu-aarch64', str(self.arm64_tool)] if shutil.which('qemu-aarch64') else [str(self.host_tool)]
            out1 = subprocess.check_output(
                [*runner, 'write-gpt-5part', str(disk),
                 str(super_sz), str(meta_sz), str(misc_sz), str(esp_sz)],
                text=True,
            )
            rows = [line.split('\t') for line in out1.strip().splitlines()]
            self.assertEqual([r[0] for r in rows], ['super', 'metadata', 'misc', 'SQ2PROBE', 'userdata'])
            self.assertEqual(int(rows[0][3]), super_sz)
            self.assertEqual(int(rows[3][3]), esp_sz)
            self.assertGreater(int(rows[4][3]), 2 * 1024 * 1024 * 1024)

            # Verify stale bytes at metadata start were wiped
            with disk.open('rb') as f:
                f.seek(int(rows[1][2]))
                self.assertEqual(f.read(64), b'\x00' * 64)
                f.seek(512 + 56)
                guid1 = f.read(16)

            # Verify inspect-gpt agrees
            insp = subprocess.check_output([*runner, 'inspect-gpt', str(disk)], text=True)
            insp_names = [line.split('\t')[0] for line in insp.strip().splitlines()]
            self.assertEqual(insp_names, ['super', 'metadata', 'misc', 'SQ2PROBE', 'userdata'])

            # Host sgdisk -v check if available
            if Path('/usr/sbin/sgdisk').is_file():
                subprocess.run(['/usr/sbin/sgdisk', '-v', str(disk)], check=True, capture_output=True)

            # Second write must generate a brand-new random disk GUID
            subprocess.check_output(
                [*runner, 'write-gpt-5part', str(disk),
                 str(super_sz), str(meta_sz), str(misc_sz), str(esp_sz)],
                text=True,
            )
            with disk.open('rb') as f:
                f.seek(512 + 56)
                guid2 = f.read(16)
            self.assertNotEqual(guid1, guid2)

            # Test copy-verify + progress file
            src = d / 'src.bin'
            dst = d / 'dst.bin'
            prog = d / 'progress'
            payload = os.urandom(6 * 1024 * 1024)
            src.write_bytes(payload)
            dst.write_bytes(b'\x00' * len(payload))
            cv = subprocess.check_output(
                [*runner, 'copy-verify', str(src), str(dst), str(len(payload)), str(prog)],
                text=True,
            )
            kv = parse_kv(cv)
            expected_sha = hashlib.sha256(payload).hexdigest()
            self.assertEqual(kv['SUPER_SHA256'], expected_sha)
            self.assertEqual(kv['SUPER_BYTES'], str(len(payload)))
            self.assertEqual(kv['SUPER_VERIFIED'], '1')
            self.assertEqual(dst.read_bytes(), payload)
            self.assertTrue(prog.is_file())
            self.assertEqual(parse_kv(prog.read_text())['stage'], 'verifying_super')

    def test_probe_usb_vs_internal_and_filters(self):
        with tempfile.TemporaryDirectory(prefix='spx-probe-') as td:
            root = Path(td)
            # Case 1: USB boot + internal NVMe -> eligible, only nvme0n1 listed (never sda or sdb)
            env, _ = self._make_fake_env(root / 'c1', boot_is_usb=True, internal_size_gib=16)
            out = subprocess.check_output(['sh', str(INSTALLER_SCRIPT), 'probe'], env=env, text=True)
            kv = parse_kv(out)
            self.assertEqual(kv['boot_is_usb'], '1')
            self.assertEqual(kv['boot_transport'], 'usb')
            self.assertEqual(kv['eligible'], '1')
            self.assertEqual(kv['target_count'], '1')
            self.assertEqual(kv['target_0_name'], 'nvme0n1')
            self.assertEqual(kv['target_0_bus'], 'nvme')
            self.assertEqual(kv['target_0_model'], 'KBG40ZNS256G KIOXIA')
            self.assertEqual(kv['target_0_enough_space'], '1')

            # Case 2: Already booted from internal NVMe -> rejected
            env2, _ = self._make_fake_env(root / 'c2', boot_is_usb=False, internal_size_gib=16)
            kv2 = parse_kv(subprocess.check_output(['sh', str(INSTALLER_SCRIPT), 'probe'], env=env2, text=True))
            self.assertEqual(kv2['boot_is_usb'], '0')
            self.assertEqual(kv2['eligible'], '0')
            self.assertEqual(kv2['ineligible_reason'], 'not_usb_boot')

            # Case 3: Legacy 39-partition layout -> rejected
            env3, _ = self._make_fake_env(root / 'c3', boot_is_usb=True, legacy_39part=True)
            kv3 = parse_kv(subprocess.check_output(['sh', str(INSTALLER_SCRIPT), 'probe'], env=env3, text=True))
            self.assertEqual(kv3['eligible'], '0')
            self.assertEqual(kv3['ineligible_reason'], 'unsupported_source_layout')

            # Case 4: Internal disk too small (1 GiB) -> rejected
            env4, _ = self._make_fake_env(root / 'c4', boot_is_usb=True, internal_size_gib=1)
            kv4 = parse_kv(subprocess.check_output(['sh', str(INSTALLER_SCRIPT), 'probe'], env=env4, text=True))
            self.assertEqual(kv4['eligible'], '0')
            self.assertEqual(kv4['ineligible_reason'], 'internal_disk_too_small')

            # Case 5: No internal disk -> rejected
            env5, _ = self._make_fake_env(root / 'c5', boot_is_usb=True, include_internal=False)
            kv5 = parse_kv(subprocess.check_output(['sh', str(INSTALLER_SCRIPT), 'probe'], env=env5, text=True))
            self.assertEqual(kv5['eligible'], '0')
            self.assertEqual(kv5['ineligible_reason'], 'no_internal_disk')

    def test_end_to_end_fresh_install_and_safety_guards(self):
        with tempfile.TemporaryDirectory(prefix='spx-e2e-install-') as td:
            root = Path(td)
            env, super_data = self._make_fake_env(root, boot_is_usb=True, internal_size_gib=4)

            # Build real sparse target disk + backing partition files
            target_disk = root / 'dev/nvme0n1'
            with target_disk.open('wb') as f:
                f.truncate(4 * 1024 * 1024 * 1024)
            p_super = root / 'p1-super.img'
            p_meta = root / 'p2-meta.img'
            p_misc = root / 'p3-misc.img'
            p_esp = root / 'p4-esp.img'
            p_data = root / 'p5-data.img'
            for p, sz in ((p_super, 4 * 1024 * 1024), (p_meta, 64 * 1024 * 1024),
                          (p_misc, 4 * 1024 * 1024), (p_esp, 64 * 1024 * 1024),
                          (p_data, 128 * 1024 * 1024)):
                with p.open('wb') as f:
                    f.truncate(sz)

            esp_out = root / 'target-esp-dir'
            prog_file = root / 'disk-progress'

            # Wrapper that delegates Android mke2fs / e2fsck to host e2fsprogs
            wrapper = root / 'android-wrapper.sh'
            wrapper.write_text(
                '#!/bin/sh\n'
                'cmd="$1"; shift\n'
                'case "$cmd" in\n'
                '  mke2fs) exec /usr/sbin/mke2fs "$@" ;;\n'
                '  e2fsck) exec /usr/sbin/e2fsck "$@" ;;\n'
                '  *) exit 0 ;;\n'
                'esac\n'
            )
            wrapper.chmod(0o755)

            env.update({
                'SPX_INSTALL_ALLOW_FILE_TARGET': '1',
                'SPX_INSTALL_SKIP_REREAD': '1',
                'SPX_INSTALL_PART_SUPER': str(p_super),
                'SPX_INSTALL_PART_META': str(p_meta),
                'SPX_INSTALL_PART_MISC': str(p_misc),
                'SPX_INSTALL_PART_ESP': str(p_esp),
                'SPX_INSTALL_PART_DATA': str(p_data),
                'SPX_INSTALL_ESP_DST_DIR': str(esp_out),
                'SPX_INSTALL_ANDROID_WRAPPER': str(wrapper),
            })

            out = subprocess.check_output(
                ['sh', str(INSTALLER_SCRIPT), 'install', str(target_disk), str(prog_file)],
                env=env, text=True,
            )
            kv = parse_kv(out)
            self.assertEqual(kv['STATUS'], 'OK')
            self.assertEqual(kv['TARGET_DEV'], str(target_disk))
            self.assertEqual(kv['SUPER_SHA256'], hashlib.sha256(super_data).hexdigest())
            self.assertEqual(p_super.read_bytes(), super_data)
            self.assertEqual(parse_kv(prog_file.read_text())['stage'], 'done')
            self.assertEqual(parse_kv(prog_file.read_text())['percent'], '100')

            # Verify clean ESP contents: boot files present, standard grub.cfg written, stale state excluded
            self.assertTrue((esp_out / 'SPX-IMAGE.txt').is_file())
            self.assertTrue((esp_out / 'EFI/BOOT/BOOTAA64.EFI').is_file())
            self.assertTrue((esp_out / 'EFI/SQ2Nat/start.efi').is_file())
            self.assertTrue((esp_out / 'EFI/SQ2Nat/Image').is_file())
            self.assertTrue((esp_out / 'EFI/SQ2Nat/initramfs.cpio.gz').is_file())
            self.assertTrue((esp_out / 'firmware/board.bin').is_file())
            grub_cfg = (esp_out / 'EFI/BOOT/grub.cfg').read_text()
            self.assertIn('/EFI/SQ2Nat/start.efi', grub_cfg)
            self.assertNotIn('bootmgfw.efi', grub_cfg)
            self.assertFalse((esp_out / 'EFI/BOOT/grubenv').exists())
            self.assertFalse((esp_out / 'SPX-BOOT-SOURCE.txt').exists())
            self.assertFalse((esp_out / 'SPX-USERDATA-GROWN.txt').exists())
            self.assertFalse((esp_out / 'SPX-OTA-RESULT.txt').exists())

            # Verify userdata ext4 features match disk_image.py and only contains lost+found
            if Path('/usr/sbin/debugfs').is_file():
                ls_out = subprocess.check_output(['/usr/sbin/debugfs', '-R', 'ls -l /', str(p_data)], text=True)
                entries = [line.split()[-1] for line in ls_out.splitlines() if line.strip()]
                self.assertTrue(all(e in ('.', '..', 'lost+found') for e in entries), entries)
            if Path('/usr/sbin/dumpe2fs').is_file():
                fs_info = subprocess.check_output(['/usr/sbin/dumpe2fs', '-h', str(p_data)], text=True, stderr=subprocess.DEVNULL)
                for feat in ('casefold', 'encrypt', 'verity', 'metadata_csum'):
                    self.assertIn(feat, fs_info)

            # Safety guard: booting from internal NVMe must fail closed with not_usb_boot
            env_nvme, _ = self._make_fake_env(root / 'nvme-guard', boot_is_usb=False, internal_size_gib=4)
            guard_target = root / 'nvme-guard/dev/nvme0n1'
            guard_target.write_bytes(b'\x00' * 1024)
            env_nvme['SPX_INSTALL_ALLOW_FILE_TARGET'] = '1'
            p = subprocess.run(
                ['sh', str(INSTALLER_SCRIPT), 'install', str(guard_target)],
                env=env_nvme, capture_output=True, text=True,
            )
            self.assertNotEqual(p.returncode, 0)
            self.assertIn('ERROR=not_usb_boot', p.stderr)

    def test_ota_boot_disk_ipc_commands(self):
        with tempfile.TemporaryDirectory(prefix='spx-disk-ipc-') as td:
            root = Path(td)
            app_files = root / 'root/data/user_de/0/org.surface.advancedcontrol/files'
            app_files.mkdir(parents=True)
            esp = root / 'esp'
            esp.mkdir()
            (esp / 'SPX-IMAGE.txt').write_text('SPX_IMAGE_FORMAT=1\n')
            etc = root / 'etc'
            etc.mkdir()
            (etc / 'spx-ota-info').write_text('SPX_OTA_VERSION=v0.8.0\n')
            run = root / 'run'
            run.mkdir()

            fake_installer = root / 'fake-installer.sh'
            fake_installer.write_text(
                '#!/bin/sh\n'
                'if [ "$1" = "probe" ]; then\n'
                '  printf "disk_install_version=1\\nboot_disk=/dev/sda\\nboot_transport=usb\\nboot_is_usb=1\\nlayout_compatible=1\\neligible=1\\ntarget_count=1\\ntarget_0_dev=/dev/nvme0n1\\ntarget_0_name=nvme0n1\\ntarget_0_model=TestNVMe\\ntarget_0_bytes=256000000000\\ntarget_0_bus=nvme\\ntarget_0_enough_space=1\\n"\n'
                '  exit 0\n'
                'fi\n'
                'if [ "$1" = "install" ]; then\n'
                '  printf "stage=done\\npercent=100\\ndetail=%s\\n" "$2" > "$3"\n'
                '  printf "STATUS=OK\\nTARGET_DEV=%s\\nSUPER_SHA256=%064d\\nSUPER_BYTES=3650093056\\nUSERDATA_BYTES=250000000000\\n" "$2" 0\n'
                '  exit 0\n'
                'fi\n'
                'exit 2\n'
            )
            fake_installer.chmod(0o755)

            env = os.environ.copy()
            env.update({
                'SPX_OTA_BOOT_WAIT': '0',
                'SPX_OTA_CLAIM_INTENT': '0',
                'SPX_OTA_TEST_USER': '0',
                'SPX_OTA_ONCE': '1',
                'SPX_OTA_NO_REBOOT': '1',
                'SPX_OTA_ROOT': str(root / 'root'),
                'SPX_OTA_ESP': str(esp),
                'SPX_OTA_ETC': str(etc),
                'SPX_OTA_RUN': str(run),
                'SPX_OTA_LOCK_DIR': str(run / 'lock'),
                'SPX_OTA_TMP_REQ': str(run / 'req'),
                'SPX_OTA_TOOL': str(self.host_tool),
                'SPX_OTA_INSTALLER': str(fake_installer),
            })

            # 1. disk-refresh
            (app_files / 'disk-request').write_text('disk-refresh\nnonce-1\n')
            subprocess.run(['sh', str(OTA_BOOT_SCRIPT)], env=env, check=True)
            self.assertTrue((app_files / 'disk-reply').read_text().startswith('nonce-1\nOK\n'))
            state = parse_kv((app_files / 'disk-state').read_text())
            self.assertEqual(state['eligible'], '1')
            self.assertEqual(state['target_0_dev'], '/dev/nvme0n1')

            # 2. disk-install rejects bad device path
            (app_files / 'disk-request').write_text('disk-install\nnonce-2\n/dev/../etc/passwd\n')
            subprocess.run(['sh', str(OTA_BOOT_SCRIPT)], env=env, check=True)
            self.assertIn('ERROR\ninvalid_target_device', (app_files / 'disk-reply').read_text())

            # 3. disk-install succeeds for valid /dev/nvme0n1
            (app_files / 'disk-request').write_text('disk-install\nnonce-3\n/dev/nvme0n1\n')
            subprocess.run(['sh', str(OTA_BOOT_SCRIPT)], env=env, check=True)
            reply3 = (app_files / 'disk-reply').read_text()
            self.assertTrue(reply3.startswith('nonce-3\nOK\n'))
            self.assertIn('TARGET_DEV=/dev/nvme0n1', reply3)

            # 4. disk-poweroff replies OK
            (app_files / 'disk-request').write_text('disk-poweroff\nnonce-4\n')
            subprocess.run(['sh', str(OTA_BOOT_SCRIPT)], env=env, check=True)
            self.assertTrue((app_files / 'disk-reply').read_text().startswith('nonce-4\nOK\n'))

    def test_full_image_ota_installs_internal_installer_and_passes_runtime_policy(self):
        records = {'init': (0o100755,)}
        bodies = {
            'init': b'#!/bin/busybox sh\nbase=/media/data/googlebook-stage\nexec native-handoff /newroot\n',
        }
        full_image_ota.install(
            records, bodies, lambda m: (m,),
            compiled_ota_tool=self.arm64_bytes,
            version='v0.8.0-test',
            source_commit='abcdef1',
        )
        self.assertIn('bin/spx-internal-install', bodies)
        self.assertEqual(records['bin/spx-internal-install'], (0o100755,))
        # Must pass runtime_boot_policy without raising
        runtime_boot_policy.apply(bodies)

    def test_java_policy_strings_and_apk_manifest(self):
        with tempfile.TemporaryDirectory(prefix='spx-disk-java-') as td:
            d = Path(td)
            classes = d / 'classes'
            classes.mkdir()
            src_dir = ROOT / 'probe/advanced-control'
            harness = d / 'DiskInstallTestMain.java'
            harness.write_text(
                'import java.util.Locale;\n'
                'import org.surface.advancedcontrol.DiskInstallPolicy;\n'
                'import org.surface.advancedcontrol.DiskInstallStrings;\n'
                'public class DiskInstallTestMain {\n'
                '  static void check(boolean cond, String msg) {\n'
                '    if (!cond) throw new AssertionError(msg);\n'
                '  }\n'
                '  public static void main(String[] args) {\n'
                '    check(DiskInstallPolicy.installRequest("abc-1", "/dev/nvme0n1").equals("disk-install\\nabc-1\\n/dev/nvme0n1\\n"), "install req");\n'
                '    check(DiskInstallPolicy.poweroffRequest("abc-2").equals("disk-poweroff\\nabc-2\\n"), "poweroff req");\n'
                '    boolean badThrew = false;\n'
                '    try { DiskInstallPolicy.validateTargetDevice("/dev/../sda"); } catch (IllegalArgumentException e) { badThrew = true; }\n'
                '    check(badThrew, "reject traversal");\n'
                '    String raw = "bridge_ready=1\\nboot_disk=/dev/sda\\nboot_transport=usb\\nboot_is_usb=1\\nlayout_compatible=1\\neligible=1\\nrequired_super_bytes=3650093056\\nrequired_esp_bytes=1073741824\\ntarget_count=1\\ntarget_0_dev=/dev/nvme0n1\\ntarget_0_name=nvme0n1\\ntarget_0_model=KIOXIA\\ntarget_0_bytes=256060514304\\ntarget_0_bus=nvme\\ntarget_0_enough_space=1\\n";\n'
                '    DiskInstallPolicy.DiskState s = DiskInstallPolicy.parseState(raw);\n'
                '    check(s.bridgeReady && s.bootIsUsb && s.eligible && s.targets.size() == 1, "parse state");\n'
                '    check(s.targets.get(0).device.equals("/dev/nvme0n1"), "target dev");\n'
                '    DiskInstallStrings zh = new DiskInstallStrings(Locale.SIMPLIFIED_CHINESE);\n'
                '    DiskInstallStrings en = new DiskInstallStrings(Locale.US);\n'
                '    check("写入内置硬盘".equals(zh.title()), "zh title");\n'
                '    check("Install to Internal Disk".equals(en.title()), "en title");\n'
                '    check(zh.powerOffNowButton().contains("立即关机"), "zh poweroff");\n'
                '    System.out.println("DISK_JAVA_OK");\n'
                '  }\n'
                '}\n'
            )
            subprocess.run(
                ['javac', '--release', '8', '-encoding', 'UTF-8', '-d', str(classes),
                 str(src_dir / 'DiskInstallPolicy.java'),
                 str(src_dir / 'DiskInstallStrings.java'),
                 str(harness)],
                check=True,
            )
            out = subprocess.check_output(['java', '-cp', str(classes), 'DiskInstallTestMain'], text=True)
            self.assertIn('DISK_JAVA_OK', out)

            # Build full APK and confirm InternalDiskInstallActivity is packaged
            apk_path = d / 'SurfaceAdvancedControl.apk'
            apk_bytes = full_image_advanced_control.build_apk(apk_path)
            self.assertGreater(len(apk_bytes), 10000)
            with zipfile.ZipFile(apk_path) as zf:
                manifest = zf.read('AndroidManifest.xml')
                self.assertIn(b'InternalDiskInstallActivity', manifest.replace(b'\x00', b''))


if __name__ == '__main__':
    unittest.main()
