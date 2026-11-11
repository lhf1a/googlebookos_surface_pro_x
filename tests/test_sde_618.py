import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("sde_prepare", ROOT / "scripts/prepare-sde-618.py")
sde = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sde)


class PrepareTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source, self.output = self.root / "donor", self.root / "prepared"
        self.names = {
            "techpack/display/msm/Makefile": b"obj-$(CONFIG_DISPLAY_BUILD) += msm_drm.o\n",
            "techpack/display/msm/msm_drv.c": b"driver\n",
            "techpack/display/msm/sde/sde_hw_catalog.c": b"catalog\n",
            "include/uapi/drm/msm_drm.h": b"uapi\n",
            "drivers/not-copied.c": b"unrelated\n",
        }
        for name, data in self.names.items():
            p = self.source / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        (self.root / "configs").mkdir()
        self.manifest = {"commit": "fixture", "sha256": {
            n: hashlib.sha256(b).hexdigest() for n, b in self.names.items()}}
        (self.root / "configs/donor.json").write_text(json.dumps(self.manifest))
        self.config = self.root / "configs/port.json"
        self.pins = dict(source_manifest="donor.json", source_prefix="techpack/display/",
                         additional_sources={"include/uapi/drm/msm_drm.h": self.manifest["sha256"]["include/uapi/drm/msm_drm.h"]},
                         status="compile-blocked-not-device-ready", patches=[], open=["fixture"])
        self.config.write_text(json.dumps(self.pins))
        mock = patch.object(sde, "ROOT", self.root)
        mock.start()
        self.addCleanup(mock.stop)

    def run_prepare(self):
        return sde.prepare(self.source, self.output, self.config)

    def test_exact_sources_and_explicit_donor_uapi(self):
        result = self.run_prepare()
        self.assertEqual(len(result["sha256"]), 4)
        self.assertFalse((self.output / "drivers/not-copied.c").exists())
        make = (self.output / "techpack/display/msm/Kbuild").read_text()
        self.assertIn("LINUXINCLUDE := -I$(src)/../../../include/uapi", make)
        self.assertNotIn("KBUILD_MODPOST_WARN", make)
        for n, data in self.names.items():
            self.assertEqual((self.source / n).read_bytes(), data)

    def test_changed_source_rejected_before_output(self):
        (self.source / "techpack/display/msm/msm_drv.c").write_text("changed")
        with self.assertRaisesRegex(ValueError, "Unrecognized source"):
            self.run_prepare()
        self.assertFalse(self.output.exists())

    def test_missing_uapi_rejected_before_output(self):
        (self.source / "include/uapi/drm/msm_drm.h").unlink()
        with self.assertRaises(FileNotFoundError):
            self.run_prepare()
        self.assertFalse(self.output.exists())

    def test_existing_output_preserved(self):
        self.output.mkdir()
        marker = self.output / "owned"
        marker.write_text("keep")
        with self.assertRaisesRegex(ValueError, "fresh"):
            self.run_prepare()
        self.assertEqual(marker.read_text(), "keep")

    def test_symlink_rejected(self):
        p = self.source / "techpack/display/msm/msm_drv.c"
        target = self.root / "outside"
        target.write_bytes(p.read_bytes())
        p.unlink()
        p.symlink_to(target)
        with self.assertRaisesRegex(ValueError, "Unsafe source"):
            self.run_prepare()

    def test_parent_escape_rejected(self):
        self.pins["additional_sources"]["../outside"] = "unused"
        self.config.write_text(json.dumps(self.pins))
        with self.assertRaisesRegex(ValueError, "Unsafe source path"):
            self.run_prepare()
        self.assertFalse(self.output.exists())

    def test_incomplete_core_rejected_before_output(self):
        del self.manifest["sha256"]["techpack/display/msm/msm_drv.c"]
        (self.root / "configs/donor.json").write_text(json.dumps(self.manifest))
        with self.assertRaisesRegex(ValueError, "Incomplete display source"):
            self.run_prepare()
        self.assertFalse(self.output.exists())

    def test_added_source_is_pinned_and_reported(self):
        name = "techpack/display/msm/dp/native_link.c"
        data = b"new helper\n"
        content = "--- /dev/null\n+++ b/" + name + "\n@@ -0,0 +1 @@\n+new helper\n"
        p = self.root / "addition.patch"
        p.write_text(content)
        self.pins["patches"] = [dict(path="addition.patch", sha256=sde.sha(p), files={
            name: dict(before=None, after=hashlib.sha256(data).hexdigest())})]
        self.config.write_text(json.dumps(self.pins))
        report = self.run_prepare()
        self.assertEqual((self.output / name).read_bytes(), data)
        self.assertEqual(report["sha256"][name], hashlib.sha256(data).hexdigest())
        self.assertFalse((self.source / name).exists())

    def test_added_source_cannot_replace_original(self):
        name = "techpack/display/msm/msm_drv.c"
        p = self.root / "addition.patch"
        p.write_text("unused")
        self.pins["patches"] = [dict(path="addition.patch", sha256=sde.sha(p), files={
            name: dict(before=None, after="unused")})]
        self.config.write_text(json.dumps(self.pins))
        with self.assertRaisesRegex(ValueError, "Unexpected added source"):
            self.run_prepare()
        self.assertEqual((self.source / name).read_bytes(), b"driver\n")


if __name__ == "__main__":
    unittest.main()
