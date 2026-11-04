import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('pm_once', ROOT/'scripts/prepare-sq1-pm-once.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
BASE = b"""insmod part_gpt
insmod fat
insmod chain
set timeout=10
set default=spx-default
menuentry 'Surface Pro X SQ1/SQ2 - Qualcomm Vulkan' --id spx-default {
 search --no-floppy --fs-uuid --set=root B026-1027
 chainloader /EFI/SPX/default.efi
}
menuentry 'Surface Pro X SQ1/SQ2 - Mesa fallback' --id spx-fallback {
 search --no-floppy --fs-uuid --set=root B026-1027
 chainloader /EFI/SPX/fallback.efi
}
"""

class TestPmOnce(unittest.TestCase):
    def test_exact_two_entries_and_fallback(self):
        menu, env = m.prepare(BASE)
        self.assertEqual(menu.count(b'menuentry '), 2)
        self.assertIn(b'set default=spx-default\n', menu)
        self.assertEqual(menu.split(b"menuentry 'Surface Pro X SQ1/SQ2 - Mesa")[1],
                         BASE.split(b"menuentry 'Surface Pro X SQ1/SQ2 - Mesa")[1])
        self.assertEqual(len(env), 1024)
        self.assertIn(b'sq1_pm_once=1\n', env)

    def test_consume_and_readback_before_chain(self):
        menu, _ = m.prepare(BASE)
        self.assertLess(menu.index(b'save_env'), menu.index(b'set sq1_pm_once=unknown'))
        self.assertLess(menu.index(b'set sq1_pm_once=unknown'), menu.index(b'chainloader /EFI/SQ1PmTrial'))
        self.assertIn(b'if [ "$sq1_pm_once" = "0" ]; then', menu)
        self.assertIn(b'else\n  chainloader /EFI/SPX/default.efi', menu)

    def test_wrong_parent_and_rearming_refused(self):
        with self.assertRaises(ValueError):
            m.prepare(BASE+b'\n')
        menu, _ = m.prepare(BASE)
        with self.assertRaises(ValueError):
            m.prepare(menu)
