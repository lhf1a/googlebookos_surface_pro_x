import importlib.util
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("heap_build", ROOT / "scripts/build-system-uncached-heap.py")
heap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(heap)

class HeapBuildTest(unittest.TestCase):
    def test_unknown_source_rejected_before_creating_candidate(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            source = root / "source.c"
            source.write_text("unreviewed exporter")
            with self.assertRaisesRegex(ValueError, "Unrecognized"):
                heap.prepare(source, root / "candidate")
            self.assertFalse((root / "candidate").exists())

class MemoryPackageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("memory_pack", ROOT / "scripts/pack-sq2-memory-backend.py")
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)

    def test_mixed_backend_and_missing_exporter_rejected(self):
        validate = self.module.pack.validate_memory_pair
        marker = self.module.MARKER
        validate(b"old-ion", b"old-bridge", b"")
        validate(marker, marker, b"q2_memory_backend=" + marker)
        for pieces in [(marker, b"old", marker), (b"old", marker, marker), (marker, marker, b"")]:
            with self.subTest(pieces=pieces), self.assertRaisesRegex(ValueError, "paired"):
                validate(*pieces)

    def test_checksum_change_keeps_other_records_and_rejects_stale_parent(self):
        old = "a" * 64
        other = "b" * 64
        data = f"{old}  lib/ion.so\n{other}  unrelated.so\n".encode()
        before = {"root/lib/ion.so": (None, None, old)}
        delta = {"root/lib/ion.so": b"new library"}
        result = self.module.update_sums(data, "root/", before, delta)
        self.assertEqual(result, f"{self.module.pack.digest(b'new library')}  lib/ion.so\n{other}  unrelated.so\n".encode())
        with self.assertRaisesRegex(ValueError, "Stale"):
            self.module.update_sums(data.replace(old.encode(), other.encode(), 1), "root/", before, delta)

class DonorSourceTest(unittest.TestCase):
    def test_bad_archive_does_not_create_output(self):
        spec = importlib.util.spec_from_file_location("donor", ROOT / "scripts/prepare-kgsl-sde-source.py")
        donor = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(donor)
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            archive = root / "source.tar.gz"
            archive.write_bytes(b"unverified archive")
            with self.assertRaisesRegex(ValueError, "archive mismatch"):
                donor.prepare(archive, root / "output")
            self.assertFalse((root / "output").exists())
