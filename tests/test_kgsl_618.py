import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("kgsl_prepare", ROOT / "scripts/prepare-kgsl-618.py")
kgsl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kgsl)


def digest(data):
    return hashlib.sha256(data).hexdigest()


class PrepareTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "donor"
        self.output = self.root / "candidate"
        self.name = "drivers/gpu/msm/kgsl.c"
        self.header = "include/uapi/linux/msm_kgsl.h"
        for name, data in [(self.name, b"old\n"), (self.header, b"ABI\n"),
                           ("drivers/gpu/msm/stale.o", b"stale object")]:
            p = self.source / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        (self.root / "configs").mkdir()
        (self.root / "patches").mkdir()
        self.patch = self.root / "patches/import.patch"
        self.patch.write_text("--- a/" + self.name + "\n+++ b/" + self.name +
                              "\n@@ -1 +1 @@\n-old\n+new\n")
        self.config = dict(source_manifest="donor.json", status="partial-source-port-not-buildable",
                           patches=[dict(path="patches/import.patch", sha256=digest(self.patch.read_bytes()),
                                         files={self.name: dict(before=digest(b"old\n"), after=digest(b"new\n"))})])
        (self.root / "configs/donor.json").write_text(json.dumps(dict(commit="fixture", sha256={
            self.name: digest(b"old\n"), self.header: digest(b"ABI\n")})))
        self.write_config()
        mock = patch.object(kgsl, "ROOT", self.root)
        mock.start()
        self.addCleanup(mock.stop)

    def write_config(self):
        (self.root / "configs/kgsl-618-port.json").write_text(json.dumps(self.config))

    def test_success_keeps_donor_and_uapi_excludes_build_outputs(self):
        report = kgsl.prepare(self.source, self.output)
        self.assertEqual((self.source / self.name).read_bytes(), b"old\n")
        self.assertEqual((self.output / self.name).read_bytes(), b"new\n")
        self.assertEqual((self.output / self.header).read_bytes(), b"ABI\n")
        self.assertFalse((self.output / "drivers/gpu/msm/stale.o").exists())
        self.assertEqual(report["sha256"][self.name], digest(b"new\n"))
        self.assertEqual(report["status"], "partial-source-port-not-buildable")
        with self.assertRaisesRegex(ValueError, "fresh"):
            kgsl.prepare(self.source, self.output)

    def test_explicit_candidate_config_does_not_change_default(self):
        alternate = self.root / "configs/alternate.json"
        alternate.write_text(json.dumps(dict(self.config, patches=[])))
        report = kgsl.prepare(self.source, self.output, alternate)
        self.assertEqual((self.output / self.name).read_bytes(), b"old\n")
        self.assertEqual(report["patches"], [])
        report = kgsl.prepare(self.source, self.root / "default")
        self.assertEqual((self.root / "default" / self.name).read_bytes(), b"new\n")
        self.assertEqual(len(report["patches"]), 1)

    def test_added_source_is_pinned_and_reported(self):
        name = "drivers/gpu/msm/native.c"
        self.patch.write_text(self.patch.read_text() +
                              "--- /dev/null\n+++ b/" + name + "\n@@ -0,0 +1 @@\n+native\n")
        self.config["patches"][0]["sha256"] = digest(self.patch.read_bytes())
        self.config["patches"][0]["files"][name] = dict(before=None, after=digest(b"native\n"))
        self.write_config()
        report = kgsl.prepare(self.source, self.output)
        self.assertEqual(report["sha256"][name], digest(b"native\n"))
        self.assertFalse((self.source / name).exists())

    def test_added_source_cannot_replace_existing(self):
        self.config["patches"][0]["files"][self.name]["before"] = None
        self.write_config()
        with self.assertRaisesRegex(ValueError, "already exists"):
            kgsl.prepare(self.source, self.output)
        self.assertEqual((self.output / self.name).read_bytes(), b"old\n")

    def test_added_source_path_cannot_escape(self):
        self.config["patches"][0]["files"]["drivers/gpu/msm/../../outside"] = dict(before=None, after="0" * 64)
        self.write_config()
        with self.assertRaisesRegex(ValueError, "added source path"):
            kgsl.prepare(self.source, self.output)
        self.assertFalse((self.output / "SOURCE.json").exists())

    def test_modified_source_rejected_before_output(self):
        (self.source / self.name).write_bytes(b"unrecognized")
        with self.assertRaisesRegex(ValueError, "donor"):
            kgsl.prepare(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_symlink_rejected_before_output(self):
        path = self.source / self.header
        path.unlink()
        target = self.root / "outside"
        target.write_bytes(b"ABI\n")
        path.symlink_to(target)
        with self.assertRaisesRegex(ValueError, "donor"):
            kgsl.prepare(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_modified_patch_rejected_before_output(self):
        self.patch.write_text("unexpected")
        with self.assertRaisesRegex(ValueError, "patch"):
            kgsl.prepare(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_post_patch_mismatch_never_publishes_manifest(self):
        self.config["patches"][0]["files"][self.name]["after"] = "0" * 64
        self.write_config()
        with self.assertRaisesRegex(ValueError, "patched source"):
            kgsl.prepare(self.source, self.output)
        self.assertFalse((self.output / "SOURCE.json").exists())


if __name__ == "__main__":
    unittest.main()
