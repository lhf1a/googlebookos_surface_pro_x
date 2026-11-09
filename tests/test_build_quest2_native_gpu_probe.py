import importlib.util
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("native_gpu_builder", ROOT / "scripts/build-quest2-native-gpu-probe.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

class NativeGpuInputsTest(unittest.TestCase):
    def test_runtime_bytes_and_manifest(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "lib.so").write_bytes(b"reference")
            sums = (hashlib.sha256(b"reference").hexdigest() + "  lib.so\n").encode()
            (root / "SHA256SUMS").write_bytes(sums)
            with mock.patch.object(builder, "RUNTIME_SUMS_SHA", hashlib.sha256(sums).hexdigest()):
                builder.verify_runtime(root)
                (root / "lib.so").write_bytes(b"modified")
                with self.assertRaisesRegex(ValueError, "input mismatch"): builder.verify_runtime(root)
                (root / "SHA256SUMS").write_text("unknown manifest")
                with self.assertRaisesRegex(ValueError, "verified"): builder.verify_runtime(root)

    def test_runtime_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "actual").write_bytes(b"reference")
            (root / "lib.so").symlink_to("actual")
            sums = (hashlib.sha256(b"reference").hexdigest() + "  lib.so\n").encode()
            (root / "SHA256SUMS").write_bytes(sums)
            with mock.patch.object(builder, "RUNTIME_SUMS_SHA", hashlib.sha256(sums).hexdigest()):
                with self.assertRaisesRegex(ValueError, "input mismatch"): builder.verify_runtime(root)
