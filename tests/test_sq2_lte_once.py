import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('lte_once', ROOT/'scripts/prepare-sq2-lte-once.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
BASE = b'''insmod part_gpt
insmod fat
insmod chain
set timeout=10
set default=sq2-default
menuentry 'SQ2 Android + Linux - Qualcomm / 60Hz / GPU700' --id sq2-default {
 search --no-floppy --fs-uuid --set=root B027-0512
 chainloader /EFI/SQ2Default/el2launch.efi
}
menuentry 'SQ2 working fallback - Mesa / 60Hz / GPU700' --id sq2-fallback {
 search --no-floppy --fs-uuid --set=root B027-0512
 chainloader /EFI/SQ2Fallback/el2launch.efi
}
'''


class Once(unittest.TestCase):
    def test_defaults_and_order(self):
        menu, env = module.prepare(BASE)
        self.assertEqual(menu.count(b'menuentry '), 2)
        self.assertEqual(menu.count(b'set default=sq2-default\n'), 1)
        self.assertNotIn(b'set default=sq2-fallback', menu)
        self.assertEqual(len(env), 1024)
        self.assertIn(b'sq2_lte_once=1\n', env)
        self.assertLess(menu.index(b'save_env'), menu.index(b'chainloader /EFI/SQ2LTE/'))
        self.assertIn(b'set sq2_lte_once=unknown', menu)
        self.assertIn(b'if [ "$sq2_lte_once" = "0" ]', menu)
        self.assertEqual(menu[menu.index(b"menuentry 'SQ2 working fallback"):],
                         BASE[BASE.index(b"menuentry 'SQ2 working fallback"):])

    def test_wrong_baseline_refused(self):
        for data in [b'', BASE+b'\n', BASE.replace(b'sq2-default', b'sq2-fallback'),
                     BASE+b'menuentry extra --id extra {}\n']:
            with self.subTest(data=data[:30]), self.assertRaises(ValueError):
                module.prepare(data)


if __name__ == '__main__':
    unittest.main()
