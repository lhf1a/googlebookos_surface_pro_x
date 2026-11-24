"""Tests for SC8180X Adreno 680 GPU DDR/LLCC opp-peak-kBps DTB & DTS patches."""
import importlib.util
from pathlib import Path
import re
import struct
import subprocess
import tarfile
import tempfile
import unittest
import libfdt

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("gpu_bw_dt", ROOT / "scripts/prepare-spx-gpu-bw-dtb.py")
gpu_bw_dt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gpu_bw_dt)

DTS_PATCH = ROOT / "patches/spx-portable-boot/0017-arm64-dts-qcom-sc8180x-gpu-opp-peak-bw.patch"
SOURCE_ARCHIVE = ROOT / "private/build-inputs/kernel-6.18/source/source.tar.gz"
BOARD_DTS = ROOT / "private/build-inputs/kernel-6.18/board.dts"

SQ1_OPPS = (
    (514_000_000, 0x1A0),
    (500_000_000, 0x180),
    (461_000_000, 0x140),
    (405_000_000, 0x100),
    (315_000_000, 0xC0),
    (256_000_000, 0x80),
    (177_000_000, 0x40),
)
SQ2_OPPS = (
    (700_000_000, 0x1B0),
    (661_000_000, 0x1B0),
    (590_000_000, 0x1A0),
) + SQ1_OPPS


def compile_dts(dts_text):
    with tempfile.TemporaryDirectory(prefix="spx-gpu-bw-dt-") as td:
        src = Path(td) / "test.dts"
        dst = Path(td) / "test.dtb"
        src.write_text(dts_text)
        subprocess.run(["dtc", "-q", "-I", "dts", "-O", "dtb", "-o", str(dst), str(src)], check=True)
        return dst.read_bytes()


def fixture(
    *,
    board="microsoft,surface-pro-x",
    opps=SQ2_OPPS,
    icc_names="gfx-mem",
    extra_first_opp="",
):
    opp_blocks = []
    for idx, (hz, level) in enumerate(opps):
        extra = extra_first_opp if idx == 0 else ""
        opp_blocks.append(f"""
            opp-{hz} {{
                opp-hz = /bits/ 64 <{hz}>;
                opp-level = <{level:#x}>;
                {extra}
            }};""")
    icc_block = ""
    if icc_names is not None:
        icc_block = f'interconnects = <0x05 0x05 0x00 0x06 0x01 0x00>;\n            interconnect-names = "{icc_names}";'
    return f"""/dts-v1/;
/memreserve/ 0x99000000 0x1400000;
/ {{
    compatible = "{board}", "qcom,sc8180x";
    #address-cells = <2>;
    #size-cells = <2>;
    soc@0 {{
        #address-cells = <2>;
        #size-cells = <2>;
        gpu@2c00000 {{
            compatible = "qcom,adreno-680.1", "qcom,adreno";
            reg = <0x0 0x2c00000 0x0 0x40000>;
            operating-points-v2 = <&gpu_opp_table>;
            {icc_block}
            status = "okay";

            gpu_opp_table: opp-table {{
                compatible = "operating-points-v2";
                {"".join(opp_blocks)}
            }};
        }};
    }};
    unrelated {{ keep = <42>; }};
}};
"""


