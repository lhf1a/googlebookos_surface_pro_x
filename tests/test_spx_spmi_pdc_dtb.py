"""Tests for Surface Pro X SC8180X SPMI PMIC arbiter PDC wakeup DTB routing."""
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import libfdt

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("spmi_pdc_dt", ROOT / "scripts/prepare-spx-spmi-pdc-dtb.py")
spmi_pdc_dt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(spmi_pdc_dt)


def compile_dts(dts_text):
    with tempfile.TemporaryDirectory(prefix="spx-spmi-pdc-dt-") as td:
        src = Path(td) / "test.dts"
        dst = Path(td) / "test.dtb"
        src.write_text(dts_text)
        subprocess.run(["dtc", "-q", "-I", "dts", "-O", "dtb", "-o", str(dst), str(src)], check=True)
        return dst.read_bytes()


def fixture(
    *,
    board="microsoft,surface-pro-x",
    pdc_ranges="<0 480 94>, <94 609 31>",
    pdc_phandle_line="phandle = <0x52>;",
    spmi_irq_prop="interrupts = <0 481 4>;",
    irq_name="periph_irq",
):
    return f"""/dts-v1/;
/memreserve/ 0x99000000 0x1400000;
/ {{
    compatible = "{board}", "qcom,sc8180x";
    #address-cells = <2>;
    #size-cells = <2>;
    unrelated {{ phandle = <420>; keep = <9>; }};
    soc@0 {{
        #address-cells = <2>;
        #size-cells = <2>;
        pdc: interrupt-controller@b220000 {{
            compatible = "qcom,sc8180x-pdc", "qcom,pdc";
            reg = <0 0x0b220000 0 0x30000>;
            qcom,pdc-ranges = {pdc_ranges};
            #interrupt-cells = <2>;
            interrupt-controller;
            {pdc_phandle_line}
        }};
        spmi@c440000 {{
            compatible = "qcom,spmi-pmic-arb";
            interrupt-names = "{irq_name}";
            {spmi_irq_prop}
            qcom,ee = <0>;
        }};
    }};
}};
"""


class SpxSpmiPdcDtbTests(unittest.TestCase):
    def test_routes_spmi_irq_through_pdc_pin_1_and_is_idempotent(self):
        before = compile_dts(fixture())
        after = spmi_pdc_dt.prepare(before)
        props_before = spmi_pdc_dt.dt.properties(libfdt.Fdt(before))
        props_after = spmi_pdc_dt.dt.properties(libfdt.Fdt(after))

        spmi_path = "/soc@0/spmi@c440000"
        self.assertNotIn((spmi_path, "interrupts"), props_after)
        self.assertEqual(
            props_after[spmi_path, "interrupts-extended"],
            struct.pack(">3I", 0x52, 1, 4),
        )
        for key, value in props_before.items():
            if key != (spmi_path, "interrupts"):
                self.assertEqual(props_after[key], value)
        self.assertEqual(spmi_pdc_dt.prepare(after), after)

    def test_allocates_pdc_phandle_when_absent_and_still_idempotent(self):
        before = compile_dts(fixture(pdc_phandle_line=""))
        after = spmi_pdc_dt.prepare(before)
        props = spmi_pdc_dt.dt.properties(libfdt.Fdt(after))
        pdc_phandle = struct.unpack(">I", props["/soc@0/interrupt-controller@b220000", "phandle"])[0]
        self.assertGreater(pdc_phandle, 420)
        self.assertEqual(
            props["/soc@0/spmi@c440000", "interrupts-extended"],
            struct.pack(">3I", pdc_phandle, 1, 4),
        )
        self.assertEqual(spmi_pdc_dt.prepare(after), after)

    def test_rejects_invalid_board_irq_or_pdc_range(self):
        with self.assertRaisesRegex(ValueError, "Surface Pro X"):
            spmi_pdc_dt.prepare(compile_dts(fixture(board="other,board")))
        with self.assertRaisesRegex(ValueError, "interrupts property"):
            spmi_pdc_dt.prepare(compile_dts(fixture(spmi_irq_prop="interrupts = <0 482 4>;")))
        with self.assertRaisesRegex(ValueError, "PDC pin 1 maps to SPI"):
            spmi_pdc_dt.prepare(compile_dts(fixture(pdc_ranges="<0 490 94>")))
        with self.assertRaisesRegex(ValueError, "interrupt-names"):
            spmi_pdc_dt.prepare(compile_dts(fixture(irq_name="other_irq")))

    def test_full_image_wires_prepare_spx_spmi_pdc_dtb(self):
        text = (ROOT / "scripts/full_image.py").read_text()
        self.assertIn("prepare-spx-spmi-pdc-dtb.py", text)

    @unittest.skipUnless(os.environ.get("SPX_BOARD_DTS"), "requires SPX_BOARD_DTS env var")
    def test_transforms_real_board_dts_cleanly(self):
        board_dts = Path(os.environ["SPX_BOARD_DTS"])
        raw = subprocess.check_output(["dtc", "-q", "-I", "dts", "-O", "dtb", str(board_dts)])
        after = spmi_pdc_dt.prepare(raw)
        props = spmi_pdc_dt.dt.properties(libfdt.Fdt(after))
        pdc_phandle = struct.unpack(">I", props["/soc@0/interrupt-controller@b220000", "phandle"])[0]
        self.assertNotIn(("/soc@0/spmi@c440000", "interrupts"), props)
        self.assertEqual(
            props["/soc@0/spmi@c440000", "interrupts-extended"],
            struct.pack(">3I", pdc_phandle, 1, 4),
        )
        self.assertEqual(spmi_pdc_dt.prepare(after), after)


if __name__ == "__main__":
    unittest.main()
