import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("provider", ROOT / "scripts/prepare-kgsl-smmu-provider.py")
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)


class ProviderPrepareTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "sample.c").write_text("before\n")
        self.patch = self.root / "change.patch"
        self.patch.write_text("--- a/sample.c\n+++ b/sample.c\n@@ -1 +1 @@\n-before\n+after\n")
        self.config = self.root / "config.json"
        self.pins = {"status": "test", "patch": str(self.patch),
                     "sha256": provider.sha(self.patch),
                     "files": {"sample.c": {"before": hashlib.sha256(b"before\n").hexdigest(),
                                             "after": hashlib.sha256(b"after\n").hexdigest()}}}

    def run_prepare(self):
        self.config.write_text(json.dumps(self.pins))
        return provider.prepare(self.source, self.root / "output", self.config)

    def test_explicit_config(self):
        self.assertEqual(self.run_prepare(), self.pins)
        self.assertEqual((self.source / "sample.c").read_text(), "before\n")
        self.assertEqual((self.root / "output/sample.c").read_text(), "after\n")
        self.assertTrue((self.root / "output/SOURCE.json").exists())

    def test_bad_patch(self):
        self.patch.write_text("bad")
        with self.assertRaises(ValueError): self.run_prepare()
        self.assertFalse((self.root / "output").exists())

    def test_bad_source(self):
        (self.source / "sample.c").write_text("modified")
        with self.assertRaises(ValueError): self.run_prepare()
        self.assertFalse((self.root / "output").exists())

    def test_escape_path(self):
        hashes = self.pins["files"]["sample.c"]
        for name in ("../outside", str(self.source / "sample.c")):
            self.pins["files"] = {name: hashes}
            with self.assertRaises(ValueError): self.run_prepare()
        self.assertFalse((self.root / "output").exists())

    def test_existing_output(self):
        (self.root / "output").mkdir()
        (self.root / "output/keep").write_text("keep")
        with self.assertRaises(FileExistsError): self.run_prepare()
        self.assertEqual((self.root / "output/keep").read_text(), "keep")

    def add_new_file(self):
        self.patch.write_text(self.patch.read_text() + "--- /dev/null\n+++ b/include/new.h\n@@ -0,0 +1 @@\n+new\n")
        self.pins["sha256"] = provider.sha(self.patch)
        self.pins["files"]["include/new.h"] = {"before": None, "after": hashlib.sha256(b"new\n").hexdigest()}

    def test_new_file(self):
        self.add_new_file()
        self.run_prepare()
        self.assertEqual((self.root / "output/include/new.h").read_text(), "new\n")
        self.assertFalse((self.source / "include/new.h").exists())

    def test_new_file_does_not_overwrite(self):
        self.add_new_file()
        (self.source / "include").mkdir()
        (self.source / "include/new.h").write_text("keep")
        with self.assertRaises(ValueError): self.run_prepare()
        self.assertEqual((self.source / "include/new.h").read_text(), "keep")
        self.assertFalse((self.root / "output").exists())

    def test_new_file_rejects_symlink(self):
        self.add_new_file()
        (self.source / "include").mkdir()
        (self.source / "include/new.h").symlink_to(self.root / "missing")
        with self.assertRaises(ValueError): self.run_prepare()
        self.assertFalse((self.root / "output").exists())

    def test_new_file_rejects_parent_escape(self):
        self.add_new_file()
        (self.source / "include").symlink_to(self.root)
        with self.assertRaises(ValueError): self.run_prepare()
        self.assertFalse((self.root / "output").exists())
