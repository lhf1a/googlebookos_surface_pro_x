import os
from pathlib import Path
import subprocess
import tempfile
import unittest

HELPER = Path(__file__).resolve().parents[1]/'probe/test2/audio-drivers-start'


class AudioProviderTests(unittest.TestCase):
    def run_guard(self, providers):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            for index, name, compatible, state, firmware, dt_firmware in providers:
                p = root/f'remoteproc{index}'
                (p/'device/of_node').mkdir(parents=True)
                for field, value in [('name', name), ('state', state), ('firmware', firmware),
                                     ('device/of_node/compatible', compatible+'\0'),
                                     ('device/of_node/firmware-name', dt_firmware+'\0')]:
                    (p/field).write_text(value)
            guard = HELPER.read_text().split('# The 6.3 NGD child')[0]
            guard = guard.replace('/sys/class/remoteproc', str(root))
            return subprocess.run(['sh', '-c', guard+'\necho ADSP_VERIFIED\n'],
                                  capture_output=True, text=True)

    def provider(self, index=7, name='adsp', state='attached', **kw):
        d = dict(compatible='qcom,sc8180x-adsp-pas',
                 firmware='qcom/msft/surface/pro-x-sq2/qcadsp8180.mbn',
                 dt_firmware='qcom/msft/surface/pro-x-sq2/qcadsp8180.mbn')
        d.update(kw)
        return index, name, d['compatible'], state, d['firmware'], d['dt_firmware']

    def test_names_indices_and_active_states(self):
        for name in ['17300000.remoteproc', 'adsp', 'provider-renamed']:
            for state in ['attached', 'running']:
                self.assertEqual(self.run_guard([self.provider(name=name, state=state)]).returncode, 0)

    def test_wrong_or_missing_provider(self):
        self.assertNotEqual(self.run_guard([]).returncode, 0)
        self.assertNotEqual(self.run_guard([self.provider(compatible='qcom,sc8180x-mpss-pas')]).returncode, 0)

    def test_wrong_state_or_firmware(self):
        for item in [self.provider(state='offline'), self.provider(firmware='wrong.mbn'),
                     self.provider(firmware='wrong.mbn', dt_firmware='wrong.mbn')]:
            self.assertNotEqual(self.run_guard([item]).returncode, 0)

    def test_ambiguous_providers(self):
        self.assertNotEqual(self.run_guard([self.provider(1), self.provider(9)]).returncode, 0)


if __name__ == '__main__':
    unittest.main()
