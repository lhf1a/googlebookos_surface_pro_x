import importlib.util
from pathlib import Path
import struct
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('efs_container',ROOT/'scripts/inspect-sq2-efs-container.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


def fixture(tag=b'IMGEFSG'):
    d=bytearray(4096)
    struct.pack_into('<10I',d,0,20,3,472,0,3584,3584,3584,0,3584,0)
    d[40:47]=tag;d[47]=15
    return d


class Container(unittest.TestCase):
    def test_three_tags_and_no_decryption_claim(self):
        for tag in [b'IMGEFS1',b'IMGEFS2',b'IMGEFSG']:
            result=m.inspect(fixture(tag))
            self.assertEqual(result['layout'][2]['offset'],512)
            self.assertFalse(result['decrypted'])
            self.assertFalse(result['ready_for_calibration_only_transplant'])

    def test_geometry_rejections(self):
        bad=[b'',bytes(4096),fixture()[:-1]]
        for offset,value in [(4,4),(8,0xffffffff),(16,4096),(20,0),(24,123),(32,456)]:
            d=fixture();struct.pack_into('<I',d,offset,value);bad.append(d)
        for data in bad:
            with self.subTest(length=len(data)),self.assertRaises(ValueError):m.inspect(data)


if __name__=='__main__':unittest.main()
