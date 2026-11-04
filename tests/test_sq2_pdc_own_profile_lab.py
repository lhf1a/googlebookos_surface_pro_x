"""Local PDC loading session regression. No RF, modem or real QRTR sockets."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PdcOwnProfile(unittest.TestCase):
    def test_client_scoped_chunks(self):
        with tempfile.TemporaryDirectory(prefix='sq2-pdc-session-') as temp:
            binary = pathlib.Path(temp) / 'fixture'
            subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra',
                            '-Werror', str(ROOT / 'tests/sq2_pdc_session_test.c'),
                            '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True,
                                    text=True, check=True)
            self.assertIn('sockets=1 duplicate_transactions=0 remaining=19116,18092',
                          result.stdout)

    def test_native_request_guards(self):
        with tempfile.TemporaryDirectory(prefix='sq2-pdc-own-') as temp:
            binary = pathlib.Path(temp) / 'probe'
            subprocess.run(['gcc', '-std=c11', '-O2', '-Wall', '-Wextra',
                            '-Werror', str(ROOT / 'probe/test2/sq2-pdc-own-profile-lab.c'),
                            '-o', str(binary)], check=True)
            result = subprocess.run([str(binary), '--profile-self-test'],
                                    capture_output=True, text=True, check=True)
            self.assertIn('no activate/reset builders', result.stdout)
            for args in [('0', '27', 'activate'), ('0', '27', 'reset'),
                         ('0', '0', 'load-select-own-sq2')]:
                r = subprocess.run([str(binary), *args], capture_output=True)
                self.assertEqual(r.returncode, 2)
            r = subprocess.run([str(binary), '0', '27', 'load-select-own-sq2'],
                               cwd=temp, capture_output=True, text=True)
            self.assertEqual(r.returncode, 1)
            self.assertNotIn('OWN_PDC', r.stdout)
            self.assertIn('profile SHA256 mismatch', r.stderr)


if __name__ == '__main__':
    unittest.main()
