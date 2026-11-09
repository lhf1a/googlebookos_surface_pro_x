import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("audit", ROOT / "scripts/inspect-quest2-full-stack.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)

class InputAuditTest(unittest.TestCase):
    def test_reference_paths_do_not_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "outside").symlink_to("/etc")
            for name in ("../passwd", "/etc/passwd", "outside/passwd"):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    audit.within(root, name)
            self.assertEqual(audit.within(root, "lib64/test.so"), root / "lib64/test.so")

    def test_real_elf_needed_and_plain_reference(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "probe.c"
            source.write_text('#include <stdio.h>\nint main(void){puts("audit");return 0;}\n')
            executable = root / "probe"
            subprocess.run(["cc", str(source), "-o", str(executable)], check=True)
            self.assertIn("libc.so.6", audit.elf_dependencies(executable))
            self.assertIsNone(audit.elf_dependencies(source))

if __name__ == "__main__":
    unittest.main()
