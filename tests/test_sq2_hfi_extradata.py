from pathlib import Path
import importlib.util
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('extra', ROOT/'scripts/inspect-sq2-hfi-extradata.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
def record(kind, values):
    payload = struct.pack('<'+'I'*len(values), *values)
    return struct.pack('<5I', 20+len(payload), 0, 1, kind, len(payload))+payload
END = record(0, [])

class FirmwareRecords(unittest.TestCase):
    def test_raw_color_mastering_and_light_values(self):
        vui = [1,5,10,10,0,1,9,16,9,0,1,0,0,0]
        mastering = [13250,7500,34000,34500,3000,16000,15635,16450,10000000,50]
        data = record(0x7f100006,vui)+record(0x15,mastering)+record(0x16,[1000,400])+END
        parsed = module.inspect(data+b'\xa5'*64)
        self.assertEqual(parsed['consumed'],len(data))
        self.assertEqual(parsed['records'][0]['values']['transfer'],16)
        self.assertEqual(parsed['records'][1]['values']['min_luminance'],50)
        self.assertEqual(parsed['records'][2]['values']['max_average'],400)
    def test_unknown_records_retained_without_interpretation(self):
        parsed = module.inspect(record(123,[9])+END)
        self.assertEqual(parsed['records'][0]['payload_hex'],'09000000')
    def test_malformed_buffers_rejected(self):
        bad = [b'', b'\0'*24, record(1,[2]), record(0,[2]), record(0x15,[1])+END,
               struct.pack('<5I',100,0,1,1,0),struct.pack('<5I',20,0,1,1,4),
               struct.pack('<5I',21,0,1,1,0)+b'\0'+END]
        for data in bad:
            with self.subTest(data=data), self.assertRaises(ValueError):
                module.inspect(data)
if __name__=='__main__':
    unittest.main()
