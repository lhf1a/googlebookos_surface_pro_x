"""Exercise the startup's zygote selection against multiple live process shapes."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class BootSelection(unittest.TestCase):
    def test_selects_init_child_not_app_zygote(self):
        text = (ROOT/'probe/lte/lte-framework-boot').read_text()
        block = text[text.index(' zygote=\n'):text.index(' test -n "$zygote"')]
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            for pid, parent in [(3120, 1), (6190, 3120)]:
                (base/str(pid)).mkdir()
                (base/str(pid)/'status').write_text(f'Name:\tzygote64\nPPid:\t{parent}\n')
            block = block.replace('/proc/', tmp+'/')
            for order in ['6190 3120', '3120 6190']:
                script = 'android(){ echo "'+order+'"; }\n'+block+'\nprintf "%s" "$zygote"\n'
                self.assertEqual(subprocess.check_output(['sh','-c',script], text=True), '3120')
            script = 'android(){ echo 6190; }\n'+block+'\ntest -n "$zygote"\n'
            self.assertNotEqual(subprocess.run(['sh','-c',script]).returncode, 0)


if __name__ == '__main__':
    unittest.main()