class SpxGpuBwDtbTests(unittest.TestCase):
    def test_adds_opp_peak_kbps_for_sq1_and_sq2_and_is_idempotent(self):
        for opps in (SQ1_OPPS, SQ2_OPPS):
            raw = compile_dts(fixture(opps=opps))
            patched = gpu_bw_dt.prepare(raw)
            fdt = libfdt.Fdt(patched)

            seen = []
            for hz, _ in opps:
                off = fdt.path_offset(f"/soc@0/gpu@2c00000/opp-table/opp-{hz}")
                peak = struct.unpack(">I", bytes(fdt.getprop(off, "opp-peak-kBps")))[0]
                self.assertEqual(peak, gpu_bw_dt.peak_kbps_for_freq(hz))
                seen.append((hz, peak))

            # Bandwidth votes must be monotonically non-decreasing with frequency.
            by_freq = sorted(seen)
            for (_, p0), (_, p1) in zip(by_freq, by_freq[1:]):
                self.assertLessEqual(p0, p1)
            self.assertEqual(by_freq[-1][1], gpu_bw_dt.MAX_GPU_PEAK_KBPS)

            # Idempotent on re-run.
            self.assertEqual(gpu_bw_dt.prepare(patched), patched)

    def test_rejects_non_surface_board(self):
        raw = compile_dts(fixture(board="qcom,sc8180x-primus"))
        with self.assertRaisesRegex(ValueError, "Surface Pro X"):
            gpu_bw_dt.prepare(raw)

    def test_rejects_missing_gfx_mem_interconnect(self):
        raw = compile_dts(fixture(icc_names=None))
        with self.assertRaisesRegex(ValueError, "gfx-mem"):
            gpu_bw_dt.prepare(raw)

    def test_rejects_conflicting_existing_peak_bw(self):
        raw = compile_dts(fixture(extra_first_opp="opp-peak-kBps = <123456>;"))
        with self.assertRaisesRegex(ValueError, "conflicting opp-peak-kBps"):
            gpu_bw_dt.prepare(raw)

    def test_rejects_empty_opp_table(self):
        raw = compile_dts(fixture(opps=()))
        with self.assertRaisesRegex(ValueError, "no OPP child nodes"):
            gpu_bw_dt.prepare(raw)

    def test_wired_in_full_image(self):
        fi_source = (ROOT / "scripts/full_image.py").read_text()
        self.assertIn("prepare-spx-gpu-bw-dtb.py", fi_source)
        self.assertTrue(DTS_PATCH.is_file())

    def test_dts_patch_matches_script_tiers_and_applies_to_source(self):
        patch_text = DTS_PATCH.read_text()
        added_pairs = re.findall(
            r"opp-hz = /bits/ 64 <(\d+)>;\n[^+]*\+\s*opp-peak-kBps = <(\d+)>;",
            patch_text,
        )
        self.assertEqual(len(added_pairs), len(SQ1_OPPS))
        for hz_s, bw_s in added_pairs:
            self.assertEqual(int(bw_s), gpu_bw_dt.peak_kbps_for_freq(int(hz_s)))

        if not SOURCE_ARCHIVE.is_file():
            self.skipTest("private kernel source archive not present")
        with tempfile.TemporaryDirectory(prefix="spx-gpu-dts-patch-") as td:
            td = Path(td)
            rel = "arch/arm64/boot/dts/qcom/sc8180x.dtsi"
            with tarfile.open(SOURCE_ARCHIVE) as tf:
                member = tf.getmember("kernel/" + rel)
                dst = td / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                dst.write_bytes(tf.extractfile(member).read())
            res = subprocess.run(
                ["patch", "-p1", "--batch", "--forward", "--fuzz=0", "-i", str(DTS_PATCH)],
                cwd=td,
                capture_output=True,
                text=True,
            )
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
            patched_dtsi = (td / rel).read_text()
            for hz, _ in SQ1_OPPS:
                self.assertIn(f"opp-peak-kBps = <{gpu_bw_dt.peak_kbps_for_freq(hz)}>;", patched_dtsi)

    def test_full_image_models_sq1_dtb_preserves_and_populates_gpu_bw(self):
        import sys
        if str(ROOT / "scripts") not in sys.path:
            sys.path.insert(0, str(ROOT / "scripts"))
        import full_image_models as fim

        def full_fixture(opps):
            base = fixture(opps=opps)
            gmu = """        gmu@2c6a000 {
            compatible = "qcom,adreno-gmu-680.1", "qcom,adreno-gmu";
            operating-points-v2 = <&gmu_opp_table>;
            gmu_opp_table: opp-table {
                compatible = "operating-points-v2";
                opp-200000000 {
                    opp-hz = /bits/ 64 <200000000>;
                    opp-level = <0x30>;
                };
            };
        };
    };"""
            return base.replace("    };\n    unrelated {", gmu + "\n    unrelated {", 1)

        sq2_cur = gpu_bw_dt.prepare(compile_dts(full_fixture(SQ2_OPPS)))
        sq1_ref = compile_dts(full_fixture(SQ1_OPPS))
        sq1_out, _ = fim.sq1_dtb(sq2_cur, sq1_ref)
        fdt = libfdt.Fdt(sq1_out)
        for hz, _ in SQ1_OPPS:
            off = fdt.path_offset(f"/soc@0/gpu@2c00000/opp-table/opp-{hz}")
            peak = struct.unpack(">I", bytes(fdt.getprop(off, "opp-peak-kBps")))[0]
            self.assertEqual(peak, gpu_bw_dt.peak_kbps_for_freq(hz))
        with self.assertRaises(libfdt.FdtException):
            fdt.path_offset("/soc@0/gpu@2c00000/opp-table/opp-700000000")

    def test_real_board_dts_if_present(self):
        if not BOARD_DTS.is_file():
            self.skipTest("private board.dts not present")
        raw = compile_dts(BOARD_DTS.read_text())
        patched = gpu_bw_dt.prepare(raw)
        fdt = libfdt.Fdt(patched)
        for hz, _ in SQ2_OPPS:
            off = fdt.path_offset(f"/soc@0/gpu@2c00000/opp-table/opp-{hz}")
            peak = struct.unpack(">I", bytes(fdt.getprop(off, "opp-peak-kBps")))[0]
            self.assertEqual(peak, gpu_bw_dt.peak_kbps_for_freq(hz))
        self.assertEqual(gpu_bw_dt.prepare(patched), patched)


if __name__ == "__main__":
    unittest.main()
