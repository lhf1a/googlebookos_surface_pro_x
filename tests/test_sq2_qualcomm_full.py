"""Offline synthetic CPIO/ELF fixtures; never load a driver or touch an ESP.

The helper tests rewrite its absolute paths into a temporary Android fixture.
A synthetic receipt exercises file placement only, not real cache readiness.
"""
import argparse
import gzip
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('qualcomm_full_pack', ROOT/'scripts/pack-sq2-qualcomm-full.py')
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)
HELPER = ROOT/'probe/graphics/qualcomm-full-start'
MESA = ('egl/libEGL_freedreno.so', 'egl/libGLESv1_CM_freedreno.so',
        'egl/libGLESv2_freedreno.so', 'hw/vulkan.freedreno.so')


def archive(entries):
    raw = b''.join(M.cpio.entry(n, data, mode) for n, (mode, data) in entries.items())
    raw += M.cpio.entry('TRAILER!!!', b'', 0)
    return gzip.compress(raw+b'\0'*(-len(raw) % 512), mtime=0)


def arm64_elf(name):
    # Only the packer's architecture/header contract, not executable donor code.
    data = bytearray(64)
    data[:6] = b'\x7fELF\x02\x01'
    data[18:20] = (183).to_bytes(2, 'little')
    return bytes(data)+name.encode()


