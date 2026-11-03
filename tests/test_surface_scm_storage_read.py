"""Request encoding only; this test has no secure-call transport."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StorageRead(unittest.TestCase):
    def test_get_info_response_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = str(Path(tmp) / 'response-test')
            subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-function', '-I', str(ROOT / 'probe/lte'),
                            str(ROOT / 'tests/surface_scm_getinfo_response_test.c'),
                            '-o', binary], check=True)
            result = subprocess.run([binary], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('response_validation=1', result.stdout)
            self.assertIn('NO hardware calls', result.stdout)

    def test_read_only_abi_and_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = str(Path(tmp) / 'storage-test')
            subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'tests/surface_scm_storage_read_test.c'),
                            '-o', binary], check=True)
            result = subprocess.run([binary], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('NO hardware calls', result.stdout)


if __name__ == '__main__':
    unittest.main()
