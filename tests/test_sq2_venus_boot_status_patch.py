"""The incremental boot patch must reject unknown inputs before writing."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("bootstatus", ROOT / "scripts/patch-sq2-venus-boot-status.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
FIXTURE = (
    "untouched prefix\nstatic int venus_boot_core(void) {\n"
    "max_tries = 100;\n"
    "while (!ctrl_status && count < max_tries)\n"
    "if ((ctrl_status & CPU_CS_SCIACMDARG0_ERROR_STATUS_MASK) == 4)\n"
    + m.OLD + "\n}\nstatic u32 venus_hwversion(void) {}\nuntouched suffix\n"
)


class BootStatusPatch(unittest.TestCase):
    def prepare(self, directory):
        (directory / "hfi_venus.c").write_text(FIXTURE)

    def test_exact_change_and_reapply(self):
        with tempfile.TemporaryDirectory() as td:
            directory = Path(td)
            self.prepare(directory)
            m.apply(directory)
            expected = FIXTURE.replace(m.OLD, m.NEW)
            self.assertEqual((directory / "hfi_venus.c").read_text(), expected)
            m.apply(directory)
            self.assertEqual((directory / "hfi_venus.c").read_text(), expected)

    def test_unknown_poll_bound_rejected_before_writes(self):
        with tempfile.TemporaryDirectory() as td:
            directory = Path(td)
            self.prepare(directory)
            path = directory / "hfi_venus.c"
            path.write_text(FIXTURE.replace("max_tries = 100;", "max_tries = 101;"))
            before = path.read_bytes()
            with self.assertRaises(ValueError):
                m.apply(directory)
            self.assertEqual(path.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
