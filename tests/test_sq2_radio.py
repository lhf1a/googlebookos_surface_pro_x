import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


class RadioProtocol(unittest.TestCase):
    def test_apn_scope_and_input_validation(self):
        spec = importlib.util.spec_from_file_location('apn', ROOT / 'scripts/prepare-sq2-radio-apn.py')
        apn = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(apn)
        row = ET.fromstring(apn.apn_resource('001001', 'test.apn')).find('apn')
        self.assertEqual((row.get('mcc'), row.get('mnc')), ('001', '001'))
        self.assertEqual(row.get('type'), 'default,supl')
        for numeric, value in [('1', 'apn'), ('00101', 'apn"/><apn'), ('00101', '')]:
            with self.assertRaises(ValueError):
                apn.apn_resource(numeric, value)

    def test_bounds_and_real_sim_signal_layouts(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'query'
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-g',
                            str(ROOT / 'probe/lte/sq2-radio-query.cpp'), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary), 'selftest'], check=True, capture_output=True, text=True)
            self.assertIn('PASS QMI bounds', result.stdout)

    def test_every_request_has_a_completion(self):
        spec = importlib.util.spec_from_file_location('gen', ROOT / 'scripts/generate-sq2-radio-defaults.py')
        gen = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gen)
        includes = ROOT / 'out/lte-radio-20261103/generated/include'
        if not includes.exists():
            self.skipTest('Run build-sq2-radio.py --prepare-only for pinned AIDL headers')
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / 'defaults.h'
            gen.generate(includes, out)
            text = out.read_text()
            self.assertIn('hangupConnectionResponse(info(in_serial, RadioError::REQUEST_NOT_SUPPORTED)', text)
            api = ROOT / 'out/lte-radio-20261103/reference/radio/aidl/aidl_api'
            for part in ('config', 'modem', 'network', 'sim', 'data', 'voice', 'messaging'):
                name = 'IRadio' + part.capitalize()
                aidl = api / ('android.hardware.radio.' + part) / '3/android/hardware/radio' / part / (name + '.aidl')
                expected = set(re.findall(r'oneway void (\w+)\(', aidl.read_text()))
                parsed = set(gen.methods(includes / 'aidl/android/hardware/radio' / part / (name + '.h')))
                self.assertEqual(expected, parsed - {'getInterfaceVersion', 'getInterfaceHash'})


if __name__ == '__main__':
    unittest.main()
