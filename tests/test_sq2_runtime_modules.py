import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("modcopy", ROOT / "scripts/prepare-sq2-runtime-modules.py")
MOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)


class ModuleCopyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.src = self.root / "source"
        self.src.mkdir()
        (self.src / "example.ko").write_bytes(b"synthetic-not-module\0")
        (self.src / "modules.dep").write_text("example.ko:\n")
        (self.src / "build").symlink_to("/outside-never-follow", target_is_directory=True)

    def tearDown(self):
        self.tmp.cleanup()

    def test_known_links_and_regular_cache_only(self):
        (self.src / "source").symlink_to("/outside-source-never-follow", target_is_directory=True)
        for n in MOD.CACHES:
            (self.src / n).write_bytes(b"\0cache\xff")
        before = MOD.SNAP.snapshot(self.src)
        out = self.root / "output"
        result = MOD.prepare(self.src, out)
        self.assertEqual(MOD.SNAP.snapshot(self.src), before)
        self.assertEqual(result["modules_unchanged"], 1)
        self.assertEqual(set(result["removed"]), MOD.LINKS | MOD.CACHES)
        self.assertEqual((self.src / "example.ko").read_bytes(), (out / "example.ko").read_bytes())

    def test_unknown_link_rejected(self):
        (self.src / "module.ko").symlink_to("example.ko")
        with self.assertRaises(ValueError):
            MOD.prepare(self.src, self.root / "output")

    def test_cache_link_rejected(self):
        (self.src / "modules.dep.bb").symlink_to("example.ko")
        with self.assertRaises(ValueError):
            MOD.prepare(self.src, self.root / "output")

    def test_cache_directory_rejected(self):
        (self.src / "modules.dep.bb").mkdir()
        with self.assertRaises(ValueError):
            MOD.prepare(self.src, self.root / "output")

    def test_build_file_rejected(self):
        (self.src / "build").unlink()
        (self.src / "build").write_text("not a symlink")
        with self.assertRaises(ValueError):
            MOD.prepare(self.src, self.root / "output")

    def test_nested_output_rejected(self):
        with self.assertRaises(ValueError):
            MOD.prepare(self.src, self.src / "output")

    def test_existing_output_rejected(self):
        out = self.root / "output"
        out.mkdir()
        with self.assertRaises(ValueError):
            MOD.prepare(self.src, out)


if __name__ == "__main__":
    unittest.main()
