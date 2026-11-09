"""Parser tests use synthetic records, not firmware evidence."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("end_audit", ROOT / "scripts/audit-sq2-session-end-trace.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class EndTraceTest(unittest.TestCase):
    def fixture(self, directory, prior=1, ret="0x0", confirmed=1, reply_error="0x0"):
        (directory / "end-trace.txt").write_text(
            f" thread-12 [000] ..... 1.000: begin: () inst=0x123 prior={prior} sent=0 confirmed=0 sys_error=0x0\n"
            f" irq-55 [000] ..... 1.001: reply: () inst=0x123 size=16 type=0x20007 reply_error={reply_error} prior={prior} sys_error=0x0\n"
            f" thread-12 [000] ..... 1.002: finish: () retval={ret}\n"
            f" thread-12 [000] ..... 1.003: quarantine: () inst=0x123 error=-5 prior={prior} confirmed={confirmed}\n")
        for name in ["global-trace-before.txt", "global-trace-after.txt"]:
            (directory / name).write_text("unchanged\n")
        (directory / "trace-stats.txt").write_text("overrun: 0\ncommit overrun: 0\ndropped events: 0\n")
        (directory / "runner.exit").write_text("0\n")
        (directory / "runner.txt").write_text("error=0 sys_error=0 hfi_error=0 insts=0 vb2_bytes=0\nEND_TRACE_CLEANUP\n")

    def test_prior_error_successful_end(self):
        with tempfile.TemporaryDirectory() as name:
            d = Path(name)
            self.fixture(d)
            result = audit.audit(d, "retained-error-confirmed")
            self.assertEqual(result["retained_error_END_confirmations"], 1)
            self.assertEqual(result["retained_error_END_rejections"], 0)
            with self.assertRaises(AssertionError):
                audit.audit(d, "retained-error-rejected")

    def test_old_rejection(self):
        with tempfile.TemporaryDirectory() as name:
            d = Path(name)
            self.fixture(d, ret="0xfffffffb", confirmed=0)
            self.assertEqual(audit.audit(d, "retained-error-rejected")["retained_error_END_rejections"], 1)
            with self.assertRaises(AssertionError):
                audit.audit(d, "retained-error-confirmed")

    def test_negative_reply_and_trace_loss_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            d = Path(name)
            self.fixture(d, reply_error="0x1")
            with self.assertRaises(AssertionError):
                audit.audit(d, "retained-error-confirmed")
            self.fixture(d)
            (d / "trace-stats.txt").write_text("overrun: 1\n")
            with self.assertRaises(AssertionError):
                audit.audit(d, "retained-error-confirmed")

    def test_unpaired_reply_rejected(self):
        with tempfile.TemporaryDirectory() as name:
            d = Path(name)
            self.fixture(d)
            p = d / "end-trace.txt"
            p.write_text(p.read_text().replace("reply: () inst=0x123", "reply: () inst=0x456"))
            with self.assertRaises(AssertionError):
                audit.audit(d, "retained-error-confirmed")

    def test_cleared_prior_error_is_not_accepted(self):
        with tempfile.TemporaryDirectory() as name:
            d = Path(name)
            self.fixture(d)
            p = d / "end-trace.txt"
            p.write_text(p.read_text().replace("reply_error=0x0 prior=1", "reply_error=0x0 prior=0"))
            with self.assertRaises(AssertionError):
                audit.audit(d, "retained-error-confirmed")
