import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("allocator_builder",ROOT/"scripts/build-quest2-allocator.py")
builder=importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

class AllocatorInputs(unittest.TestCase):
    def test_native_parent_hash_and_path_boundaries(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/"lib.so").write_bytes(b"fixture")
            digest=hashlib.sha256(b"fixture").hexdigest()
            manifest={"sha256":{"lib.so":digest}}
            (p/"BUILD.json").write_text(json.dumps(manifest))
            self.assertEqual(builder.verify_candidate(p),manifest)
            (p/"lib.so").write_bytes(b"changed")
            with self.assertRaises(ValueError):builder.verify_candidate(p)
            manifest["sha256"]={"../outside.so":digest}
            (p/"BUILD.json").write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):builder.verify_candidate(p)
            (p/"link.so").symlink_to(p/"lib.so")
            manifest["sha256"]={"link.so":builder.sha(p/"lib.so")}
            (p/"BUILD.json").write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):builder.verify_candidate(p)

if __name__ == "__main__":
    unittest.main()
