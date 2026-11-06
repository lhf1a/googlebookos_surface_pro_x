"""Execute the real RMTFS shell guard with sysfs fixtures, never firmware."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def exercise(source, names=('modem',), state='attached', compatible='qcom,sc8180x-mpss-pas',
             firmware_mismatch=False, memory=True, command='sq2.el2=1'):
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root/'cmdline').write_text(command+'\n')
        (root/'sys').mkdir()
        (root/'bin').mkdir()
        firmware = 'qcom/msft/surface/pro-x-sq2/qcmpss8180.mbn'
        for index, name in enumerate(names, 7):
            p = root/'sys'/('remoteproc'+str(index))
            dt = p/'device/of_node'
            dt.mkdir(parents=True)
            (p/'name').write_text(name+'\n')
            (p/'state').write_text(state+'\n')
            (p/'firmware').write_text(('wrong.mbn' if firmware_mismatch else firmware)+'\n')
            (dt/'compatible').write_bytes(compatible.encode()+b'\0')
            (dt/'firmware-name').write_bytes(firmware.encode()+b'\0')
        if memory:
            (root/'rmtfs_mem').symlink_to('/dev/null')
        stub = root/'bin/rmtfs'
        stub.write_text('#!/bin/sh\nprintf "RMTFS %s\\n" "$*"\n')
        stub.chmod(0o755)
        code = source.replace('/proc/cmdline', str(root/'cmdline'))
        code = code.replace('/sys/class/remoteproc', str(root/'sys'))
        code = code.replace('/dev/qcom_rmtfs_mem1', str(root/'rmtfs_mem'))
        return subprocess.run(['sh', '-c', code], capture_output=True, text=True,
                              env=dict(os.environ, PATH=str(root/'bin')+':'+os.environ['PATH']))


class Discovery(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT/'probe/virtualization/el2-rmtfs-start').read_text()

    def test_legacy_and_renamed_label(self):
        for label in ('4080000.remoteproc', 'modem', 'arbitrary-display-name'):
            result = exercise(self.source, names=(label,))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, 'RMTFS -r -o /run/rmtfs\n')

    def test_requires_attached_provider(self):
        for state in ('offline', 'crashed', 'running'):
            result = exercise(self.source, state=state)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn('RMTFS -r', result.stdout)

    def test_wrong_provider_and_firmware_rejected(self):
        for args in ({'compatible': 'qcom,sc8180x-adsp-pas'}, {'firmware_mismatch': True}):
            result = exercise(self.source, **args)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn('RMTFS -r', result.stdout)

    def test_exactly_one_provider_and_memory_required(self):
        for args in ({'names': ()}, {'names': ('modem', 'modem')}, {'memory': False}):
            self.assertNotEqual(exercise(self.source, **args).returncode, 0)

    def test_non_el2_keeps_original_start_mode(self):
        result = exercise(self.source, names=(), command='sq2.el2=0')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, 'RMTFS -r -o /run/rmtfs -s\n')


if __name__ == '__main__':
    unittest.main()
