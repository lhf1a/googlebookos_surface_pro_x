import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location(
    'genfs', Path(__file__).resolve().parents[1]/'scripts/fix-sq2-lts-genfs-policy.py')
genfs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(genfs)


class GenfsPolicyTests(unittest.TestCase):
    def fixture(self, bits=0x3a27):
        return (struct.pack('<II', 0xf97cff8c, 8) + b'SE Linux'
                + struct.pack('<IIII', 30, 1, 8, 7)
                + struct.pack('<IIIIQ', 64, 64, 1, 0, bits)
                + b'unchanged rules and permissive bitmap')

    def test_only_named_capability_changes(self):
        old = self.fixture()
        new = genfs.fix_policy(old)
        self.assertEqual(new, self.fixture(0x3227))
        self.assertEqual(len(new), len(old))

    def test_reject_already_prefix_policy(self):
        with self.assertRaises(AssertionError):
            genfs.fix_policy(self.fixture(0x3227))

    def test_reject_invalid_header(self):
        with self.assertRaises(AssertionError):
            genfs.fix_policy(b'bad!' + self.fixture()[4:])

    def test_reject_absent_capability_node(self):
        data = bytearray(self.fixture())
        struct.pack_into('<I', data, 40, 0)
        with self.assertRaises(AssertionError):
            genfs.fix_policy(bytes(data))


if __name__ == '__main__':
    unittest.main()
