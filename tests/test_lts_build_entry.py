"""Actual single-version entry behavior, not a claim of hardware acceptance."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from kernel_baseline import policy, validate_build_tree, validate_kernel_source
spec = importlib.util.spec_from_file_location('lts_build', ROOT / 'scripts/build-lts.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class LtsEntry(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.bundle = self.root / 'bundle'
        self.bundle.mkdir()
        self.selected = dict(policy())
        for name in build.REQUIRED:
            body = (b'sq2.el2=1 nohibernate suspend.screen_only=0 suspend.screen_only_autosuspend=0\n'
                    if name == 'kernel-options.txt' else ('fixture-' + name).encode())
            (self.bundle / name).write_bytes(body)
        self.selected['files'] = {n: build.digest(self.bundle / n) for n in build.REQUIRED}
        self.sums()

    def sums(self):
        (self.bundle / 'SHA256SUMS').write_text(''.join(h + '  ' + n + '\n' for n, h in sorted(self.selected['files'].items())))

    def test_package_byte_exact_and_private(self):
        result = build.package(self.bundle, self.root / 'export', self.selected)
        self.assertFalse(result['source_compiled'])
        self.assertTrue(result['private_identity_present'])
        for n in build.REQUIRED | {'SHA256SUMS'}:
            self.assertEqual((self.bundle / n).read_bytes(), (self.root / 'export' / self.selected['bundle_directory'] / n).read_bytes())
        with self.assertRaisesRegex(ValueError, 'fresh'):
            build.package(self.bundle, self.root / 'export', self.selected)

    def test_package_rejects_escaping_directory_before_writing(self):
        for location in ('../escape', '/escape', '.'):
            with self.assertRaisesRegex(ValueError, 'bundle directory'):
                build.package(self.bundle, self.root / 'export', dict(self.selected, bundle_directory=location))
            self.assertFalse((self.root / 'export').exists())

    def test_reject_tampered_modules_or_dtb_container(self):
        for name in ('initramfs.cpio.gz', 'Image', 'android-el2.dtb'):
            old = (self.bundle / name).read_bytes()
            (self.bundle / name).write_bytes(old + b'wrong-ABI')
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                build.validate_bundle(self.bundle, self.selected)
            (self.bundle / name).write_bytes(old)

    def test_reject_missing_and_extra_manifest_names(self):
        for files in (dict(self.selected['files'], **{'../escape': '0'*64}), {}):
            with self.assertRaisesRegex(ValueError, 'file list'):
                build.validate_bundle(self.bundle, dict(self.selected, files=files))

    def test_reject_rehashed_old_boot_policy(self):
        p = self.bundle / 'kernel-options.txt'
        p.write_text('sq2.el2=experimental nohibernate\n')
        self.selected['files'][p.name] = build.digest(p)
        self.sums()
        with self.assertRaisesRegex(ValueError, 'boot policy'):
            build.validate_bundle(self.bundle, self.selected)

    def test_old_source_and_module_abi_rejected(self):
        for version, ok in (('6.3.8', False), ('6.18.60', True), ('6.18.61', False)):
            v, p, s = version.split('.')
            (self.root / 'Makefile').write_text(f'VERSION = {v}\nPATCHLEVEL = {p}\nSUBLEVEL = {s}\n')
            if ok:
                self.assertEqual(validate_kernel_source(self.root), version)
            else:
                with self.assertRaisesRegex(ValueError, 'Unsupported kernel source'):
                    validate_kernel_source(self.root)
        p = self.root / 'include/config/kernel.release'; p.parent.mkdir(parents=True)
        p.write_text('6.3.8-sq2-android1\n')
        with self.assertRaisesRegex(ValueError, 'Unsupported module ABI'):
            validate_build_tree(self.root)
        p.write_text(policy()['kernel_release'] + '\n')
        self.assertEqual(validate_build_tree(self.root), policy()['kernel_release'])

    def test_all_aliases_share_help_and_reject_legacy_disk_flags(self):
        for name in ('build.py', 'build-surface-pro-x.py', 'build-source-boot.py', 'build-source-disk.py', 'build-sq2-default.py'):
            p = subprocess.run([sys.executable, str(ROOT / 'scripts' / name), '--help'], capture_output=True, text=True)
            self.assertEqual(p.returncode, 0, p.stderr)
            self.assertIn('{import-private,full-image,verify,package,single-user,disk-image,compile}', p.stdout)
            p = subprocess.run([sys.executable, str(ROOT / 'scripts' / name), '--template', 'old.img'], capture_output=True)
            self.assertNotEqual(p.returncode, 0)


    def compile_fixture(self, core_only=True):
        source = self.root / 'source'; source.mkdir()
        (source / 'Makefile').write_text('VERSION = 6\nPATCHLEVEL = 18\nSUBLEVEL = 60\n')
        config = self.root / 'config'
        config.write_text('CONFIG_LOCALVERSION="-sq2-android1"\n')
        return argparse.Namespace(source=source, build_dir=self.root / 'build',
            output=self.root / 'compiled', external_sources=None, core_only=core_only,
            config=config, resume_build=False, cross_compile='/fixture/aarch64-linux-gnu-', jobs=2)

    def fake_make(self, args):
        def put(path, body):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(body)
        def run(command, stdout, stderr):
            stdout.write(('fixture command: ' + ' '.join(command) + '\n').encode())
            b = args.build_dir
            if 'olddefconfig' in command:
                put(b / 'include/config/kernel.release', (self.selected['kernel_release'] + '\n').encode())
            if 'Image' in command:
                for path in ('arch/arm64/boot/Image', 'System.map', 'Module.symvers', 'arch/arm64/boot/dts/qcom/fixture.dtb'):
                    put(b / path, ('fixture ' + path).encode())
            if 'modules_install' in command:
                stage = args.output / 'modules-stage/lib/modules' / self.selected['kernel_release']
                put(stage / 'kernel/stale/foo.ko', b'old')
                put(stage / 'kernel/keep.ko', b'unrelated')
            if 'modules' in command and any(s.startswith('M=') for s in command):
                path = Path(next(s[2:] for s in command if s.startswith('M=')))
                put(path / 'foo.ko', b'\x7fELF\x00vermagic=' + self.selected['kernel_release'].encode() + b' SMP\x00')
            return subprocess.CompletedProcess(command, 0)
        return run

    def test_compile_requires_explicit_external_or_incomplete_mode(self):
        args = self.compile_fixture(core_only=False)
        with self.assertRaisesRegex(ValueError, '--external-sources'):
            build.compile_kernel(args, self.selected)
        self.assertFalse(args.output.exists())
        self.assertFalse(args.build_dir.exists())

    def test_core_compilation_is_real_command_sequence_and_marked_incomplete(self):
        args = self.compile_fixture()
        with patch.object(build.subprocess, 'run', side_effect=self.fake_make(args)):
            result = build.compile_kernel(args, self.selected)
        self.assertTrue(result['source_compiled'])
        self.assertTrue(result['core_only'])
        self.assertFalse(result['accepted_baseline_replaced'])
        self.assertFalse(result['external_runtime_overrides_integrated'])
        self.assertIn('Image', result['files'])
        self.assertIn('dtbs/qcom/fixture.dtb', result['files'])
        self.assertEqual(result['commands'][1]['command'][-3:], ['Image', 'modules', 'dtbs'])
        self.assertIn('modules_install', result['commands'][2]['command'])
        self.assertTrue((args.output / 'kernel.stdout').read_text().startswith('fixture command:'))

    def test_external_modules_are_built_paired_and_installed_without_stale_duplicate(self):
        args = self.compile_fixture(core_only=False)
        args.external_sources = self.root / 'external'
        module = args.external_sources / 'foo'; module.mkdir(parents=True)
        (module / 'Makefile').write_text('obj-m += foo.o\n')
        config = self.root / 'configs'; config.mkdir()
        manifest = {'schema': 1, 'kernel_release': self.selected['kernel_release'], 'groups': [
            {'source': 'foo', 'make_options': ['CONFIG_FOO=m'], 'modules': {'foo.ko': 'extra/foo.ko'}}]}
        (config / 'kernel-external-modules.json').write_text(json.dumps(manifest))
        manifest_path = config / 'kernel-external-modules.json'
        manifest['groups'][0]['modules']['foo.ko'] = 'extra/wrong.ko'
        manifest_path.write_text(json.dumps(manifest))
        with patch.object(build, 'ROOT', self.root):
            with self.assertRaisesRegex(ValueError, 'module path'):
                build.external_manifest(args.external_sources, self.selected)
        manifest['groups'][0]['modules']['foo.ko'] = 'extra/foo.ko'
        manifest_path.write_text(json.dumps(manifest))
        with patch.object(build, 'ROOT', self.root), patch.object(build.subprocess, 'run', side_effect=self.fake_make(args)):
            result = build.compile_kernel(args, self.selected)
        self.assertTrue(result['external_runtime_overrides_integrated'])
        self.assertFalse(result['core_only'])
        self.assertFalse((module / 'foo.ko').exists(), 'preserved external source was modified')
        stage = args.output / 'modules-stage/lib/modules' / self.selected['kernel_release']
        self.assertTrue((stage / 'extra/foo.ko').exists())
        self.assertFalse((stage / 'kernel/stale/foo.ko').exists())
        self.assertEqual((stage / 'kernel/keep.ko').read_bytes(), b'unrelated')
        self.assertEqual(result['commands'][-1]['command'][0], 'depmod')
        self.assertIn('CONFIG_FOO=m', result['commands'][-2]['command'])
        self.assertEqual(result['external_modules'][0]['removed_duplicate_paths'], ['kernel/stale/foo.ko'])

    def test_command_failure_is_recorded_and_does_not_claim_completed_build(self):
        args = self.compile_fixture()
        with patch.object(build.subprocess, 'run', return_value=subprocess.CompletedProcess([], 7)):
            with self.assertRaisesRegex(RuntimeError, 'Compilation failed'):
                build.compile_kernel(args, self.selected)
        self.assertEqual(json.loads((args.output / 'commands.json').read_text())[0]['exit'], 7)
        self.assertFalse((args.output / 'build-audit.json').exists())

    def spi_resume_fixture(self):
        args = self.compile_fixture(core_only=False)
        args.external_sources = self.root / 'external'
        module = args.external_sources / 'spi'; module.mkdir(parents=True)
        (module / 'Makefile').write_text('obj-m += spi-hid.o\n')
        (module / 'spi-hid-core.c').write_text('preserved V1 source\n')
        groups = {'groups': [{'source': 'spi', 'modules': {'spi-hid.ko': 'extra/spi-hid.ko'}}]}
        return args, module, groups

    def test_spi_resume_patch_runs_on_copy_before_compilation(self):
        args, module, groups = self.spi_resume_fixture()
        fake = self.fake_make(args)
        def run(command, stdout, stderr):
            if any(s.endswith('patch-sq2-spi-pm.py') for s in command):
                copied = Path(command[-1])
                self.assertEqual(copied.resolve(), (args.output / 'external-build/spi').resolve())
                (copied / 'spi-hid-core.c').write_text('fixture patched recovery\n')
            if 'modules' in command and any(s.startswith('M=') for s in command):
                copied = Path(next(s[2:] for s in command if s.startswith('M=')))
                self.assertEqual((copied / 'spi-hid-core.c').read_text(), 'fixture patched recovery\n')
                (copied / 'spi-hid.ko').write_bytes(b'\x7fELF\x00vermagic=' + self.selected['kernel_release'].encode() + b' SMP\x00')
            return fake(command, stdout, stderr)
        with patch.object(build, 'external_manifest', return_value=groups), patch.object(build.subprocess, 'run', side_effect=run):
            result = build.compile_kernel(args, self.selected)
        self.assertEqual((module / 'spi-hid-core.c').read_text(), 'preserved V1 source\n')
        commands = [x['command'] for x in result['commands']]
        patch_index = next(i for i, c in enumerate(commands) if any(s.endswith('patch-sq2-spi-pm.py') for s in c))
        self.assertEqual(commands[patch_index + 1][-1], 'clean')

    def test_spi_resume_patch_failure_stops_before_module_compile(self):
        args, module, groups = self.spi_resume_fixture()
        fake = self.fake_make(args)
        def run(command, stdout, stderr):
            if any(s.endswith('patch-sq2-spi-pm.py') for s in command):
                return subprocess.CompletedProcess(command, 17)
            return fake(command, stdout, stderr)
        with patch.object(build, 'external_manifest', return_value=groups), patch.object(build.subprocess, 'run', side_effect=run):
            with self.assertRaisesRegex(RuntimeError, 'Compilation failed'):
                build.compile_kernel(args, self.selected)
        commands = json.loads((args.output / 'commands.json').read_text())
        self.assertEqual(commands[-1]['exit'], 17)
        self.assertFalse((args.output / 'external-build/spi/spi-hid.ko').exists())
        self.assertEqual((module / 'spi-hid-core.c').read_text(), 'preserved V1 source\n')


if __name__ == '__main__':
    unittest.main()
