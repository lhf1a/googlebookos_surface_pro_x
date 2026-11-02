"""Execute the actual portable C validator; does not access a disk or modem."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Gpt(unittest.TestCase):
    def test_native_boundaries(self):
        with tempfile.TemporaryDirectory(prefix='sq2-lte-gpt-') as directory:
            binary = Path(directory)/'gpt-test'
            subprocess.run([os.environ.get('HOST_CC', 'cc'), '-std=c11', '-O2', '-Wall',
                            '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-pie',
                            '-no-pie', str(ROOT/'tests/sq2_lte_gpt_test.c'), '-o', str(binary)],
                           check=True, timeout=45)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, 'PASS: 35 bounded GPT/EFS GUID fixtures; no device I/O\n')
            self.assertEqual(result.stderr, '')


if __name__ == '__main__':
    unittest.main()
