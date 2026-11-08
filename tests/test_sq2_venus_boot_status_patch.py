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
    "max_tries = sq2_boot_watch && IS_SQ2_HFI(hdev->core) ? 2000 : 100;\n"
    "while (!ctrl_status && count < max_tries)\n"
    "sq2_boot_snapshot(hdev, ctrl_status, count);\n"
    "if ((ctrl_status & CPU_CS_SCIACMDARG0_ERROR_STATUS_MASK) == 4)\n"
    + m.OLD + "\n}\nstatic u32 venus_hwversion(void) {}\nuntouched suffix\n"
)


class BootStatusPatch(unittest.TestCase):
    def prepare(self, directory):
        (directory / "hfi_venus.c").write_text(FIXTURE)
        header = (ROOT / "probe/video/venus-sq2-boot-watch.h").read_bytes()
        (directory / "venus-sq2-boot-watch.h").write_bytes(header)

    def test_exact_change_and_reapply(self):
        with tempfile.TemporaryDirectory() as td:
            directory = Path(td)
            self.prepare(directory)
            m.apply(directory)
            expected = FIXTURE.replace(m.OLD, m.NEW)
            self.assertEqual((directory / "hfi_venus.c").read_text(), expected)
            m.apply(directory)
            self.assertEqual((directory / "hfi_venus.c").read_text(), expected)

    def test_unknown_header_or_poll_bound_rejected_before_writes(self):
        for part in ("header", "loop"):
            with self.subTest(part=part), tempfile.TemporaryDirectory() as td:
                directory = Path(td)
                self.prepare(directory)
                if part == "header":
                    (directory / "venus-sq2-boot-watch.h").write_text("local changes")
                else:
                    (directory / "hfi_venus.c").write_text(FIXTURE.replace("? 2000 : 100", "? 2000 : 101"))
                before = {p.name: p.read_bytes() for p in directory.iterdir()}
                with self.assertRaises(ValueError):
                    m.apply(directory)
                self.assertEqual({p.name: p.read_bytes() for p in directory.iterdir()}, before)


if __name__ == "__main__":
    unittest.main()
