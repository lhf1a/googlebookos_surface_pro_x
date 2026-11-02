import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('lte_ref', ROOT/'scripts/inspect-sq2-lte-windows-reference.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Reference(unittest.TestCase):
    def test_unknown_input_refused(self):
        for data in [b'', b'MZ', bytes(4096), b'PE\0\0' + bytes(256)]:
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                module.inspect(data)

    def test_private_pinned_reference_when_available(self):
        sample = ROOT/'out/lte-reference-20261102-75886ecf/samples/qcremotefs8180.sys'
        if not sample.is_file():
            self.skipTest('Private authorized reference not supplied; no positive binary claim')
        result = module.inspect(sample.read_bytes())
        self.assertEqual(len(result['partitions']), 4)
        self.assertFalse(result['hardware_access'])
        self.assertEqual([x['type_guid'] for x in result['partitions']], module.GUIDS)
        self.assertEqual(sum(len(x['instruction_rvas']) for x in result['partitions']) + 1, 19)


if __name__ == '__main__':
    unittest.main()
