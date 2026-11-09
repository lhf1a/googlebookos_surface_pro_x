"""The opt-in display patch restores upstream modifiers and preserves capability filtering."""
import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("qti_display",ROOT/"scripts/patch-sq2-qti-display.py")
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class QTIDisplay(unittest.TestCase):
    def test_restore_only_modifier_list_and_reapply(self):
        original="prefix\n"+module.OLD+"\n"+module.HARDWARE_FILTER+"\nsuffix\n"
        result=module.patch(original)
        self.assertEqual(result,"prefix\n"+module.NEW+"\n"+module.HARDWARE_FILTER+"\nsuffix\n")
        self.assertEqual(module.patch(result),result)

    def test_missing_or_ambiguous_hardware_contract_refused(self):
        for text in (module.OLD,module.NEW,module.OLD+module.OLD+module.HARDWARE_FILTER,
                     module.OLD+module.NEW+module.HARDWARE_FILTER,
                     module.OLD+module.HARDWARE_FILTER.replace(" && !has_no_ubwc","")):
            with self.assertRaises(ValueError):module.patch(text)

if __name__ == "__main__":unittest.main()
