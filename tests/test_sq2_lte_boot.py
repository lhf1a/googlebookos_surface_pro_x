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


    def test_stages_ipa_firmware_into_persistent_firmware_dir(self):
        text = (ROOT/'probe/lte/lte-framework-boot').read_text()
        start = text.index('persist_fw=')
        end = text.index('# The kernel resolves firmware', start)
        block = text[start:end]
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            src = base/'src'
            fw_src = src/'firmware/qcom/sc8180x/sq2/ipa_fws.elf'
            fw_src.parent.mkdir(parents=True)
            fw_src.write_bytes(b'IPA_FW_TEST_PAYLOAD')
            persist = base/'sq2-firmware'
            persist.mkdir()
            script = f'set -eu\nsrc={src}\nSQ2_LTE_PERSIST_FW_DIR={persist}\n' + block
            subprocess.run(['sh', '-c', script], check=True)
            staged = persist/'qcom/sc8180x/sq2/ipa_fws.elf'
            self.assertTrue(staged.is_file())
            self.assertEqual(staged.read_bytes(), b'IPA_FW_TEST_PAYLOAD')

if __name__ == '__main__':
    unittest.main()
