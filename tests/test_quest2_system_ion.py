import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class SystemIonTest(unittest.TestCase):
    def test_heap_contract_and_failure_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = pathlib.Path(directory) / "ion-test"
            subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I/usr/include/libdrm",
                            str(ROOT / "tests/quest2_system_ion_test.c"), "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], check=True, text=True, capture_output=True)
            self.assertIn("SYSTEM_ION_TEST_PASS", result.stdout)

if __name__ == "__main__":
    unittest.main()
