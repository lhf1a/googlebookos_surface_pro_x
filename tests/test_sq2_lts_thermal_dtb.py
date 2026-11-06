import importlib.util
from pathlib import Path
import struct
import unittest
import libfdt

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('thermal', ROOT / 'scripts/prepare-sq2-lts-thermal-dtb.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class ThermalTest(unittest.TestCase):
    def fixture(self):
        f = libfdt.Fdt.create_empty_tree(16384)
        def add(path, props):
            current = ''
            for name in path.strip('/').split('/'):
                if not name:
                    continue
                parent = current or '/'
                current += '/' + name
                try:
                    f.path_offset(current)
                except libfdt.FdtException:
                    f.add_subnode(f.path_offset(parent), name)
            for k, v in props.items():
                f.setprop(f.path_offset(path), k, v)
        add('/', {'compatible': b'microsoft,surface-pro-x\0qcom,sc8180x\0'})
        add('/soc@0/gpu@2c00000', {'compatible': b'qcom,adreno-680.1\0qcom,adreno\0', '#cooling-cells': m.u32(2), 'phandle': m.u32(701)})
        for hz, level in m.gpu.BASE_OPPS.items():
            add('/soc@0/gpu@2c00000/opp-table/opp-' + str(hz), {'opp-hz': struct.pack('>Q', hz), 'opp-level': m.u32(level)})
        add('/soc@0/sensor', {'compatible': b'qcom,tsens-v2\0', 'phandle': m.u32(709)})
        for zone in ('gpu-top-thermal', 'cluster0-thermal', 'cluster1-thermal'):
            add('/thermal-zones/' + zone, {'thermal-sensors': m.u32(709) + m.u32(5)})
            add('/thermal-zones/' + zone + '/trips', {})
        add('/thermal-zones/gpu-top-thermal/trips/trip-point0', {'type': b'passive\0', 'phandle': m.u32(711), 'temperature': m.u32(85000), 'hysteresis': m.u32(1000)})
        add('/thermal-zones/gpu-top-thermal/cooling-maps/map0', {'trip': m.u32(711), 'cooling-device': m.u32(701) + m.u32(0xffffffff) * 2})
        for cpu, ph in (('cpu@0', 731), ('cpu@400', 735)):
            add('/cpus/' + cpu, {'#cooling-cells': m.u32(2), 'phandle': m.u32(ph)})
        f.pack()
        return bytes(f.as_bytearray())

    def test_resource_references_and_policy(self):
        raw = self.fixture()
        result, report = m.build(raw)
        s = m.gpu.snapshot(libfdt.Fdt(result))
        for cluster, ph in ((0, 731), (1, 735)):
            z = f'/thermal-zones/cluster{cluster}-thermal'
            trip = s[z + '/trips/sq2-cpu-passive']
            self.assertEqual(trip['temperature'], m.u32(80000))
            self.assertEqual(trip['hysteresis'], m.u32(5000))
            self.assertEqual(s[z + '/cooling-maps/sq2-cpu']['trip'], trip['phandle'])
            self.assertEqual(s[z + '/cooling-maps/sq2-cpu']['cooling-device'], m.u32(ph) + m.u32(0xffffffff) * 2)
        self.assertEqual(s['/thermal-zones/gpu-top-thermal/trips/trip-point0']['temperature'], m.u32(75000))
        self.assertEqual(s['/soc@0/gpu@2c00000/opp-table/opp-661000000']['opp-level'], m.u32(432))
        self.assertEqual(s['/soc@0/gpu@2c00000/opp-table/opp-700000000']['opp-level'], m.u32(432))
        self.assertEqual(s['/soc@0/gpu@2c00000/opp-table/opp-590000000']['opp-level'], m.u32(416))
        self.assertFalse(report['hardware_verified'])
        with self.assertRaises(AssertionError):
            m.build(result)

    def test_wrong_map_rejected(self):
        f = libfdt.Fdt(self.fixture())
        f.setprop(f.path_offset('/thermal-zones/gpu-top-thermal/cooling-maps/map0'), 'trip', m.u32(999))
        with self.assertRaises(AssertionError):
            m.build(bytes(f.as_bytearray()))

    def test_661_remains_explicit_only(self):
        result, report = m.build(self.fixture(), 'validated-661')
        s = m.gpu.snapshot(libfdt.Fdt(result))
        self.assertEqual(s['/soc@0/gpu@2c00000/opp-table/opp-661000000']['opp-level'], m.u32(416))
        self.assertNotIn('/soc@0/gpu@2c00000/opp-table/opp-700000000', s)
        self.assertEqual(report['gpu_max_hz'], 661000000)


if __name__ == '__main__':
    unittest.main()
