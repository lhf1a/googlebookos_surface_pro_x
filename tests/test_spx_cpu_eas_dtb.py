"""Tests for Surface Pro X SC8180X CPU Energy Model (EAS) DTB transform."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import libfdt

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("cpu_eas_dt", ROOT / "scripts/prepare-spx-cpu-eas-dtb.py")
cpu_eas_dt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpu_eas_dt)


def compile_dts(dts_text):
    with tempfile.TemporaryDirectory(prefix="spx-cpu-eas-dt-") as td:
        src = Path(td) / "test.dts"
        dst = Path(td) / "test.dtb"
        src.write_text(dts_text)
        subprocess.run(["dtc", "-q", "-I", "dts", "-O", "dtb", "-o", str(dst), str(src)], check=True)
        return dst.read_bytes()


def fixture(*, board="microsoft,surface-pro-x", extra_cpu0="", little_cap=602, big_cap=1024):
    cpus = []
    for idx in range(4):
        reg = idx * 0x100
        extra = extra_cpu0 if idx == 0 else ""
        cpus.append(f"""
        cpu@{reg:x} {{
            device_type = "cpu";
            compatible = "qcom,kryo485";
            reg = <0x0 0x{reg:x}>;
            capacity-dmips-mhz = <{little_cap}>;
            {extra}
        }};""")
    for idx in range(4, 8):
        reg = idx * 0x100
        cpus.append(f"""
        cpu@{reg:x} {{
            device_type = "cpu";
            compatible = "qcom,kryo485";
            reg = <0x0 0x{reg:x}>;
            capacity-dmips-mhz = <{big_cap}>;
        }};""")
    return f"""/dts-v1/;
/memreserve/ 0x99000000 0x1400000;
/ {{
    compatible = "{board}", "qcom,sc8180x";
    #address-cells = <2>;
    #size-cells = <2>;
    cpus {{
        #address-cells = <2>;
        #size-cells = <0>;
        {"".join(cpus)}
    }};
    unrelated {{ keep = <42>; }};
}};
"""


class SpxCpuEasDtbTests(unittest.TestCase):
    def test_adds_dynamic_power_coefficients_and_is_idempotent(self):
        raw = compile_dts(fixture())
        patched = cpu_eas_dt.prepare(raw)
        fdt = libfdt.Fdt(patched)

        for idx in range(4):
            off = fdt.path_offset(f"/cpus/cpu@{idx * 0x100:x}")
            val = struct.unpack(">I", bytes(fdt.getprop(off, "dynamic-power-coefficient")))[0]
            self.assertEqual(val, cpu_eas_dt.LITTLE_DYNAMIC_POWER_COEFF)
        for idx in range(4, 8):
            off = fdt.path_offset(f"/cpus/cpu@{idx * 0x100:x}")
            val = struct.unpack(">I", bytes(fdt.getprop(off, "dynamic-power-coefficient")))[0]
            self.assertEqual(val, cpu_eas_dt.BIG_DYNAMIC_POWER_COEFF)

        # Little cluster must be strictly more energy-efficient per unit capacity than big cluster.
        little_ratio = cpu_eas_dt.LITTLE_DYNAMIC_POWER_COEFF / 602.0
        big_ratio = cpu_eas_dt.BIG_DYNAMIC_POWER_COEFF / 1024.0
        self.assertLess(little_ratio, big_ratio)

        # Idempotent on re-run.
        self.assertEqual(cpu_eas_dt.prepare(patched), patched)

    def test_rejects_non_surface_board(self):
        raw = compile_dts(fixture(board="qcom,sc8180x-primus"))
        with self.assertRaisesRegex(ValueError, "Surface Pro X"):
            cpu_eas_dt.prepare(raw)

    def test_rejects_conflicting_existing_coefficient(self):
        raw = compile_dts(fixture(extra_cpu0="dynamic-power-coefficient = <999>;"))
        with self.assertRaisesRegex(ValueError, "conflicting dynamic-power-coefficient"):
            cpu_eas_dt.prepare(raw)

    def test_rejects_symmetric_clusters(self):
        raw = compile_dts(fixture(little_cap=1024, big_cap=1024))
        with self.assertRaisesRegex(ValueError, "asymmetric"):
            cpu_eas_dt.prepare(raw)

    def test_real_built_dtb_if_present(self):
        # Verify against any real board.dts / built DTB in workspace if present.
        board_dts = ROOT / "private/build-inputs/kernel-6.18/board.dts"
        if not board_dts.is_file():
            self.skipTest("private board.dts not present")
        raw = compile_dts(board_dts.read_text())
        patched = cpu_eas_dt.prepare(raw)
        fdt = libfdt.Fdt(patched)
        for reg, expected in ((0x0, 140), (0x300, 140), (0x400, 440), (0x700, 440)):
            off = fdt.path_offset(f"/cpus/cpu@{reg:x}")
            val = struct.unpack(">I", bytes(fdt.getprop(off, "dynamic-power-coefficient")))[0]
            self.assertEqual(val, expected)


if __name__ == "__main__":
    unittest.main()
