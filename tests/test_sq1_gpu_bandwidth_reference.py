"""Boundary tests for the offline SQ1 bandwidth evidence reader."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/inspect-sq1-gpu-bandwidth-reference.py'
spec = importlib.util.spec_from_file_location('bwref', SCRIPT)
bwref = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bwref)


class ReferenceTests(unittest.TestCase):
    def test_nested_packages_keep_64_bit_values(self):
        self.assertEqual(bwref.parse_package('Package (3) { "GPU", 0x1f2d4ce40, Package (2) { Zero, Ones } }'),
                         ['GPU', 8369000000, [0, (1 << 64) - 1]])

    def test_comments_and_quoted_punctuation(self):
        self.assertEqual(bwref.parse_package('/* x */ Package (2) { "{//x}", // comment\nOne, }'),
                         ['{//x}', 1])

    def test_reject_package_count_and_trailing_data(self):
        for source in ['Package (2) { One }', 'Package (0) { One }',
                       'Package (1) { One } One', 'Package (One) { One, One }']:
            with self.subTest(source=source), self.assertRaises(ValueError):
                bwref.parse_package(source)

    def test_reject_expressions_and_overflow(self):
        for source in ['Package (1) { One + One }', 'Package (1) { SomeMethod () }',
                       'Package (1) { 0x10000000000000000 }', 'Package (65536) {}']:
            with self.subTest(source=source), self.assertRaises(ValueError):
                bwref.parse_package(source)

    def test_nesting_limit(self):
        with self.assertRaises(ValueError):
            bwref.parse_package('Package (1) {' * 34 + 'Zero' + '}' * 34)

    def test_block_ignores_braces_in_strings_and_rejects_duplicates(self):
        self.assertEqual(bwref.block('Device (GPU0) { "}" { } }', r'Device \(GPU0\)'), '{ "}" { } }')
        for source in ['Device (GPU0) {', 'Device (GPU0) {} Device (GPU0) {}']:
            with self.assertRaises(ValueError):
                bwref.block(source, r'Device \(GPU0\)')

    @staticmethod
    def evaluation():
        return ('Evaluation of \\BWQR returned object 0x1234, external buffer length 100\n'
                '  [Package] Contains 2 Elements:\n'
                '    [Package] Contains 2 Elements:\n'
                '      [String] Length 03 = "GPU"\n'
                '      [Integer] = 00000001F2D4CE40\n'
                '    [Package] Contains 0 Elements:\n\nACPI: No outstanding allocations\n')

    def test_evaluation_exact_structure(self):
        self.assertEqual(bwref.parse_evaluation(self.evaluation()), [['GPU', 8369000000], []])

    def test_reject_evaluation_failure_truncation_and_wrong_lengths(self):
        log = self.evaluation()
        for changed in [log.replace('returned object', 'failed'), log + log,
                        log.replace('Length 03', 'Length 04'), log.replace('Contains 2', 'Contains 3', 1),
                        log.replace('    [Package] Contains 0 Elements:\n', ''),
                        log.replace('      [Integer]', '    [Integer]'),
                        log.replace('[Integer]', '[Buffer]')]:
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                bwref.parse_evaluation(changed)

    def test_acpi_length_checksum(self):
        table = bytearray(36)
        table[:4] = b'DSDT'
        struct.pack_into('<I', table, 4, 36)
        table[9] = -sum(table) & 255
        bwref.check_acpi(table)
        for changed in [table[:-1], table + b'\0', b'X' + table[1:]]:
            with self.assertRaises(ValueError):
                bwref.check_acpi(changed)
        table[10] ^= 1
        with self.assertRaises(ValueError):
            bwref.check_acpi(table)

    def test_unpinned_driver_and_dsdt_rejected_before_output(self):
        with self.assertRaisesRegex(ValueError, 'driver hash'):
            bwref.driver_evidence(b'MZinvalid')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'input'
            source.write_bytes(b'DSDTinvalid')
            with self.assertRaisesRegex(ValueError, 'DSDT hash'):
                bwref.inspect(source, root / 'missing-driver', root / 'output')
            self.assertFalse((root / 'output').exists())


if __name__ == '__main__':
    unittest.main()
