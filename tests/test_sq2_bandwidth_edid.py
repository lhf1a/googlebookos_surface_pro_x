import importlib.util
import pathlib
import unittest

SCRIPT=pathlib.Path(__file__).resolve().parents[1]/'scripts/build-sq2-bandwidth-edid.py'
spec=importlib.util.spec_from_file_location('bw_edid',SCRIPT)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class TimingTest(unittest.TestCase):
    def test_exact_modes_and_preserved_sync_physical_fields(self):
        template=bytes.fromhex('70964078b08037703020550412b710000019')
        for width,height,hz,clock in [(2560,1706,60,283160),(2560,1706,70,330360),(2700,1800,70,366170)]:
            with self.subTest(width=width,hz=hz):
                b=m.dtd(width,height,hz,template);d=m.decode(b)
                self.assertEqual((d['width'],d['height']),(width,height))
                self.assertEqual((d['htotal'],d['vtotal']),(width+120,height+55))
                self.assertEqual(d['pixel_clock_khz'],clock)
                self.assertLess(abs(d['hz']-hz),0.01)
                self.assertEqual(b[8:],template[8:])

if __name__=='__main__':unittest.main()
