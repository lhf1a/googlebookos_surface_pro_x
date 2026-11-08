from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class SshFrameworkBoot(unittest.TestCase):
    def run_helper(self, ready_after):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stubs = {
                'cut': '#!/bin/sh\nn=$(cat "$CASE/clock" 2>/dev/null || echo 0)\necho "$n"\necho $((n+1)) > "$CASE/clock"\n',
                'sleep': '#!/bin/sh\nexit 0\n',
                'timeout': '#!/bin/sh\nn=$(cat "$CASE/clock")\nif [ "$n" -ge "$READY_AFTER" ]; then echo 1; else echo 0; fi\n',
                'start-ssh': '#!/bin/sh\ntouch "$CASE/started"\n',
            }
            for name, body in stubs.items():
                path = root / name
                path.write_text(body)
                path.chmod(0o755)
            helper = (ROOT / 'probe/test2/ssh-framework-boot').read_text()
            helper = helper.replace('export PATH=/bin:/sbin', 'export PATH="' + str(root) + ':/bin:/sbin"')
            result = subprocess.run(['sh', '-c', helper], env=dict(os.environ, CASE=str(root), READY_AFTER=str(ready_after)),
                                    capture_output=True, text=True, timeout=10)
            return result, (root / 'started').exists(), int((root / 'clock').read_text())

    def test_boot_after_old_four_minute_limit_starts_ssh(self):
        result, started, ticks = self.run_helper(500)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(started)
        self.assertEqual(ticks, 500)

    def test_full_ten_minutes_without_boot_completion_is_reported(self):
        result, started, ticks = self.run_helper(1000)
        self.assertEqual(result.returncode, 1)
        self.assertFalse(started)
        self.assertEqual(ticks, 601)
        self.assertIn('600 seconds', result.stderr)

    def test_ready_boot_starts_immediately(self):
        result, started, ticks = self.run_helper(0)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(started)
        self.assertEqual(ticks, 2)

if __name__ == '__main__':
    unittest.main()
