"""Mutation tests use a captured ownership run; they do not simulate hardware."""
from pathlib import Path
import importlib.util
import unittest
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("ownership", ROOT / "scripts/audit-sq2-composer-ownership.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
CAPTURE = ROOT / "out/qualcomm-full-stack-20261108/composer-hal-ownership-v5/capture/probe.txt"

@unittest.skipUnless(CAPTURE.exists(), "requires local private diagnostic capture")
class OwnershipAudit(unittest.TestCase):
    def setUp(self):
        self.text = CAPTURE.read_text()
    def test_baseline(self):
        self.assertEqual(module.audit(self.text, False)["safe_cached_reuses"], 27)
    def test_missing_frame(self):
        text = "\n".join(x for x in self.text.splitlines() if not x.startswith("FRAME_PRESENT frame=17 "))
        with self.assertRaisesRegex(ValueError, "presents"):
            module.audit(text, False)
    def test_wrong_prior_frame(self):
        text = self.text.replace("presented=2 released=1", "presented=2 released=2", 1)
        with self.assertRaisesRegex(ValueError, "associations"):
            module.audit(text, False)
    def test_fence_error(self):
        text = self.text.replace("status=0 points=1", "status=-5 points=1", 1)
        with self.assertRaisesRegex(ValueError, "error"):
            module.audit(text, False)
    def test_early_write(self):
        lines = self.text.splitlines()
        for i, line in enumerate(lines):
            if line.startswith("BUFFER_WRITE frame=3 "):
                lines[i] = line[:line.index("time_ns=")] + "time_ns=1"
                break
        with self.assertRaisesRegex(ValueError, "preceded"):
            module.audit("\n".join(lines), False)
    def test_duplicate_buffer(self):
        lines = self.text.splitlines()
        first = next(x for x in module.records(self.text) if x[0] == "BUFFER_ID")[1]["id"]
        for i, line in enumerate(lines):
            if line.startswith("BUFFER_ID slot=1 "):
                fields = line.split()
                fields[2] = "id=" + str(first)
                lines[i] = " ".join(fields)
                break
        with self.assertRaisesRegex(ValueError, "distinct"):
            module.audit("\n".join(lines), False)


FIXED = ROOT / "out/qualcomm-full-stack-20261108/composer-hal-ownership-v8/capture/probe.txt"

@unittest.skipUnless(FIXED.exists(), "requires local fixed-service capture")
class FixedOwnershipAudit(unittest.TestCase):
    def test_fixed_matrix_and_recovery(self):
        self.assertEqual(module.audit(FIXED.read_text())["malformed_matrix_cases"], 12)
    def test_missing_protocol_case(self):
        text = "\n".join(x for x in FIXED.read_text().splitlines()
                         if not x.startswith("MALFORMED_RESPONSE version=22 scenario=2 "))
        with self.assertRaisesRegex(ValueError, "matrix"):
            module.audit(text)
    def test_missing_recovery_fence(self):
        text = "\n".join(x for x in FIXED.read_text().splitlines()
                         if not x.startswith("FENCE kind=recovery frame=22 slot=2 phase=after "))
        with self.assertRaisesRegex(ValueError, "recovery present"):
            module.audit(text)

if __name__ == "__main__":
    unittest.main()
