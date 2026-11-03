"""Read-only RF observer: host tests never open a device or QRTR socket."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RfObserver(unittest.TestCase):
    def test_native_parser_and_command_gate(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = str(Path(temp) / 'rf-observe')
            subprocess.run([
                'gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                str(ROOT / 'probe/lte/surface-lte-rf-observe.c'), '-o', binary,
            ], check=True)
            result = subprocess.run([binary, 'selftest'], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('NV453-only whitelist, write/unknown rejection', result.stdout)
            for args in ([], ['write'], ['observe', '453'], ['--help']):
                with self.subTest(args=args):
                    result = subprocess.run([binary, *args], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 2)
                    self.assertEqual(result.stdout, '')
                    self.assertEqual(result.stderr, '')


if __name__ == '__main__':
    unittest.main()
