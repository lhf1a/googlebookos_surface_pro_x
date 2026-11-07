"""Fixture-only checks of private Quest 2 staging, never loading vendor code."""
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('quest2_video_runtime',
    ROOT / 'scripts/prepare-quest2-video-runtime.py')
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


def minimal_elf(label, machine=183, elf_class=2):
    header = bytearray(20)
    header[:6] = b'\x7fELF' + bytes([elf_class, 1])
    struct.pack_into('<H', header, 18, machine)
    return bytes(header) + label.encode()


class Quest2VideoRuntime(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.reference = self.root / 'reference'
        self.reference.mkdir()
        self.output = self.root / 'stage'
        self.probe = self.root / 'fixture-probe'
        self.probe.write_bytes(minimal_elf('fixture probe'))
        self.inventory = {'image_sha256': '1' * 64, 'build': 'fixture-build', 'entries': []}
        self.config = {'image_sha256': '1' * 64, 'build': 'fixture-build', 'files': {}}
        self.needed = {
            'libOmxCore.so': ['libc.so', 'libdl.so', 'libsupport.so'],
            'libOmxVdec.so': ['libsupport.so', 'libexternal-platform.so', 'liblog.so'],
            'libsupport.so': ['libcycle.so'],
            'libcycle.so': ['libsupport.so'],
        }
        for name in self.needed:
            key = '/lib64/' + name
            self.add_entry(key, minimal_elf(name))
            if name in runtime.ROOT_LIBRARIES:
                self.config['files'][key] = runtime.digest(self.reference / 'vendor' / key.lstrip('/'))
        # Present in the donor, but not in the decoder's dependency closure.
        self.add_entry('/firmware/venus.mbn', b'fixture firmware must stay in donor')
        self.add_entry('/etc/init/vendor.media-service.rc', b'fixture service must stay in donor')
        self.add_entry('/etc/vintf/manifest/media.xml', b'fixture declaration must stay in donor')
        self.add_entry('/lib64/libunrelated.so', minimal_elf('unrelated'))
        (self.root / 'configs').mkdir()
        launcher = self.root / 'probe/video/run-quest2-omx-isolated.sh'
        launcher.parent.mkdir(parents=True)
        launcher.write_bytes((ROOT / 'probe/video/run-quest2-omx-isolated.sh').read_bytes())
        self.persist()
        root_patch = patch.object(runtime, 'ROOT', self.root)
        self.addCleanup(root_patch.stop)
        root_patch.start()
        readelf_patch = patch.object(runtime.subprocess, 'run', side_effect=self.fake_readelf)
        self.addCleanup(readelf_patch.stop)
        self.readelf = readelf_patch.start()

    def add_entry(self, key, body):
        path = self.reference / 'vendor' / key.lstrip('/')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(body)
        self.inventory['entries'].append({'path': key, 'regular': True,
            'bytes': len(body), 'sha256': runtime.digest(path)})

    def persist(self):
        (self.reference / 'inventory.json').write_text(json.dumps(self.inventory))
        (self.root / 'configs/quest2-v77-video-reference.json').write_text(json.dumps(self.config))

    def update_inventory(self, name):
        key = '/lib64/' + name
        path = self.reference / 'vendor' / key.lstrip('/')
        entry = next(e for e in self.inventory['entries'] if e['path'] == key)
        entry.update(bytes=path.stat().st_size, sha256=runtime.digest(path))
        self.persist()

    def fake_readelf(self, command, **kwargs):
        self.assertEqual(command[:2], ['fixture-readelf', '-dW'])
        self.assertTrue(kwargs['check'])
        name = Path(command[2]).name
        text = ''.join(f'0x1 (NEEDED) Shared library: [{n}]\n' for n in self.needed[name])
        return subprocess.CompletedProcess(command, 0, stdout=text, stderr='')

    def prepare(self, output=None):
        return runtime.prepare(self.reference, self.probe, output or self.output, 'fixture-readelf')

    def assert_not_written(self):
        self.assertFalse(self.output.exists())

    def test_dependency_closure_exact_copies_and_no_firmware_or_service_changes(self):
        before = {p.relative_to(self.reference).as_posix(): p.read_bytes()
                  for p in self.reference.rglob('*') if p.is_file()}
        report = self.prepare()
        self.assertEqual(set(report['files']), set(self.needed))
        self.assertEqual(self.readelf.call_count, len(self.needed), 'cycles must be visited once')
        self.assertIn('libexternal-platform.so', report['platform_dependencies'])
        self.assertTrue(runtime.PLATFORM.issubset(report['platform_dependencies']))
        for name in self.needed:
            self.assertEqual((self.output / 'lib64' / name).read_bytes(),
                             (self.reference / 'vendor/lib64' / name).read_bytes())
        exported = {p.relative_to(self.output).as_posix() for p in self.output.rglob('*') if p.is_file()}
        self.assertEqual(exported, {'lib64/' + n for n in self.needed} |
                         {'quest2-omx-probe', 'run.sh', 'MANIFEST.json', 'SHA256SUMS'})
        for field in ('firmware_replaced', 'system_service_replaced', 'gpu_backend_changed'):
            self.assertFalse(report[field])
        self.assertTrue(report['private_only'])
        self.assertEqual(report['status'], 'staged-not-decoding-verified')
        self.assertEqual(report['probe_sha256'], runtime.digest(self.probe))
        self.assertEqual(before, {p.relative_to(self.reference).as_posix(): p.read_bytes()
                                 for p in self.reference.rglob('*') if p.is_file()})
        for line in (self.output / 'SHA256SUMS').read_text().splitlines():
            sha, name = line.split('  ', 1)
            self.assertEqual(sha, runtime.digest(self.output / name))

    def test_tampered_root_or_dependency_rejected_before_staging(self):
        for name in ('libOmxCore.so', 'libsupport.so'):
            with self.subTest(name=name):
                path = self.reference / 'vendor/lib64' / name
                original = path.read_bytes()
                path.write_bytes(original + b'tamper')
                with self.assertRaisesRegex(ValueError, 'inventory mismatch'):
                    self.prepare()
                self.assert_not_written()
                path.write_bytes(original)

    def test_inventory_rehash_cannot_override_pinned_root(self):
        path = self.reference / 'vendor/lib64/libOmxCore.so'
        path.write_bytes(path.read_bytes() + b'rehashed but not pinned')
        self.update_inventory(path.name)
        with self.assertRaisesRegex(ValueError, 'Pinned video file mismatch'):
            self.prepare()
        self.assert_not_written()

    def test_output_must_be_fresh_and_existing_contents_unchanged(self):
        self.output.mkdir()
        marker = self.output / 'operator-data'
        marker.write_bytes(b'preserve')
        with self.assertRaisesRegex(ValueError, 'fresh directory'):
            self.prepare()
        self.assertEqual(marker.read_bytes(), b'preserve')
        self.readelf.assert_not_called()

    def test_cannot_stage_inside_donor(self):
        target = self.reference / 'new-output'
        with self.assertRaisesRegex(ValueError, 'must not modify the donor'):
            self.prepare(target)
        self.assertFalse(target.exists())
        self.readelf.assert_not_called()

    def test_wrong_partition_identity_rejected(self):
        self.inventory['image_sha256'] = '2' * 64
        self.persist()
        with self.assertRaisesRegex(ValueError, 'partition identity'):
            self.prepare()
        self.assert_not_written()
        self.readelf.assert_not_called()

    def test_missing_root_is_not_accepted_as_platform_dependency(self):
        self.inventory['entries'] = [e for e in self.inventory['entries']
                                     if e['path'] != '/lib64/libOmxVdec.so']
        self.persist()
        with self.assertRaisesRegex(ValueError, 'Missing donor decoder'):
            self.prepare()
        self.assert_not_written()

    def test_unsafe_dependency_cannot_escape_library_directory(self):
        self.needed['libOmxVdec.so'] = ['../outside.so']
        with self.assertRaisesRegex(ValueError, 'Unsafe dependency'):
            self.prepare()
        self.assert_not_written()

    def test_probe_wrong_class_or_machine_rejected(self):
        for body in (minimal_elf('x86', machine=62), minimal_elf('32bit', elf_class=1)):
            with self.subTest(body=body):
                self.probe.write_bytes(body)
                with self.assertRaisesRegex(ValueError, 'AArch64 ELF'):
                    self.prepare()
                self.assert_not_written()

    def test_dependency_wrong_architecture_rejected_even_if_inventory_matches(self):
        path = self.reference / 'vendor/lib64/libsupport.so'
        path.write_bytes(minimal_elf('wrong architecture', machine=62))
        self.update_inventory(path.name)
        with self.assertRaisesRegex(ValueError, 'AArch64 ELF'):
            self.prepare()
        self.assert_not_written()


if __name__ == '__main__':
    unittest.main()