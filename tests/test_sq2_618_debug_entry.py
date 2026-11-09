"""Offline entry builder: exact baseline bytes, EFI pointers and additive GRUB.

Synthetic PE/payload fixtures test transformation, not UEFI or graphics runtime.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('debug_entry', ROOT/'scripts/prepare-sq2-618-debug-entry.py')
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)

OPTIONS = ('console=tty0 suspend.screen_only=0 suspend.screen_only_autosuspend=0 '
           'nohibernate panic=0 sq2.el2=1 sq2.vulkan=mesa sq2.display=2.7-60 '
           'initrd=\\EFI\\SQ2L618C\\initramfs.cpio.gz')
GRUB = ("set timeout=10\nset default=sq2-default\n"
        "menuentry 'Current Mesa 6.18' --id sq2-default {\n"
        " search --no-floppy --fs-uuid --set=root 1234-ABCD\n"
        " chainloader /EFI/SQ2L618C/start.efi\n}\n"
        "menuentry 'Existing fallback' --id sq2-fallback {\n"
        " search --no-floppy --fs-uuid --set=root 1234-ABCD\n"
        " chainloader /EFI/SQ2Fallback/el2launch.efi\n}\n").encode()


def pe(strings):
    raw = bytearray(512)
    raw[:2] = b'MZ'
    struct.pack_into('<I', raw, 60, 128)
    raw[128:132] = b'PE\0\0'
    struct.pack_into('<H', raw, 132, 0xaa64)
    struct.pack_into('<H', raw, 152, 0x20b)
    for text in strings:
        raw.extend(text.encode('utf-16le')+b'\0\0'+b'\xff'*8)
    return bytes(raw)


class EntryBuilder(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.base = self.root/'accepted'
        self.base.mkdir()
        image = bytearray(128); image[0x38:0x3c] = b'ARMd'
        launcher = pe(['\\EFI\\SQ2L618C\\'+n for n in
                       ['Image', 'android-el2.dtb', 'qebspilaa64.efi', 'tcblaunch.exe']]+[OPTIONS])
        self.files = {'Image': bytes(image), 'android-el2.dtb': b'\xd0\x0d\xfe\xedfixture',
             'initramfs.cpio.gz': gzip.compress(b'baseline cpio fixture', mtime=0),
             'start.efi': pe(['\\EFI\\SQ2L618C\\el2launch.efi', '\\EFI\\SQ1Shared\\start.efi']),
             'el2launch.efi': launcher, 'kernel-options.txt': (OPTIONS+'\n').encode(),
             'qebspilaa64.efi': b'signed firmware fixture', 'tcblaunch.exe': b'signed TCB fixture'}
        self.manifest = self.root/'baseline.json'
        self.write_baseline()
        self.payload = self.root/'payload.cpio.gz'
        self.payload.write_bytes(gzip.compress(b'explicit graphics fixture', mtime=0))
        self.grub = self.root/'original-grub.cfg'; self.grub.write_bytes(GRUB)
        self.args = argparse.Namespace(baseline=self.base, manifest=self.manifest,
            payload=self.payload, payload_sha256=M.sha(self.payload.read_bytes()),
            grub_input=self.grub, output=self.root/'candidate', directory='SQ2QDbg',
            graphics_argument='sq2.vulkan=qcom')

    def tearDown(self):
        self.temp.cleanup()

    def write_baseline(self):
        for name, data in self.files.items(): (self.base/name).write_bytes(data)
        self.manifest.write_text(json.dumps({'kernel_release':'6.18.60-sq2-android1',
            'bundle_directory':'EFI/SQ2L618C', 'files':{n:M.sha(b) for n,b in self.files.items()}}))

    def test_complete_package_preserves_baseline_and_bmr(self):
        report = M.build(self.args)
        out = self.args.output
        self.assertEqual(M.verify_output(out), report)
        for name, before in self.files.items(): self.assertEqual((self.base/name).read_bytes(), before)
        for name in ['Image','android-el2.dtb','tcblaunch.exe','qebspilaa64.efi']:
            self.assertEqual((out/'EFI/SQ2QDbg'/name).read_bytes(), self.files[name])
        command = (out/'EFI/SQ2QDbg/kernel-options.txt').read_text()
        self.assertEqual(command, OPTIONS.replace('SQ2L618C','SQ2QDbg').replace('sq2.vulkan=mesa','sq2.vulkan=qcom')+'\n')
        self.assertIn('\\EFI\\SQ1Shared\\start.efi', [s for _,_,s in M.utf16_strings((out/'EFI/SQ2QDbg/start.efi').read_bytes())])
        self.assertTrue((out/'EFI/BOOT/grub.cfg').read_bytes().startswith(GRUB))
        evidence = json.loads((out/'VERIFICATION.txt').read_text())
        self.assertEqual([r['exit'] for r in evidence['commands']], [0]*4)
        self.assertEqual([json.loads(evidence['commands'][i]['stdout'])['menu_entries'] for i in (0,1,3)], [2,3,2])
        self.assertEqual(evidence['restored_sha256'], M.sha(GRUB))
        self.assertEqual((out/'rollback-check/EFI/BOOT/grub.cfg').read_bytes(), GRUB)
        for path in report['roles'].values(): self.assertTrue(Path(path).read_bytes())

    def test_existing_qualcomm_baseline_keeps_effective_options(self):
        for name in ('kernel-options.txt', 'el2launch.efi'):
            old = 'sq2.vulkan=mesa'.encode('utf-16le' if name.endswith('.efi') else 'utf-8')
            new = 'sq2.vulkan=qcom'.encode('utf-16le' if name.endswith('.efi') else 'utf-8')
            self.files[name] = self.files[name].replace(old, new)
        self.write_baseline()
        report = M.build(self.args)
        self.assertEqual(report['effective_utf16_options'],
                         OPTIONS.replace('SQ2L618C', 'SQ2QDbg').replace('sq2.vulkan=mesa', 'sq2.vulkan=qcom'))

    def test_ambiguous_or_unknown_graphics_option_rejected(self):
        for options in ('sq2.vulkan=qcom sq2.vulkan=mesa', 'sq2.vulkan=unknown', ''):
            with self.assertRaisesRegex(ValueError, 'graphics option'):
                M.graphics_option(options)

    def test_unified_patch_reconstructs_menu(self):
        if not shutil.which('patch'): self.skipTest('patch command unavailable')
        M.build(self.args)
        tree=self.root/'patch-copy'; (tree/'EFI/BOOT').mkdir(parents=True)
        (tree/'EFI/BOOT/grub.cfg').write_bytes(GRUB)
        p=subprocess.run(['patch','-p1','-d',str(tree),'-i',str(self.args.output/'GRUB.patch')],capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertEqual((tree/'EFI/BOOT/grub.cfg').read_bytes(),(self.args.output/'EFI/BOOT/grub.cfg').read_bytes())

    def test_payload_hash_mismatch_leaves_no_output(self):
        self.args.payload_sha256='0'*64
        with self.assertRaisesRegex(ValueError,'payload SHA'): M.build(self.args)
        self.assertFalse(self.args.output.exists())

    def test_baseline_hash_mismatch_rejected(self):
        (self.base/'Image').write_bytes(b'wrong version')
        with self.assertRaisesRegex(ValueError,'Baseline hash'): M.build(self.args)

    def test_signed_launcher_rejected_even_with_fixture_manifest(self):
        raw=bytearray(self.files['el2launch.efi'])
        struct.pack_into('<II',raw,152+112+32,100,100)
        self.files['el2launch.efi']=bytes(raw);self.write_baseline()
        with self.assertRaisesRegex(ValueError,'Signed EFI'): M.build(self.args)

    def test_path_growth_and_traversal_rejected(self):
        for directory in ['../evil','SQ2Qcom618Dbg']:
            self.args.directory=directory
            with self.assertRaises(ValueError): M.build(self.args)
            self.assertFalse(self.args.output.exists())

    def test_old_kernel_manifest_rejected(self):
        data=json.loads(self.manifest.read_text());data['kernel_release']='6.3.8-sq2'
        self.manifest.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError,'6.18 baseline'): M.build(self.args)

    def test_existing_output_and_extra_entry_rejected(self):
        self.args.output.mkdir()
        with self.assertRaisesRegex(ValueError,'Fresh output'): M.build(self.args)
        self.args.output.rmdir()
        self.grub.write_bytes(GRUB+b"menuentry 'extra' {\n}\n")
        with self.assertRaisesRegex(ValueError,'two entries'): M.build(self.args)

    def test_verify_rejects_postbuild_efi_tamper(self):
        M.build(self.args)
        p=self.args.output/'EFI/SQ2QDbg/el2launch.efi';p.write_bytes(p.read_bytes()+b'bad')
        with self.assertRaisesRegex(ValueError,'Output hash'): M.verify_output(self.args.output)

    def test_rollback_refuses_unrelated_menu_and_keeps_candidate(self):
        M.build(self.args)
        target=self.args.output/'rollback-check/EFI/BOOT/grub.cfg';target.write_bytes(b'unrelated menu\n')
        p=subprocess.run(['sh',str(self.args.output/'ROLLBACK.sh'),str(self.args.output/'rollback-check')],capture_output=True,text=True)
        self.assertNotEqual(p.returncode,0)
        self.assertEqual(target.read_bytes(),b'unrelated menu\n')
        self.assertEqual(M.inspect_grub((self.args.output/'EFI/BOOT/grub.cfg').read_bytes())['menu_entries'],3)


if __name__ == '__main__':
    unittest.main()
