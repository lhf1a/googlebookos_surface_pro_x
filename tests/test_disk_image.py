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
        self.assertIn(d.HOOK+d.ANCHOR,after['init'][1])
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
        for mode in ['normal','expanded','ambiguous','bad_super','small_super']:
            with self.subTest(mode=mode),tempfile.TemporaryDirectory() as td:
                root=Path(td)
                (root/'etc').mkdir();(root/'dev').mkdir();(root/'sys/class/block').mkdir(parents=True)
                (root/'esp-tree/EFI/SQ2Nat').mkdir(parents=True)
                (root/'esp-tree/EFI/SQ2Nat/start.efi').touch()
                (root/'esp-tree/SPX-IMAGE.txt').write_text('SPX_IMAGE_FORMAT=1\n')
                expected=hashlib.sha256(bytes(d.M)).hexdigest()
                (root/'etc/sq2-ssd-layout').write_text('SSD_SUPER_HEADER_SHA256='+('0'*64 if mode=='bad_super' else expected)+'\n')
                (root/'etc/sq2-ssd-partitions').write_text('userdata 1 32\nsuper 3 2048\nSQ2PROBE 6 1024\n')
                def partition(parent,name,number,sectors):
                    k=parent+'p'+str(number);p=root/'devices'/parent/k;p.mkdir(parents=True)
                    (p/'partition').write_text(str(number));(p/'size').write_text(str(sectors));(p/'uevent').write_text('PARTNAME='+name+'\n')
                    (root/'sys/class/block'/k).symlink_to(p)
                    (root/'dev'/k).write_bytes(bytes(d.M) if name=='super' else b'fixture')
                partition('nvme7n2','userdata',11,64 if mode=='expanded' else 32)
                partition('nvme7n2','super',8,1024 if mode=='small_super' else 2048)
                partition('nvme7n2','SQ2PROBE',5,1024)
                if mode=='ambiguous':partition('nvme8n3','SQ2PROBE',17,1024)
                code=source
                for old in ['/etc/sq2-ssd','/sys/class/block','/dev/','/run/','/media/']:
                    code=code.replace(old,str(root)+old)
                stub='''fail() { echo "FAIL:$1" >&2; exit 1; }
blkid() { echo 'TYPE="vfat" UUID="unrelated-random-UUID"'; }
mount() { for last; do :; done; mkdir -p "$last"; cp -a "$FIXTURE/esp-tree/." "$last/"; }
umount() { return 0; }
sleep() { return 0; }
'''
                p=subprocess.run(['sh','-c',stub+code],env=dict(os.environ,FIXTURE=td),text=True,capture_output=True)
                self.assertEqual(p.returncode==0,mode in ['normal','expanded','bad_super'],p.stderr)
                if p.returncode==0:
                    self.assertEqual((root/'run/sq2-ssd-device').read_text().strip(),str(root/'dev/nvme7n2'))
                else:self.assertIn('FAIL:',p.stderr)


if __name__=='__main__':unittest.main()
