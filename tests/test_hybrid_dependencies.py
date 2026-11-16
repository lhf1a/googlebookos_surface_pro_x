import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
spec = importlib.util.spec_from_file_location('deps', ROOT/'scripts/import-hybrid-dependencies.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class DependenciesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def bundle(self, data=None, extra=None, mutation=None):
        data = data or {'surface/windows/sample.bin': b'OEM fixture'}
        rows = [{'path': n, 'bytes': len(b), 'sha256': hashlib.sha256(b).hexdigest()} for n, b in data.items()]
        report = {'schema': 1, 'product': 'hybrid-sq1-sq2-6.18', 'files': rows, 'unresolved': ['fixture']}
        if mutation:
            mutation(report)
        path = self.root/'bundle.tar'
        with tarfile.open(path, 'w') as tar:
            for n, b in {**data, 'MANIFEST.json': json.dumps(report).encode(), **(extra or {})}.items():
                info = tarfile.TarInfo(n)
                info.size = len(b)
                tar.addfile(info, io.BytesIO(b))
        return path

    def run_import(self, p):
        return m.import_bundle(p, self.root/'import', m.digest(p))

    def test_roundtrip(self):
        p = self.bundle()
        self.assertEqual(len(self.run_import(p)['files']), 1)
        self.assertEqual((self.root/'import/surface/windows/sample.bin').read_bytes(), b'OEM fixture')

    def test_wrong_outer_hash(self):
        p = self.bundle()
        with self.assertRaisesRegex(ValueError, 'Package SHA256'):
            m.import_bundle(p, self.root/'import', '0'*64)

    def test_wrong_file_hash(self):
        p = self.bundle(mutation=lambda r: r['files'][0].update(sha256='0'*64))
        with self.assertRaisesRegex(ValueError, 'File hash'):
            self.run_import(p)
        self.assertFalse((self.root/'import').exists())

    def test_extra_member(self):
        p = self.bundle(extra={'rogue': b'x'})
        with self.assertRaisesRegex(ValueError, 'inventory'):
            self.run_import(p)

    def test_traversal(self):
        p = self.bundle({'../outside': b'x'})
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            self.run_import(p)

    def test_symlink(self):
        p = self.bundle()
        with tarfile.open(p, 'a') as tar:
            info = tarfile.TarInfo('escape'); info.type = tarfile.SYMTYPE; info.linkname = '/etc'
            tar.addfile(info)
        with self.assertRaisesRegex(ValueError, 'regular'):
            self.run_import(p)

    def test_duplicate_case(self):
        p = self.bundle({'File': b'x', 'file': b'y'})
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            self.run_import(p)

    def test_pinned_download_not_self_certified(self):
        p = self.bundle({'downloads/mica-recovery.zip': b'not recovery'})
        with self.assertRaisesRegex(ValueError, 'Pinned download'):
            self.run_import(p)

    def test_no_overwrite(self):
        p = self.bundle(); (self.root/'import').mkdir()
        with self.assertRaisesRegex(ValueError, 'fresh'):
            self.run_import(p)

    def test_missing_inputs(self):
        r = m.check_build_inputs(self.root)
        self.assertFalse(r['input_preflight_passed'])
        self.assertIn('camera/CAMERA.json', r['missing'])
        self.assertIn('sensors/sensors-bundle.json', r['missing'])

    def test_runtime_sources_follow_existing_provenance(self):
        spec = json.loads((ROOT/'configs/hybrid-dependencies.json').read_text())
        repos = {x['id']: x for x in spec['repositories']}
        downloads = {x['id']: x for x in spec['downloads']}
        omx = json.loads((ROOT/'configs/sm8250-omx-source.json').read_text())
        donor = json.loads((ROOT/'configs/quest2-full-donor-candidate.json').read_text())
        self.assertEqual(repos['qti-omx-sm8250']['commit'], omx['commit'])
        self.assertEqual(downloads['quest2-v77-donor']['url'], donor['url'])
        self.assertEqual(downloads['quest2-v77-donor']['hash'], donor['archive_sha256_published'])
        self.assertTrue(downloads['quest2-v77-donor']['third_party'])
        self.assertFalse(downloads['quest2-v77-donor']['oem_signature_verified'])
        groups = {x['id']: x for x in spec['runtime_groups']}
        self.assertEqual(groups['qti-source-built-hal']['source_type'], 'public-source-plus-local-patches')
        self.assertIn('qti-omx-sm8250', groups['qti-source-built-hal']['inputs'])
        self.assertEqual(len(groups), 5)

    def test_surface_conflict(self):
        for version in ('one', 'two'):
            p = self.root/'surface/windows/System32/DriverStore/FileRepository'/version/'qcslpi8180.mbn'
            p.parent.mkdir(parents=True); p.write_text(version)
        r = m.prepare_surface(self.root)
        self.assertIn('qcslpi8180.mbn', r['conflicts'])
        self.assertNotIn('firmware/qcom/msft/surface/pro-x/qcslpi8180.mbn', r['files'])

    def test_duplicate_identical_firmware(self):
        for version in ('one', 'two'):
            p = self.root/'surface/windows/System32/DriverStore/FileRepository'/version/'qcslpi8180.mbn'
            p.parent.mkdir(parents=True); p.write_text('same')
        r = m.prepare_surface(self.root)
        self.assertFalse(r['conflicts'])
        self.assertIn('firmware/qcom/msft/surface/pro-x/qcslpi8180.mbn', r['files'])


if __name__ == '__main__':
    unittest.main()
