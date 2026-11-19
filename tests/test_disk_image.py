import gzip
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import disk_image as d


class DiskImage(unittest.TestCase):
    def test_layout_and_both_gpt_crcs_no_overlap(self):
        original={'partitions':[
            dict(name='super',index=3,offset=d.M,bytes=4*d.M),
            dict(name='userdata',index=1,offset=16*d.M,bytes=16*d.M),
            dict(name='metadata',index=24,offset=6*d.M,bytes=64*1024),
            dict(name='misc',index=23,offset=7*d.M,bytes=64*1024),
            dict(name='ota_recovery_b',index=39,offset=8*d.M,bytes=128*1024)]}
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);source=root/'source';image=root/'image'
            h=bytearray(512);struct.pack_into('<QII',h,72,2,128,128)
            table=bytearray(128*128)
            for p in original['partitions']:table[(p['index']-1)*128:(p['index']-1)*128+16]=bytes.fromhex('af3dc60f838472478e793d69d8477de4')
            source.write_bytes(bytes(512)+h+table)
            with image.open('wb') as f:f.truncate(128*d.M)
            parts=d.plan_layout(original,128*d.M,8*d.M)
            d.write_gpt(source,image,parts)
            observed=d.module('test_gpt','inspect-recovery.py').inspect(image)
            self.assertTrue(observed['gpt_verified'])
            self.assertEqual([(p['name'],p['offset'],p['bytes']) for p in observed['partitions']],[(p['name'],p['offset'],p['bytes']) for p in parts])
            with self.assertRaisesRegex(ValueError,'too small'):
                d.plan_layout(original,20*d.M,8*d.M)
            occupied={'partitions':original['partitions']+[dict(name='occupied',index=6,offset=9*d.M,bytes=d.M)]}
            with self.assertRaisesRegex(ValueError,'slot 6'):
                d.plan_layout(occupied,128*d.M,8*d.M)

    def test_storage_delta_preserves_private_parent_and_user_policy(self):
        c=d.cpio_module()
        def arc(e):
            return gzip.compress(b''.join(c.entry(n,b,m) for n,(m,b) in e.items())+c.entry('TRAILER!!!',b'',0),mtime=0)
        init=(b'#!/bin/sh\nsq2-loop-slice "${ssd_part}3" "$offset" "$bytes"\n'
              b'echo " androidboot.serialno=SQ2TEST"\n'+d.HOOK+d.ANCHOR)
        before={'init':(0o100755,init),'bin/apply-single-user':(0o100755,b'user0'),
                'bin/prepare-native-ssd':(0o100755,b'old'),
                'etc/sq2-ssd-layout':(0o100644,b'old'),
                'etc/sq2-ssd-partitions':(0o100644,b'old'),
                'lib/firmware/keep':(0o100644,b'firmware')}
        identity=arc({'etc/shadow':(0o100600,b'fixture'),'bin/dropbear':(0o100755,b'fixture')})
        base=arc(before)+identity
        payload,audit,delta=d.storage_payload(base,[dict(name='super',index=8,bytes=2048*512)],'a'*64)
        after=c.entries(gzip.decompress(payload))
        self.assertTrue(payload.endswith(identity))
        self.assertTrue(payload.startswith(base[:-len(identity)]))
        self.assertEqual(after['bin/apply-single-user'],before['bin/apply-single-user'])
        self.assertEqual(after['lib/firmware/keep'],before['lib/firmware/keep'])
        self.assertNotIn(b'SQ2TEST',after['init'][1])
        self.assertIn(b'/dev/block/by-name/super',after['init'][1])
        self.assertLess(after['init'][1].index(d.HOOK),after['init'][1].index(d.ANCHOR))
        self.assertEqual(set(c.entries(gzip.decompress(delta))),set(audit['changed']))

    def test_rescue_efi_patches_only_unsigned_path_and_options(self):
        fixture=d.module('efi_fixture','../tests/test_sq2_618_debug_entry.py')
        tool=d.module('efi_checker','prepare-sq2-618-debug-entry.py')
        options='console=tty0 suspend.screen_only=0 suspend.screen_only_autosuspend=0 nohibernate panic=0 sq2.el2=1 sq2.vulkan=qcom sq2.video.c2=experimental initrd=\\EFI\\SQ2Nat\\initramfs.cpio.gz'
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);src=root/'EFI/SQ2Nat';src.mkdir(parents=True)
            files={'start.efi':fixture.pe(['\\EFI\\SQ2Nat\\el2launch.efi','\\EFI\\SQ1Default\\el2launch.efi']),
                   'el2launch.efi':fixture.pe(['\\EFI\\SQ2Nat\\'+n for n in ['Image','android-el2.dtb','qebspilaa64.efi','tcblaunch.exe']]+[options]),
                   'kernel-options.txt':(options+'\n').encode(),'Image':b'kernel','android-el2.dtb':b'dtb',
                   'qebspilaa64.efi':b'signed-fw','tcblaunch.exe':b'signed-tcb','initramfs.cpio.gz':b'payload'}
            for name,body in files.items():(src/name).write_bytes(body)
            d.rescue_efi(root,None)
            dst=root/'EFI/SQ2Res'
            for name in ['Image','android-el2.dtb','qebspilaa64.efi','tcblaunch.exe','initramfs.cpio.gz']:
                self.assertEqual((dst/name).read_bytes(),files[name])
            self.assertIn('sq2.rescue=1',(dst/'kernel-options.txt').read_text())
            tool.inspect_efi((dst/'start.efi').read_bytes(),(dst/'el2launch.efi').read_bytes(),(dst/'kernel-options.txt').read_text(),'SQ2Res','sq2.vulkan=qcom',router_target_count=2)
            self.assertFalse((dst/'SHA256SUMS').exists())

    def test_runtime_discovery_accepts_other_indices_identities_expansion_and_rejects_ambiguity(self):
        source=(ROOT/'probe/test2/prepare-image-ssd').read_text()
        for mode in ['normal','expanded','stale_clone','ambiguous','missing_token','bad_super','small_super']:
            with self.subTest(mode=mode),tempfile.TemporaryDirectory() as td:
                root=Path(td)
                (root/'etc').mkdir();(root/'dev').mkdir();(root/'sys/class/block').mkdir(parents=True)
                (root/'esp-tree/EFI/SQ2Nat').mkdir(parents=True)
                (root/'esp-tree/EFI/SQ2Nat/start.efi').touch()
                (root/'esp-tree/SPX-IMAGE.txt').write_text('SPX_IMAGE_FORMAT=1\n')
                (root/'esp-tree/SPX-BOOT-SOURCE.txt').write_text('a'*64+'\n')
                (root/'cmdline').write_text(('console=tty0' if mode=='missing_token' else 'spx.boot_token='+'a'*64)+'\n')
                (root/'clone-esp').mkdir()
                (root/'clone-esp/SPX-IMAGE.txt').write_text('SPX_IMAGE_FORMAT=1\n')
                (root/'clone-esp/SPX-BOOT-SOURCE.txt').write_text('b'*64+'\n')
                (root/'clone-esp/EFI/SQ2Nat').mkdir(parents=True)
                (root/'clone-esp/EFI/SQ2Nat/start.efi').touch()
                expected=hashlib.sha256(bytes(d.M)).hexdigest()
                (root/'etc/sq2-ssd-layout').write_text('SSD_SUPER_HEADER_SHA256='+('0'*64 if mode=='bad_super' else expected)+'\n')
                (root/'etc/sq2-ssd-partitions').write_text('userdata 1 32\nsuper 3 2048\nSQ2PROBE 6 1024\n')
                def partition(parent,name,number,sectors):
                    k=parent+'p'+str(number);p=root/'devices'/parent/k;p.mkdir(parents=True)
                    (p/'partition').write_text(str(number)+'\n');(p/'size').write_text(str(sectors)+'\n');(p/'uevent').write_text('PARTNAME='+name+'\n')
                    (root/'sys/class/block'/k).symlink_to(p)
                    (root/'dev'/k).write_bytes(bytes(d.M) if name=='super' else b'fixture')
                partition('nvme7n2','userdata',11,64 if mode=='expanded' else 32)
                partition('nvme7n2','super',8,1024 if mode=='small_super' else 2048)
                partition('nvme7n2','SQ2PROBE',5,1024)
                if mode in ('ambiguous','stale_clone'):partition('nvme8n3','SQ2PROBE',17,1024)
                code=source.replace('/proc/cmdline',str(root/'cmdline'))
                for old in ['/etc/sq2-ssd','/sys/class/block','/dev/','/run/','/media/']:
                    code=code.replace(old,str(root)+old)
                stub='''fail() { echo "FAIL:$1" >&2; exit 1; }
blkid() { echo 'TYPE="vfat" UUID="unrelated-random-UUID"'; }
mount() {
    case "$5:$TEST_MODE" in
        *nvme8n3*:stale_clone) src=clone-esp ;;
        *) src=esp-tree ;;
    esac
    for last; do :; done
    mkdir -p "$last"; cp -a "$FIXTURE/$src/." "$last/"
}
umount() { return 0; }
sleep() { return 0; }
'''
                p=subprocess.run(['sh','-c',stub+code],env=dict(os.environ,FIXTURE=td,TEST_MODE=mode),text=True,capture_output=True)
                self.assertEqual(p.returncode==0,mode in ['normal','expanded','stale_clone','bad_super'],p.stderr)
                if p.returncode==0:
                    self.assertEqual((root/'run/sq2-ssd-device').read_text().strip(),str(root/'dev/nvme7n2'))
                else:self.assertIn('FAIL:',p.stderr)

    def test_minimal_layout_compacts_super_and_writes_5_partitions(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source = root / 'source.img'
            image = root / 'minimal.img'
            src_super_off = 2 * d.M
            src_super_len = 16 * d.M
            with source.open('wb') as f:
                f.truncate(20 * d.M)
                # Place system_a at 1 MiB and vendor_a after a 6 MiB system_b gap.
                f.seek(src_super_off + 1 * d.M + 1024)
                f.write(b'\xe2\xe1\xf5\xe0')
                f.seek(src_super_off + 9 * d.M + 1024)
                f.write(b'\xe2\xe1\xf5\xe0')
            slices = f'system_a {1*d.M} {2*d.M}\nvendor_a {9*d.M} {1*d.M}\n'.encode()
            compacted_super, slice_plan, new_slices = d.compact_super_slices(
                slices, source, src_super_off, src_super_len)
            self.assertEqual(compacted_super, 4 * d.M)
            self.assertEqual(slice_plan, [
                ('system_a', 1*d.M, 1*d.M, 2*d.M),
                ('vendor_a', 9*d.M, 3*d.M, 1*d.M),
            ])
            self.assertEqual(new_slices.decode(), f'system_a {1*d.M} {2*d.M}\nvendor_a {3*d.M} {1*d.M}\n')

            parts = d.plan_minimal_layout(compacted_super, image_bytes=3*d.G, esp_bytes=768*d.M)
            self.assertEqual([p['name'] for p in parts], ['super', 'metadata', 'misc', 'SQ2PROBE', 'userdata'])
            with image.open('wb') as f:
                f.truncate(3 * d.G)
            d.write_minimal_gpt(image, parts)
            observed = d.module('test_min_gpt', 'inspect-recovery.py').inspect(image)
            self.assertTrue(observed['gpt_verified'])
            self.assertEqual([(p['index'], p['name'], p['offset'], p['bytes']) for p in observed['partitions']],
                             [(p['index'], p['name'], p['offset'], p['bytes']) for p in parts])

    def test_build_lts_full_image_defaults_to_minimal_partitions(self):
        from unittest import mock
        entry = d.module('test_build_lts_defaults', 'build-lts.py')
        captured = {}
        def fake_full(args, _compile):
            captured['minimal'] = args.minimal_partitions
            return {'ok': True}
        with mock.patch('full_image.build', side_effect=fake_full):
            entry.main(['--output', '/tmp/unused-default-check'])
            self.assertTrue(captured['minimal'])
            entry.main(['--output', '/tmp/unused-legacy-check', '--preserve-template-layout'])
            self.assertFalse(captured['minimal'])


    def test_storage_ready_stage_preserves_init_rc_basename_for_kernelsu(self):
        """KernelSU is_init_rc() checks dentry->d_name == 'init.rc'; bind-mounted file must keep basename init.rc."""
        script = (d.ROOT / 'probe/test2/storage-ready-stage').read_text(encoding='utf-8')
        subprocess.run(['sh', '-n'], input=script, text=True, check=True)
        self.assertNotIn('storage-ready-init.rc', script)
        self.assertIn('/init.rc"', script)
        self.assertIn('mount --bind "$target" "$source"', script)

    def test_compute_super_sha256_and_ota_info_embedding(self):
        """compute_super_sha256 must match written compacted super bytes and embed into etc/spx-ota-info."""
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source = root / 'source.img'
            src_super_off = 2 * d.M
            with source.open('wb') as f:
                f.truncate(12 * d.M)
                f.seek(src_super_off + 1 * d.M)
                f.write(b'A' * 1024 + b'\xe2\xe1\xf5\xe0' + b'B' * (d.M - 1028))
                f.seek(src_super_off + 5 * d.M)
                f.write(b'C' * 1024 + b'\xe2\xe1\xf5\xe0' + b'D' * (d.M - 1028))
            slices = f'system_a {1*d.M} {1*d.M}\nvendor_a {5*d.M} {1*d.M}\n'.encode()
            compacted_super, slice_plan, _ = d.compact_super_slices(slices, source, src_super_off, 10 * d.M)
            computed = d.compute_super_sha256(source, src_super_off, compacted_super, slice_plan=slice_plan)

            # Materialize compacted super and verify exact SHA-256 equality.
            out = root / 'super.raw'
            with out.open('wb') as f:
                f.truncate(compacted_super)
            for _, old_off, new_off, sz in slice_plan:
                d.copy_region(source, out, src_super_off + old_off, sz, new_off)
            self.assertEqual(computed, d.sha(out))

            # Verify storage_payload updates etc/spx-ota-info with SPX_OTA_SUPER_SHA256.
            cpio = d.cpio_module()
            init = (b'#!/bin/sh\nsq2-loop-slice "${ssd_part}3" "$offset" "$bytes"\n' + d.HOOK + d.ANCHOR)
            entries = {
                'init': (0o100755, init),
                'bin/apply-single-user': (0o100755, b'user0'),
                'bin/prepare-native-ssd': (0o100755, b'old'),
                'etc/sq2-ssd-layout': (0o100644, b'old'),
                'etc/sq2-ssd-partitions': (0o100644, b'old'),
                'etc/spx-ota-info': (0o100644, b'SPX_OTA_VERSION=v0.7.6\nSPX_OTA_SUPER_SHA256=\n'),
            }
            base_with_ota = gzip.compress(
                b''.join(cpio.entry(n, b, m) for n, (m, b) in entries.items()) + cpio.entry('TRAILER!!!', b'', 0),
                mtime=0,
            )
            parts = [{'index': i, 'name': n, 'bytes': 64 * d.M}
                     for i, n in enumerate(['super', 'userdata', 'metadata', 'misc', 'SQ2PROBE'], 1)]
            payload, _, _ = d.storage_payload(base_with_ota, parts, anonymous=True, super_sha256=computed)
            after = cpio.entries(gzip.decompress(payload))
            from datetime import datetime, timezone
            yymmdd = datetime.now(timezone.utc).strftime('%y%m%d')
            ota_info_after = after['etc/spx-ota-info'][1].decode()
            self.assertIn(f'SPX_OTA_SUPER_SHA256={computed}\n', ota_info_after)
            self.assertIn(f'SPX_OTA_BUILD_DISPLAY=v0.7.6.{yymmdd}\n', ota_info_after)


if __name__=='__main__':unittest.main()
