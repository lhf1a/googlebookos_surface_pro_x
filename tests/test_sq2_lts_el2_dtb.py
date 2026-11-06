import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

try:
    import libfdt
except ImportError:
    libfdt = None


@unittest.skipUnless(libfdt and shutil.which('dtc'), 'requires libfdt and dtc')
class LtsEl2DtbTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.script = Path(__file__).resolve().parents[1] / 'scripts/prepare-sq2-lts-el2-dtb.py'
        spec = importlib.util.spec_from_file_location('lts_dtb', cls.script)
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)
        dts = '''/dts-v1/;
        / {
            compatible = "microsoft,surface-pro-x", "qcom,sc8180x";
            #address-cells = <2>; #size-cells = <2>;
            reserved-memory {
                #address-cells = <2>; #size-cells = <2>; ranges;
                memory@85500000 { compatible = "qcom,rmtfs-mem";
                    reg = <0 0x85500000 0 0x200000>;
                    qcom,vmid = <15>; qcom,client-id = <1>; no-map; };
            };
            soc@0 {
                remoteproc@17300000 { compatible = "qcom,sc8180x-adsp-pas";
                    firmware-name = "qcom/adsp.mbn"; memory-region = <100>; };
                remoteproc@8300000 { compatible = "qcom,sc8180x-cdsp-pas";
                    firmware-name = "qcom/cdsp.mbn"; memory-region = <101>; };
                remoteproc@4080000 { compatible = "qcom,sc8180x-mpss-pas";
                    firmware-name = "qcom/mpss.mbn"; memory-region = <102>; };
            };
        };'''
        cls.pristine = subprocess.run(['dtc', '-q', '-I', 'dts', '-O', 'dtb'],
                                      input=dts.encode(), capture_output=True, check=True).stdout

    def get(self, data, path, name):
        return self.module.get(libfdt.Fdt(data), path, name)

    def mutate(self, path, name, value):
        dt = libfdt.Fdt(self.pristine)
        dt.resize(len(self.pristine) + 4096)
        dt.setprop(dt.path_offset(path), name, value)
        dt.pack()
        return bytes(dt.as_bytearray())

    def test_all_results_start_incomplete(self):
        data = self.module.prepare(self.pristine)
        for path in ['/reserved-memory/memory@85500000'] + [
                '/soc@0/remoteproc@' + address for address in ['17300000', '8300000', '4080000']]:
            self.assertEqual(self.get(data, path, 'microsoft,el2-preboot-complete'), bytes(4))
            self.assertEqual(self.get(data, path, 'microsoft,el2-preboot-status'), bytes(24))
        self.assertEqual(self.get(data, '/', 'microsoft,el2-kernel-fdt'), bytes(8))
        self.assertEqual(self.get(data, '/', 'microsoft,el2-pas-trace'), b'\xff' * 480)

    def test_native_resources_preserved(self):
        data = self.module.prepare(self.pristine)
        for name in ['firmware-name', 'memory-region', 'compatible']:
            path = '/soc@0/remoteproc@17300000'
            self.assertEqual(self.get(data, path, name), self.get(self.pristine, path, name))

    def test_idempotent(self):
        first = self.module.prepare(self.pristine)
        self.assertEqual(first, self.module.prepare(first))

    def test_no_false_success(self):
        data = self.mutate('/soc@0/remoteproc@17300000', 'microsoft,el2-preboot-complete',
                           self.module.cells(1))
        with self.assertRaisesRegex(ValueError, 'non-pristine'):
            self.module.prepare(data)

    def test_wrong_hardware_refused(self):
        data = self.mutate('/', 'compatible', b'not,surface\0')
        with self.assertRaisesRegex(ValueError, 'hardware'):
            self.module.prepare(data)

    def test_rmtfs_boundary_preserved(self):
        data = self.mutate('/reserved-memory/memory@85500000', 'reg',
                           self.module.cells(0, 0x85500000, 0, 0x400000))
        with self.assertRaisesRegex(ValueError, 'resource/ownership'):
            self.module.prepare(data)

    def test_input_cannot_be_output(self):
        import sys
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'original.dtb'
            path.write_bytes(self.pristine)
            result = subprocess.run([sys.executable, str(self.script), str(path), str(path)],
                                    capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(path.read_bytes(), self.pristine)

    def video_tree(self, phandle=None):
        dt = libfdt.Fdt(self.pristine)
        dt.resize(len(self.pristine) + 4096)
        dt.setprop(dt.path_offset('/'), 'phandle', self.module.cells(183))
        node = dt.add_subnode(dt.path_offset('/soc@0'), 'clock-controller@ab00000')
        dt.setprop(node, 'compatible', b'qcom,sc8180x-videocc\0qcom,sm8150-videocc\0')
        for name in ['#clock-cells', '#reset-cells', '#power-domain-cells']:
            node = dt.path_offset('/soc@0/clock-controller@ab00000')
            dt.setprop(node, name, self.module.cells(1))
        if phandle is not None:
            dt.setprop(dt.path_offset('/soc@0/clock-controller@ab00000'),
                       'phandle', self.module.cells(phandle))
        dt.pack()
        return bytes(dt.as_bytearray())

    def test_video_provider_allocates_after_existing_handles(self):
        result = self.module.prepare(self.video_tree())
        self.assertEqual(self.get(result, '/soc@0/clock-controller@ab00000', 'phandle'),
                         self.module.cells(184))

    def test_video_provider_preserves_existing_handle(self):
        data = self.video_tree(99)
        self.assertEqual(self.module.ensure_video_provider(data), data)

    def test_video_provider_idempotent(self):
        result = self.module.ensure_video_provider(self.video_tree())
        self.assertEqual(self.module.ensure_video_provider(result), result)

    def test_video_provider_rejects_zero_handle(self):
        with self.assertRaisesRegex(ValueError, 'invalid existing'):
            self.module.ensure_video_provider(self.video_tree(0))


if __name__ == '__main__':
    unittest.main()
