import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
import libfdt

spec = importlib.util.spec_from_file_location("camera_test_dt", Path(__file__).resolve().parents[1] / "scripts/prepare-spx-camera-test-dtb.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class CameraTestDT(unittest.TestCase):
    def test_board_resources_and_preservation(self):
        text = """/dts-v1/;
/memreserve/ 0x81234000 0x2000;
/ {
 compatible = "microsoft,surface-pro-x", "qcom,sc8180x";
 #address-cells = <2>; #size-cells = <2>;
 chosen { marker = "preserve"; };
 soc@0 {
  compatible = "simple-bus"; #address-cells = <2>; #size-cells = <2>; ranges;
  clock-controller@ad00000 { compatible = "qcom,sc8180x-camcc"; phandle = <42>; };
  interrupt-controller@17a00000 { compatible = "arm,gic-v3"; phandle = <2>; };
  pinctrl@3100000 { compatible = "qcom,sc8180x-tlmm"; phandle = <3>; };
  regulators-a { compatible = "qcom,pmc8180-rpmh-regulators"; qcom,pmic-id = "a";
   vdd-l1-l8-l11-supply = <2>; vdd-l7-l12-l14-l15-supply = <2>; vdd-l13-l16-l17-supply = <2>;
   ldo7 { regulator-min-microvolt = <1800000>; };
  };
  spmi@c440000 { pmic@1 { }; };
  regulators-e { compatible = "qcom,pmc8180-rpmh-regulators"; qcom,pmic-id = "e";
   ldo14 { phandle = <41>; };
  };
 };
};"""
        with tempfile.TemporaryDirectory() as directory:
            source, binary = Path(directory)/"in.dts", Path(directory)/"in.dtb"
            source.write_text(text)
            subprocess.run(["dtc", "-q", "-I", "dts", "-O", "dtb", "-o", str(binary), str(source)], check=True)
            original = binary.read_bytes()
        result = mod.prepare(original)
        f = libfdt.Fdt(result)
        before, after = mod.base.properties(libfdt.Fdt(original)), mod.base.properties(f)
        self.assertTrue(all(after[k] == v for k, v in before.items()))
        self.assertEqual(f.get_mem_rsv(0)[1:], [0x81234000, 0x2000])
        supply = f.get_phandle(f.path_offset("/soc@0/spmi@c440000/pmic@1/camera-regulators/l14"))
        self.assertEqual(after["/camera-id-front", "iovdd-supply"], mod.cells(supply))
        self.assertNotEqual(supply, 41)
        self.assertFalse(any(p.startswith("/soc@0/regulators-a/ldo1") for p, n in after))
        self.assertEqual(after["/camera-id-rear", "reset-gpios"], mod.cells(3, 22, 1))
        self.assertEqual(after["/camera-id-ir", "clocks"], mod.cells(42, 105))
        self.assertEqual(after["/soc@0/cci@ac4b000", "interrupts"], mod.cells(0, 271, 1))
        self.assertEqual(after["/soc@0/cci@ac4a000/i2c-bus@1", "clock-frequency"], mod.cells(100000))
        self.assertNotIn(("/soc@0/spmi@c440000/pmic@1/camera-regulators/l14", "regulator-always-on"), after)
        phandles = [v for (p, n), v in after.items() if n == "phandle"]
        self.assertEqual(len(phandles), len(set(phandles)))
        with self.assertRaisesRegex(ValueError, "node already exists"):
            mod.prepare(result)

if __name__ == "__main__":
    unittest.main()