@unittest.skipUnless(shutil.which('sh') and shutil.which('sha256sum'), 'POSIX host tools required')
class QualcommFullPack(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='qualcomm-full-fixture-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.project = self.root/'project'
        (self.project/'configs').mkdir(parents=True)
        (self.project/'probe/graphics').mkdir(parents=True)
        for name in ('qualcomm-full-start', 'chrome-graphics-cache.sh'):
            shutil.copyfile(ROOT/'probe/graphics'/name, self.project/'probe/graphics'/name)
        patch = mock.patch.object(M, 'ROOT', self.project)
        patch.start()
        self.addCleanup(patch.stop)
        self.runtime = self.root/'runtime'
        self.runtime.mkdir()
        for name in M.LIBRARIES:
            (self.runtime/name).write_bytes(arm64_elf(name))
        mapper = self.runtime/'libqti-mapper3-bridge.so'
        mapper.write_bytes(mapper.read_bytes()+b'/vendor/lib64/egl/libsq2map.so\0')
        (self.runtime/M.CPU_SHIM).write_bytes(arm64_elf(M.CPU_SHIM))
        (self.runtime/'qgl_config.txt').write_bytes(b'baseline qgl_config.txt\n')
        pin = mock.patch.object(M, 'DEPLOYABLE_RUNTIME_SUMS_SHA', '')
        pin.start()
        self.addCleanup(pin.stop)
        self.runtime_hashes()
        parent = {
            'init': (0o100755, b'#!/bin/sh\n'+M.HOOK+b'echo android-init\n'),
            'etc': (0o40755, b''),
            'bin': (0o40755, b''),
            'etc/googlebook-security/build.prop': (0o100644, b'ro.hardware.egl=freedreno\nro.hardware.vulkan=freedreno\n'),
            'lib/modules/6.18.60/untouched.ko': (0o100644, b'baseline module fixture'),
            'lib/firmware/untouched.bin': (0o100644, b'original firmware fixture'),
            'etc/unchanged': (0o100600, b'effective last entry'),
        }
        for name in MESA:
            parent['etc/googlebook-security/lib64/'+name] = (0o100644, ('mesa '+name).encode())
        for name in ('qgl_config.txt', 'sq2-closed-vulkan.rc', 'vulkan-version.xml'):
            parent['etc/sq2-vulkan-closed/'+name] = (0o100644, ('baseline '+name+'\n').encode())
        for name in list(parent):
            for directory in reversed(Path(name).parents):
                if str(directory) != '.':
                    parent.setdefault(directory.as_posix(), (0o40755, b''))
        # A duplicate in an earlier member tests effective last-entry semantics.
        self.prefix = archive({'etc/unchanged': (0o100644, b'obsolete entry')})+archive(parent)
        self.credential = archive({'etc/shadow': (0o100600, b'fixture-locked-account:!:\n'),
                                   'bin/dropbear': (0o100755, b'fixture, not executable SSH')})
        self.base = self.root/'base.cpio.gz'
        self.base.write_bytes(self.prefix+self.credential)
        self.accept_base()
        self.output = self.root/'fresh-output'
        self.args = argparse.Namespace(base=self.base, runtime=self.runtime, output=self.output)

    def accept_base(self):
        (self.project/'configs/kernel-baseline.json').write_text(json.dumps({
            'kernel_release': '6.18.60',
            'files': {'initramfs.cpio.gz': M.sha(self.base.read_bytes())}}))

    def runtime_hashes(self):
        (self.runtime/'SHA256SUMS').write_text(''.join(
            f'{M.sha((self.runtime/name).read_bytes())}  {name}\n'
            for name in sorted(M.LIBRARIES|{M.CPU_SHIM, 'qgl_config.txt'})))
        # Synthetic bytes exercise packaging; they are never accepted by the
        # production private-runtime pin outside this isolated module fixture.
        M.DEPLOYABLE_RUNTIME_SUMS_SHA = M.sha((self.runtime/'SHA256SUMS').read_bytes())

    def test_changed_only_init_and_new_namespace_originals_preserved(self):
        original = self.base.read_bytes()
        before = M.cpio.entries(gzip.decompress(original))
        report = M.build(self.args)
        candidate = (self.output/'initramfs.cpio.gz').read_bytes()
        after = M.cpio.entries(gzip.decompress(candidate))
        self.assertEqual(self.base.read_bytes(), original)
        self.assertTrue(candidate.startswith(original))
        self.assertTrue(candidate.endswith(self.credential))
        self.assertEqual(after['init'][1], before['init'][1].replace(M.HOOK, M.NEW_HOOK))
        for name in before.keys()-{'init'}:
            self.assertEqual(after[name], before[name], name)
        changes = {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)}
        self.assertEqual(changes, set(report['changed_effective_paths']))
        self.assertTrue(all(n == 'init' or n.startswith('etc/sq2-graphics-full') or
                            n in ('bin/qualcomm-full-start', 'bin/chrome-graphics-cache') for n in changes))
        self.assertEqual(after['etc/unchanged'], (0o100600, b'effective last entry'))
        self.assertEqual(len(report['runtime_sha256']), 15)
        self.assertFalse(report['cache_receipt_single_use'])
        self.assertFalse(report['cache_receipt_required'])
        self.assertEqual(report['cache_cleanup_trigger'], 'postboot-on-demand')
        self.assertIsNone(report['cache_receipt_schema'])
        self.assertEqual(report['cache_scope'], 'chrome-graphics-only')
        self.assertEqual(report['cache_backup'], 'none')
        self.assertIn('bin/chrome-graphics-cache', after)
        self.assertNotIn('bin/graphics-backend-cache', after)
        for name in ('hardware_tested', 'deployed', 'rebooted'):
            self.assertFalse(report[name])
        self.assertFalse(any('graphics-cache-receipt' in n or n.endswith('/ready') for n in after))
        self.assertTrue(report['original_mesa_preserved'])

    def test_explicit_paired_venus_parent_preserves_its_modules(self):
        original = self.base.read_bytes()
        # Separate pinned candidate parent, while published baseline remains unchanged.
        candidate = original + archive({'lib/modules/6.18.60/venus-fixture.ko':
                                        (0o100644, b'paired Venus compatibility module')})
        self.base.write_bytes(candidate)
        with mock.patch.object(M, 'VENUS_618_BASE_SHA', M.sha(candidate)):
            report = M.build(self.args)
        after = M.cpio.entries(gzip.decompress((self.output/'initramfs.cpio.gz').read_bytes()))
        self.assertTrue(report['venus_618_omx_prerequisites'])
        self.assertEqual(after['lib/modules/6.18.60/venus-fixture.ko'][1],
                         b'paired Venus compatibility module')
        self.assertEqual((self.output/'restored.cpio.gz').read_bytes(), candidate)

    def test_copy_rollback_and_exact_reapply_are_real_commands(self):
        M.build(self.args)
        report = json.loads((self.output/'VERIFICATION.txt').read_text())
        records = {r['role']: r for r in report['commands']}
        for record in records.values():
            self.assertEqual(record['exit'], 0)
            self.assertEqual(record['stderr'], '')
        baseline = json.loads(records['BASELINE']['stdout'])
        modified = json.loads(records['MODIFIED']['stdout'])
        rollback = json.loads(records['ROLLBACK']['stdout'])
        self.assertEqual(baseline, rollback)
        self.assertEqual(baseline['full_runtime_libraries'], 0)
        self.assertEqual(modified['full_runtime_libraries'], 15)
        self.assertFalse(modified['cache_receipt_single_use'])
        self.assertFalse(modified['cache_receipt_required'])
        self.assertEqual(modified['cache_cleanup_trigger'], 'postboot-on-demand')
        self.assertEqual(modified['backend_selector'], 'qualcomm-full-opt-in')
        self.assertEqual((self.output/'restored.cpio.gz').read_bytes(), self.base.read_bytes())
        self.assertEqual((self.output/'reapplied.cpio.gz').read_bytes(), (self.output/'initramfs.cpio.gz').read_bytes())
        self.assertIn('RESTORED_ACCEPTED_MESA_INITRAMFS', records['ROLLBACK_EXECUTE']['stdout'])
        # APPLY must not overwrite an already-existing path.
        p = subprocess.run(['sh', str(self.output/'APPLY.sh'), str(self.base), str(self.output/'reapplied.cpio.gz')], capture_output=True)
        self.assertEqual(p.returncode, 64)

    def test_inspect_legacy_package_does_not_mislabel_chrome_policy(self):
        legacy = self.root/'legacy-full.cpio.gz'
        legacy.write_bytes(archive({'init': (0o100755, M.NEW_HOOK),
            'bin/qualcomm-full-start': (0o100755, b'graphics-cache-v1\nmkdir "$receipt/activation"\n')}))
        info = M.inspect(legacy)
        self.assertEqual(info['cache_scope'], 'all-app-graphics')
        self.assertEqual(info['cache_backup'], 'transaction-backup')
        self.assertEqual(info['cache_receipt_schema'], 'graphics-cache-v1')

    def test_fresh_output_required(self):
        self.output.mkdir()
        sentinel = self.output/'original'
        sentinel.write_bytes(b'keep')
        with self.assertRaisesRegex(ValueError, 'Fresh output'):
            M.build(self.args)
        self.assertEqual(list(self.output.iterdir()), [sentinel])
        self.assertEqual(sentinel.read_bytes(), b'keep')

    def test_baseline_hash_rejected_before_output(self):
        self.base.write_bytes(self.base.read_bytes()+b'wrong')
        with self.assertRaisesRegex(ValueError, 'baseline hash mismatch'):
            M.build(self.args)
        self.assertFalse(self.output.exists())

    def test_runtime_hash_rejected_before_output(self):
        name = sorted(M.LIBRARIES)[0]
        (self.runtime/name).write_bytes(arm64_elf(name)+b'wrong')
        with self.assertRaisesRegex(ValueError, 'Runtime hash mismatch'):
            M.build(self.args)
        self.assertFalse(self.output.exists())

    def test_full_runtime_requires_every_library(self):
        manifest = self.runtime/'SHA256SUMS'
        manifest.write_text('\n'.join(manifest.read_text().splitlines()[1:])+'\n')
        with self.assertRaisesRegex(ValueError, '15-library'):
            M.build(self.args)
        self.assertFalse(self.output.exists())

    def test_wrong_elf_architecture_rejected_after_matching_hash(self):
        name = sorted(M.LIBRARIES)[0]
        data = bytearray((self.runtime/name).read_bytes())
        data[18:20] = (62).to_bytes(2, 'little')
        (self.runtime/name).write_bytes(data)
        self.runtime_hashes()
        with self.assertRaisesRegex(ValueError, 'Not arm64 ELF'):
            M.build(self.args)
        self.assertFalse(self.output.exists())

    def test_missing_private_credential_tail_rejected(self):
        self.base.write_bytes(self.prefix)
        self.accept_base()
        with self.assertRaisesRegex(ValueError, 'SSH tail not found'):
            M.build(self.args)
        self.assertFalse(self.output.exists())

    def test_unexpected_parent_hook_rejected(self):
        self.base.write_bytes(archive({'init': (0o100755, M.HOOK*2)})+self.credential)
        self.accept_base()
        with self.assertRaisesRegex(ValueError, 'Unexpected graphics hook'):
            M.build(self.args)
        self.assertFalse(self.output.exists())

    def helper_fixture(self, cmdline):
        """Every absolute runtime location is redirected to this test's root."""
        sandbox = self.root/'sandbox'
        sandbox.mkdir()
        script = HELPER.read_text()
        for path in ('/etc/sq2-graphics-full', '/newroot', '/media/esp', '/proc/cmdline', '/proc/mounts'):
            script = script.replace(path, str(sandbox/path.lstrip('/')))
        helper = sandbox/'helper.sh'
        helper.write_text(script)
        (sandbox/'proc').mkdir()
        (sandbox/'proc/cmdline').write_text(cmdline+'\n')
        (sandbox/'proc/mounts').write_text(f'overlay {sandbox}/newroot/vendor overlay rw 0 0\n')
        return sandbox, helper

    def test_mesa_does_not_implicitly_select_full_stack(self):
        sandbox, helper = self.helper_fixture('sq2.vulkan=mesa sq2.el2=1')
        before = sorted(str(p.relative_to(sandbox)) for p in sandbox.rglob('*'))
        p = subprocess.run(['sh', str(helper)], capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn('SQ2_FULL_GRAPHICS_DISABLED_MESA', p.stdout)
        self.assertEqual(sorted(str(p.relative_to(sandbox)) for p in sandbox.rglob('*')), before)

    def test_no_receipt_boots_without_creating_or_clearing_cache_state(self):
        sandbox, helper, source, vendor, receipt, entries = self.activation_fixture()
        shutil.rmtree(receipt)
        p = subprocess.run(['sh', str(helper)], capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout+p.stderr)
        self.assertIn('SQ2_FULL_GRAPHICS_SELECTED', p.stdout)
        self.assertFalse(receipt.exists())
        self.assertTrue((vendor/'lib64/egl/libGLESv2_freedreno.so').is_symlink())

    def test_file_placement_preserves_unrelated_receipt_unchanged(self):
        sandbox, helper, source, vendor, receipt, entries = self.activation_fixture()
        before = {str(p.relative_to(receipt)):p.read_bytes() for p in receipt.rglob('*') if p.is_file()}
        p = subprocess.run(['sh', str(helper)], capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout+p.stderr)
        self.assertFalse((receipt/'activation').exists())
        self.assertEqual(before, {str(p.relative_to(receipt)):p.read_bytes() for p in receipt.rglob('*') if p.is_file()})
        for name, target in {'libEGL_freedreno.so':'libquest2-egl-wrapper.so',
                             'libGLESv1_CM_freedreno.so':'libGLESv1_CM_adreno.so',
                             'libGLESv2_freedreno.so':'libGLESv2_adreno.so'}.items():
            path = vendor/'lib64/egl'/name
            self.assertTrue(path.is_symlink())
            self.assertEqual(path.readlink(), Path(target))
            self.assertEqual(path.read_bytes(), (self.runtime/target).read_bytes())
        self.assertEqual((vendor/'lib64/hw/vulkan.freedreno.so').read_bytes(), (self.runtime/'libquest2-vulkan-wrapper.so').read_bytes())
        for name in MESA:
            self.assertEqual((sandbox/'newroot/debug_ramdisk/mesa-graphics-original'/name).read_bytes(), entries['etc/googlebook-security/lib64/'+name][1])

    def activation_fixture(self):
        M.build(self.args)
        sandbox, helper = self.helper_fixture('sq2.vulkan=qcom sq2.el2=1')
        entries = M.cpio.entries(gzip.decompress((self.output/'initramfs.cpio.gz').read_bytes()))
        source = sandbox/'etc/sq2-graphics-full'
        source.mkdir(parents=True)
        for name, (mode, body) in entries.items():
            if name.startswith('etc/sq2-graphics-full/'):
                (source/Path(name).name).write_bytes(body)
        vendor = sandbox/'newroot/vendor'
        for name in MESA:
            dest = vendor/'lib64'/name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(entries['etc/googlebook-security/lib64/'+name][1])
        for directory in ('etc/init', 'etc/permissions'):
            (vendor/directory).mkdir(parents=True)
        receipt = sandbox/'media/esp/EFI/SQ2QDbg/graphics-cache-receipt'
        receipt.mkdir(parents=True)
        for name, text in {'schema': 'chrome-graphics-cache-v1', 'mode': 'apply', 'status': 'complete',
                           'from': 'mesa', 'to': 'qualcomm',
                           'ready': 'target=qualcomm\ncoverage=chrome-graphics-only\ncache-backup=none'}.items():
            (receipt/name).write_text(text+'\n')
        return sandbox, helper, source, vendor, receipt, entries

    def test_claimed_or_old_receipt_is_not_a_boot_gate(self):
        sandbox, helper, source, vendor, receipt, entries = self.activation_fixture()
        (receipt/'activation').mkdir()
        (receipt/'activation/status').write_text('consumed\n')
        (receipt/'schema').write_text('graphics-cache-v1\n')
        before = {str(p.relative_to(receipt)):p.read_bytes() for p in receipt.rglob('*') if p.is_file()}
        p = subprocess.run(['sh', str(helper)], capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout+p.stderr)
        self.assertEqual(before, {str(p.relative_to(receipt)):p.read_bytes() for p in receipt.rglob('*') if p.is_file()})

    def test_dangling_receipt_symlink_is_neither_followed_nor_changed(self):
        sandbox, helper, source, vendor, receipt, entries = self.activation_fixture()
        shutil.rmtree(receipt)
        receipt.symlink_to('missing-external-receipt')
        p = subprocess.run(['sh', str(helper)], capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout+p.stderr)
        self.assertEqual(receipt.readlink(), Path('missing-external-receipt'))
        self.assertFalse((receipt.parent/'missing-external-receipt').exists())

    def test_mesa_hash_failure_still_stops_before_vendor_mutation(self):
        sandbox, helper, source, vendor, receipt, entries = self.activation_fixture()
        (vendor/'lib64/egl/libGLESv2_freedreno.so').write_bytes(b'wrong mesa fixture')
        p = subprocess.run(['sh', str(helper)], capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)
        self.assertFalse((sandbox/'newroot/debug_ramdisk').exists())
        self.assertFalse((vendor/'lib64/egl/libGLESv2_adreno.so').exists())

    def test_overlay_failure_still_stops_before_vendor_mutation(self):
        sandbox, helper, source, vendor, receipt, entries = self.activation_fixture()
        (sandbox/'proc/mounts').write_text('')
        p = subprocess.run(['sh', str(helper)], capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)
        self.assertFalse((sandbox/'newroot/debug_ramdisk').exists())

    def test_destination_collision_still_stops_before_vendor_mutation(self):
        sandbox, helper, source, vendor, receipt, entries = self.activation_fixture()
        target = vendor/'lib64/egl/libGLESv2_adreno.so'
        target.write_bytes(b'preserve-existing')
        p = subprocess.run(['sh', str(helper)], capture_output=True, text=True)
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual(target.read_bytes(), b'preserve-existing')
        self.assertFalse((sandbox/'newroot/debug_ramdisk').exists())

    def test_unaccepted_complete_runtime_manifest_is_rejected(self):
        M.DEPLOYABLE_RUNTIME_SUMS_SHA = '0'*64
        with self.assertRaisesRegex(ValueError, 'runtime manifest pin mismatch'):
            M.build(self.args)
        self.assertFalse(self.output.exists())

    def test_runtime_qgl_bytes_are_not_silently_ignored(self):
        (self.runtime/'qgl_config.txt').write_bytes(b'other config')
        self.runtime_hashes()
        with self.assertRaisesRegex(ValueError, 'qgl_config differs'):
            M.build(self.args)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
