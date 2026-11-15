import importlib.util
from pathlib import Path
import subprocess
import unittest
import libfdt

spec = importlib.util.spec_from_file_location(
    "camera_dtb", Path(__file__).parents[1] / "scripts/prepare-spx-camera-dtb.py")
camera = importlib.util.module_from_spec(spec)
spec.loader.exec_module(camera)


def tree(board="microsoft,surface-pro-x", camprop="", extra=""):
    source = f""" /dts-v1/;
    /memreserve/ 0x8b700000 0x500000;
    / {{
        compatible = "{board}", "qcom,sc8180x";
        #address-cells = <2>; #size-cells = <2>;
        provider {{ phandle = <123>; }};
        camera-clock {{
            compatible = "qcom,sc8180x-camcc"; {camprop}
        }};
        untouched {{ value = <1 2 3>; }};
        {extra}
    }};"""
    return subprocess.check_output(["dtc", "-I", "dts", "-O", "dtb"],
                                   input=source.encode())


class CameraReferenceTest(unittest.TestCase):
    def test_only_unused_reference_and_reservation_preserved(self):
        before = tree()
        after = camera.prepare(before)
        props = camera.properties(libfdt.Fdt(before))
        props["/camera-clock", "phandle"] = (124).to_bytes(4, "big")
        self.assertEqual(camera.properties(libfdt.Fdt(after)), props)
        self.assertEqual(libfdt.Fdt(after).get_mem_rsv(0),
                         libfdt.Fdt(before).get_mem_rsv(0))
        self.assertEqual(camera.prepare(after), after)

    def test_existing_reference_keeps_original_bytes(self):
        original = tree(camprop="phandle = <77>;")
        self.assertEqual(camera.prepare(original), original)

    def test_wrong_board_or_ambiguous_provider(self):
        with self.assertRaises(ValueError):
            camera.prepare(tree(board="example,other"))
        with self.assertRaises(ValueError):
            camera.prepare(tree(extra='second { compatible = "qcom,sc8180x-camcc"; };'))


if __name__ == "__main__":
    unittest.main()
