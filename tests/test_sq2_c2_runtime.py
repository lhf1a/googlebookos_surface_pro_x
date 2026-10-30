"""Host packaging guards; device playback is recorded separately."""
from pathlib import Path
import importlib.util
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('runtime', ROOT/'scripts/pack-sq2-c2-runtime.py')
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)

class RuntimeTest(unittest.TestCase):
    def test_existing_output_is_never_replaced(self):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp); out=d/'out.tar'; out.write_bytes(b'preserve')
            with self.assertRaisesRegex(ValueError, 'existing'):
                runtime.package(d/'missing-service', d/'missing-mapper', out)
            self.assertEqual(out.read_bytes(), b'preserve')

    def test_unknown_service_never_produces_bundle(self):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp); service=d/'service'; mapper=d/'mapper'; out=d/'out.tar'
            service.write_bytes(b'unknown'); mapper.write_bytes(b'unknown')
            with self.assertRaisesRegex(ValueError, 'unverified runtime: service'):
                runtime.package(service, mapper, out)
            self.assertFalse(out.exists())

    def test_unknown_mapper_never_produces_bundle(self):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp); service=d/'service'; mapper=d/'mapper'; out=d/'out.tar'
            service.write_bytes(b'fixture'); mapper.write_bytes(b'unknown')
            old=runtime.SERVICE
            try:
                runtime.SERVICE=runtime.digest(b'fixture')
                with self.assertRaisesRegex(ValueError, 'unverified runtime: libminigbm'):
                    runtime.package(service, mapper, out)
            finally:
                runtime.SERVICE=old
            self.assertFalse(out.exists())

if __name__ == '__main__':
    unittest.main()
