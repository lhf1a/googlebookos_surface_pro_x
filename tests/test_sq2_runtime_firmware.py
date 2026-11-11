import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("fwcopy", ROOT / "scripts/prepare-sq2-runtime-firmware.py")
FW = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FW)


class FirmwareCopyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.src = self.root / "source"
        (self.src / "qca").mkdir(parents=True)
        for alias, target in FW.ALIASES.items():
            p = self.src / alias
            (p.parent / target).write_bytes(b"synthetic-not-firmware\0" + target.encode())
            p.symlink_to(target)

    def tearDown(self):
        self.tmp.cleanup()

    def test_alias_bytes_not_version_change(self):
        before = FW.snapshot(self.src)
        out = self.root / "output"
        r = FW.prepare(self.src, out)
        self.assertEqual(FW.snapshot(self.src), before)
        self.assertFalse(r["firmware_version_changed"])
        for alias, target in FW.ALIASES.items():
            self.assertFalse((out / alias).is_symlink())
            self.assertEqual((out / alias).read_bytes(), (self.src / Path(alias).parent / target).read_bytes())

    def test_unknown_alias_rejected_before_output(self):
        (self.src / "qca/unknown").symlink_to("crbtfw21.tlv")
        out = self.root / "output"
        with self.assertRaises(ValueError):
            FW.prepare(self.src, out)
        self.assertFalse(out.exists())

    def test_outside_alias_rejected(self):
        p = self.src / "qca/crbtfw01.tlv"
        p.unlink()
        p.symlink_to("../../outside")
        with self.assertRaises(ValueError):
            FW.prepare(self.src, self.root / "output")

    def test_directory_target_rejected(self):
        p = self.src / "qca/crbtfw21.tlv"
        p.unlink()
        p.mkdir()
        with self.assertRaises(ValueError):
            FW.prepare(self.src, self.root / "output")

    def test_nested_output_rejected(self):
        with self.assertRaises(ValueError):
            FW.prepare(self.src, self.src / "nested-output")

    def test_existing_output_rejected(self):
        out = self.root / "output"
        out.mkdir()
        with self.assertRaises(ValueError):
            FW.prepare(self.src, out)

    def test_broken_output_link_rejected(self):
        out = self.root / "output"
        out.symlink_to(self.root / "missing")
        with self.assertRaises(ValueError):
            FW.prepare(self.src, out)

    def test_source_root_alias_rejected(self):
        alias = self.root / "source-alias"
        alias.symlink_to(self.src, target_is_directory=True)
        with self.assertRaises(ValueError):
            FW.prepare(alias, self.root / "output")


if __name__ == "__main__":
    unittest.main()
