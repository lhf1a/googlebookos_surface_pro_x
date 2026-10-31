"""Exercise first-run persistence and failure retry with the actual shell helper."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PageModeSetup(unittest.TestCase):
    def test_once_and_failure_retry(self):
        for fail in (False, True):
            with self.subTest(fail=fail), tempfile.TemporaryDirectory() as tmp:
                root = pathlib.Path(tmp)
                script = (ROOT / 'probe/test2/page-mode-notification-setup').read_text()
                script = script.replace('export PATH=/bin:/sbin', 'export PATH=/usr/bin:/bin')
                script = script.replace(
                    'android() { timeout 10 nsenter -t 1 -m -r -- /system/bin/"$@"; }',
                    '''android() {
case "$1" in
getprop) echo 1;;
pm) case "$2" in
list) printf 'UserInfo{0:System:811}\nUserInfo{10:User:412}\n';;
disable) echo "$*" >> CALLS; [ ! -f FAIL ];;
esac;;
esac
}'''.replace('CALLS', str(root / 'calls')).replace('FAIL', str(root / 'fail')))
                script = script.replace('/proc/1/root/data/vendor/sq2-page-mode-notification', str(root / 'state'))
                script = script.replace('/run/sq2-page-mode-notification-lock', str(root / 'lock'))
                runner = root / 'runner'
                runner.write_text(script)
                if fail:
                    (root / 'fail').touch()
                    result = subprocess.run(['sh', str(runner)], capture_output=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertFalse((root / 'state/done-v1').exists())
                    self.assertFalse((root / 'lock').exists())
                    (root / 'fail').unlink()
                    (root / 'calls').unlink()
                subprocess.run(['sh', str(runner)], check=True, capture_output=True)
                calls = (root / 'calls').read_text()
                self.assertEqual(len(calls.splitlines()), 2)
                self.assertIn('--user 0', calls)
                self.assertIn('--user 10', calls)
                self.assertEqual((root / 'state/done-v1').read_text(), '0\n10\n')
                subprocess.run(['sh', str(runner)], check=True, capture_output=True)
                self.assertEqual((root / 'calls').read_text(), calls)


if __name__ == '__main__':
    unittest.main()
