"""Pure host tests; never contacts a modem or changes configuration."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PdcReadOnly(unittest.TestCase):
    def test_native_selftest_and_endpoint_guards(self):
        with tempfile.TemporaryDirectory(prefix='sq2-pdc-readonly-') as d:
            binary = pathlib.Path(d) / 'probe'
            subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra',
                            '-Werror', str(ROOT / 'probe/test2/sq2-pdc-readonly.c'),
                            '-o', str(binary)], check=True)
            result = subprocess.run([str(binary), '--self-test'], check=True,
                                    capture_output=True, text=True)
            self.assertIn('bounded metadata IDs remain private', result.stdout)
            for args in [('0', '0'), ('-1', '27'), ('0', '4294967295'),
                         ('0', '27extra'), ('0', '27', 'activate')]:
                result = subprocess.run([str(binary), *args],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 2, args)

    def test_only_read_operations_are_dispatched(self):
        source = (ROOT / 'probe/test2/sq2-pdc-readonly.c').read_text()
        self.assertIn('msg==0x28&&e&&e->type==config', source)
        self.assertIn('else if(msg==0x22||msg==0x24)', source)
        self.assertNotIn('query(&dst,0x23', source)
        self.assertNotIn('query(&dst,0x26', source)
        self.assertNotIn('query(&dst,0x27', source)


if __name__ == '__main__':
    unittest.main()
