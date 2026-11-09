"""Source mutations must be rejected before building the QTI display library set."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("display_build", ROOT / "scripts/build-sm8350-display.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class BuildInputs(unittest.TestCase):
    def test_changed_or_escaping_input_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "source.cpp"
            path.write_bytes(b"source")
            digest = hashlib.sha256(b"source").hexdigest()
            self.assertEqual(module.verify(root, path.name, digest), path)
            path.write_bytes(b"changed")
            for relative in (path.name, "../escape", "/absolute"):
                with self.assertRaises(ValueError):
                    module.verify(root, relative, digest)
            (root / "alias").symlink_to(path)
            with self.assertRaises(ValueError):
                module.verify(root, "alias", digest)

    def test_nonliteral_or_ambiguous_module_sources_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bp = root / "Android.bp"
            bp.write_text('srcs: ["one.cpp", "drm/two.cpp"],')
            self.assertEqual(module.sources(root, "."), [root / "one.cpp", root / "drm/two.cpp"])
            for text in ('srcs: ["*.cpp"],', 'srcs: ["../escape.cpp"],',
                         'srcs: ["one.cpp"], srcs: ["two.cpp"],'):
                bp.write_text(text)
                with self.assertRaises(ValueError):
                    module.sources(root, ".")

if __name__ == "__main__":
    unittest.main()
