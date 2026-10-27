import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location('inspection', Path(__file__).resolve().parents[1] / 'scripts/inspect-recovery.py')
inspection = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspection)


def sample():
    data = bytearray(128 * 512)
    entries = bytearray(128 * 128)
    entries[:16] = bytes([1]) * 16
    entries[16:32] = bytes([2]) * 16
    struct.pack_into('<QQ', entries, 32, 40, 55)
    entries[56:68] = 'boot_a'.encode('utf-16le')
    for here, other, table in [(1, 127, 2), (127, 1, 95)]:
        h = bytearray(512)
        h[:8] = b'EFI PART'
        struct.pack_into('<IIII', h, 8, 0x10000, 92, 0, 0)
        struct.pack_into('<QQQQ', h, 24, here, other, 34, 94)
        struct.pack_into('<QIII', h, 72, table, 128, 128, zlib.crc32(entries))
        struct.pack_into('<I', h, 16, zlib.crc32(h[:92]))
        data[here * 512:(here + 1) * 512] = h
        data[table * 512:table * 512 + len(entries)] = entries
    data[40 * 512:40 * 512 + 8] = b'ANDROID!'
    struct.pack_into('<I', data, 40 * 512 + 40, 4)
    return data


class ImageValidation(unittest.TestCase):
    def run_image(self, data):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'image.raw'
            path.write_bytes(data)
            before = path.read_bytes()
            try:
                return inspection.inspect(path)
            finally:
                self.assertEqual(path.read_bytes(), before, 'Inspection must be read-only')

    def test_valid_primary_and_backup(self):
        result = self.run_image(sample())
        self.assertTrue(result['gpt_verified'])
        self.assertEqual(result['partitions'][0]['name'], 'boot_a')
        self.assertEqual(result['partitions'][0]['android_boot_header_version'], 4)

    def test_corrupt_primary_crc(self):
        data = sample()
        data[512 + 56] ^= 1
        with self.assertRaisesRegex(ValueError, 'CRC'):
            self.run_image(data)

    def test_corrupt_backup_table(self):
        data = sample()
        data[95 * 512] ^= 1
        with self.assertRaisesRegex(ValueError, 'CRC'):
            self.run_image(data)

    def test_payload_outside_partition(self):
        data = sample()
        struct.pack_into('<I', data, 40 * 512 + 8, 0x10000000)
        with self.assertRaisesRegex(ValueError, 'exceeds partition'):
            self.run_image(data)


if __name__ == '__main__':
    unittest.main()
