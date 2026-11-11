"""Pinned post-generation source increments; no kernel/device operations."""
import difflib
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = Path(os.environ.get("BOOT_BUILDER_SCRIPT", ROOT / "scripts/prepare-kgsl-sde-boot-flow.py"))
SPEC = importlib.util.spec_from_file_location("source_builder", SCRIPT)
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)
digest = lambda data: hashlib.sha256(data.encode()).hexdigest()


class PostPreludePatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.output = self.base / "output"
        self.output.mkdir()
        self.original = "#!/bin/sh\necho ORIGINAL\n"
        self.changed = "#!/bin/sh\necho COMPOSED\n"
        (self.output / "init").write_text(self.original)
        self.item = {"path": str(self.base / "increment.patch"),
                     "before": {"init": digest(self.original)},
                     "after": {"init": digest(self.changed)}}
        self.patch(self.original, self.changed)
        self.ledger = []

    def patch(self, before, after, name="init", append=False):
        p = Path(self.item["path"])
        data = "".join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                         fromfile="a/" + name, tofile="b/" + name))
        p.write_text((p.read_text() if append else "") + data)

    def apply(self):
        BUILDER.apply_post_prelude_patches(self.output, [self.item], self.ledger)

    def test_exact_parent_and_result(self):
        self.apply()
        self.assertEqual((self.output / "init").read_text(), self.changed)
        self.assertEqual([row["exit"] for row in self.ledger], [0, 0])
        self.assertEqual(self.ledger[0]["stdout"], "patching file init\n")
        self.assertEqual(self.ledger[0]["stderr"], "")

    def test_parent_mismatch_before_write(self):
        self.item["before"]["init"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "parent mismatch"):
            self.apply()
        self.assertEqual((self.output / "init").read_text(), self.original)
        self.assertFalse(self.ledger)

    def test_result_mismatch_is_not_accepted(self):
        self.item["after"]["init"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "wrong bytes"):
            self.apply()

    def test_undeclared_extra_file_is_not_accepted(self):
        (self.output / "other").write_text("original\n")
        self.patch("original\n", "changed\n", "other", append=True)
        with self.assertRaisesRegex(ValueError, "undeclared files"):
            self.apply()

    def test_unsafe_path_before_write(self):
        self.item["before"] = {"../init": digest(self.original)}
        self.item["after"] = {"../init": digest(self.changed)}
        with self.assertRaisesRegex(ValueError, "Invalid increment path"):
            self.apply()
        self.assertEqual((self.output / "init").read_text(), self.original)

    def test_mismatched_file_sets_before_write(self):
        self.item["after"] = {"other": digest(self.changed)}
        with self.assertRaisesRegex(ValueError, "matching before/after"):
            self.apply()
        self.assertFalse(self.ledger)

    def test_patch_failure_preserves_literal_ledger(self):
        self.patch("not the parent\n", self.changed)
        with self.assertRaisesRegex(RuntimeError, "patch failed"):
            self.apply()
        self.assertNotEqual(self.ledger[0]["exit"], 0)
        self.assertIn("FAILED", self.ledger[0]["stdout"])
        self.assertEqual(json.loads((self.output / "BUILD-LEDGER.json").read_text()), self.ledger)

    def test_shell_syntax_failure_is_not_accepted(self):
        invalid = "#!/bin/sh\nif then\n"
        self.patch(self.original, invalid)
        self.item["after"]["init"] = digest(invalid)
        with self.assertRaisesRegex(RuntimeError, "shell syntax failed"):
            self.apply()
        self.assertEqual(self.ledger[-1]["exit"], 2)
        self.assertTrue(self.ledger[-1]["stderr"])

    def test_symlink_source_rejected_before_write(self):
        (self.output / "link").symlink_to("init")
        with self.assertRaisesRegex(ValueError, "source symlink"):
            self.apply()
        self.assertFalse(self.ledger)

    def test_no_increment_is_noop(self):
        BUILDER.apply_post_prelude_patches(self.output, [], self.ledger)
        self.assertEqual((self.output / "init").read_text(), self.original)
        self.assertFalse(self.ledger)


if __name__ == "__main__":
    unittest.main()
