import importlib.util
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("libui_patch",ROOT/"scripts/prepare-quest2-libui-mapper3.py")
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class SelectorPatch(unittest.TestCase):
    def test_only_verified_branch_changes(self):
        data=bytearray(b"\x00"*(module.OFFSET+32))
        data[module.OFFSET-8:module.OFFSET+4]=module.CONTEXT
        result=module.patch_selector(data)
        self.assertEqual(result[:module.OFFSET],data[:module.OFFSET])
        self.assertEqual(result[module.OFFSET+4:],data[module.OFFSET+4:])
        self.assertEqual(result[module.OFFSET:module.OFFSET+4],module.NOP)
        with self.assertRaises(ValueError):module.patch_selector(result)
        with self.assertRaises(ValueError):module.patch(data)

    def test_truncation_and_changed_context_rejected(self):
        for data in (b"",b"bad input",bytes(module.OFFSET+4)):
            with self.assertRaises(ValueError):module.patch_selector(data)
            with self.assertRaises(ValueError):module.patch(data)

if __name__=="__main__":unittest.main()
