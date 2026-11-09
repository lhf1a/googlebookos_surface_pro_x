"""Synthetic evidence checks; these do not establish real BT.709 decode/scanout."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("audit", ROOT / "scripts/audit-sq2-bt709-video.py")
audit = importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
class Bt709Audit(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.capture = Path(self.temp.name)
        frames = self.capture / "fake-frames";frames.mkdir()
        self.reference = bytes(115200 * 30)
        for i in range(30):
            (frames / ("sq2-ndk-frame-%d.bin" % i)).write_bytes(bytes(115200))
        (self.capture / "fake.exit").write_text("0\n")
        self.log = "COLOR standard=1 present=1 transfer=3 present=1 range=2 present=1\n"
        self.log += "FORMAT stride: int32(320), slice-height: int32(240), color-format: int32(21)\n"
        self.log += "".join("OUTPUT pts=%d size=115200 offset=0 flags=0 capacity=115200\n" %
                            (i*33333) for i in range(30))
        self.log += "TRANSPORT sent=31 frames=30 eos=1 failed=0 stop=0 delete=0\n"
        (self.capture / "fake.txt").write_text(self.log)
    def test_correct_visible_pixels_and_color(self):
        self.assertTrue(audit.audit_frames(self.capture, "fake", self.reference, False)["pixels_exact"])
    def test_bt601_fallback_and_missing_color_are_refused(self):
        for old, new in [("standard=1 present=1", "standard=4 present=1"),
                         ("standard=1 present=1", "standard=0 present=0")]:
            (self.capture / "fake.txt").write_text(self.log.replace(old, new))
            with self.assertRaisesRegex(ValueError, "metadata"):
                audit.audit_frames(self.capture, "fake", self.reference, False)
    def test_bad_pixels_or_pts_are_refused(self):
        p = self.capture / "fake-frames/sq2-ndk-frame-0.bin"
        p.write_bytes(b"\1" + bytes(115199))
        with self.assertRaisesRegex(ValueError, "pixel"):
            audit.audit_frames(self.capture, "fake", self.reference, False)
        p.write_bytes(bytes(115200))
        (self.capture / "fake.txt").write_text(self.log.replace("pts=33333 ", "pts=33332 "))
        with self.assertRaisesRegex(ValueError, "PTS"):
            audit.audit_frames(self.capture, "fake", self.reference, False)
