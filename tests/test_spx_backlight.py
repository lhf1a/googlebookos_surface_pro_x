"""Tests for Surface Pro X linear 9-bit PWM backlight DT and Android brightness floor."""
import importlib.util
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
import libfdt

ROOT = Path(__file__).resolve().parents[1]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


backlight_dt = load_module("spx_backlight_dt", ROOT / "scripts/prepare-spx-backlight-dtb.py")
dt = load_module("spx_dt", ROOT / "scripts/spx-dt.py")

SAMPLE_DTS = """/dts-v1/;

/ {
    compatible = "microsoft,surface-pro-x", "qcom,sc8180x";

    pmc8180c_lpg: lpg {
        #pwm-cells = <2>;
    };

    backlight: backlight {
        compatible = "pwm-backlight";
        pwms = <&pmc8180c_lpg 4 5000000>;
    };
};
"""


def compile_dts(dts_text):
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "test.dts"
        dst = Path(tmp) / "test.dtb"
        src.write_text(dts_text)
        subprocess.run(["dtc", "-q", "-I", "dts", "-O", "dtb", "-o", str(dst), str(src)], check=True)
        return dst.read_bytes()


def lpg_calc_freq(period_ns):
    """Mirror drivers/leds/rgb/leds-qcom-lpg.c lpg_calc_freq() for 6/9-bit LPG PWM."""
    nsec_per_sec = 10**9
    clk_rates = [0, 1024, 32768, 19_200_000]
    pre_divs = [1, 3, 5, 6]
    best_err = 10**18
    best = None
    for bits in (6, 9):
        resolution = (1 << bits) - 1
        for clk_rate in clk_rates[1:]:
            numerator = period_ns * clk_rate
            for pre_div in pre_divs:
                denominator = nsec_per_sec * pre_div * resolution
                if numerator < denominator:
                    continue
                m = min(7, int(math.floor(math.log2(numerator // denominator))))
                actual = (denominator * (1 << m) + clk_rate - 1) // clk_rate
                err = period_ns - actual
                if err < best_err:
                    best_err = err
                    best = (bits, resolution, clk_rate, pre_div, m, actual)
    return best


def lpg_calc_duty(duty_ns, cfg):
    """Mirror drivers/leds/rgb/leds-qcom-lpg.c lpg_calc_duty()."""
    _, resolution, clk_rate, pre_div, m, _ = cfg
    val = (duty_ns * clk_rate) // (10**9 * pre_div * (1 << m))
    return min(val, resolution)


class SpxBacklightTests(unittest.TestCase):
    def test_prepare_backlight_dtb_linear_and_idempotent(self):
        raw = compile_dts(SAMPLE_DTS)
        out = backlight_dt.prepare(raw)
        props = dt.properties(libfdt.Fdt(out))
        phandle, channel, period = struct.unpack(">3I", props["/backlight", "pwms"])
        self.assertGreater(phandle, 0)
        self.assertEqual(channel, 4)
        self.assertEqual(period, 4_260_000)
        self.assertEqual(struct.unpack(">2I", props["/backlight", "brightness-levels"]), (0, 511))
        self.assertEqual(struct.unpack(">I", props["/backlight", "num-interpolated-steps"]), (511,))
        self.assertEqual(struct.unpack(">I", props["/backlight", "default-brightness-level"]), (256,))
        # Idempotent on already-prepared DTB
        self.assertEqual(backlight_dt.prepare(out), out)

    def test_preserves_lpg_frequency_and_maps_512_levels_bijectively(self):
        upstream = lpg_calc_freq(backlight_dt.UPSTREAM_PERIOD_NS)
        aligned = lpg_calc_freq(backlight_dt.ALIGNED_PERIOD_NS)
        self.assertEqual(aligned, upstream)
        self.assertEqual(aligned[0], 9)
        self.assertEqual(aligned[1], 511)
        self.assertEqual(aligned[5], 4_258_334)
        for level in range(512):
            duty_ns = (level * backlight_dt.ALIGNED_PERIOD_NS) // 511
            self.assertEqual(lpg_calc_duty(duty_ns, aligned), level)

    def test_rejects_non_spx_or_unexpected_channel(self):
        wrong_board = compile_dts(SAMPLE_DTS.replace("microsoft,surface-pro-x", "other,board"))
        with self.assertRaisesRegex(ValueError, "Surface Pro X"):
            backlight_dt.prepare(wrong_board)
        wrong_channel = compile_dts(SAMPLE_DTS.replace("<&pmc8180c_lpg 4 5000000>", "<&pmc8180c_lpg 3 5000000>"))
        with self.assertRaisesRegex(ValueError, "channel"):
            backlight_dt.prepare(wrong_channel)

    def test_overlay_brightness_floor_prevents_zero_pwm_across_full_slider(self):
        cfg_xml = ROOT / "probe/sensors/overlay/res/values/config.xml"
        tree = ET.parse(cfg_xml)
        items = {
            el.attrib["name"]: float(el.text.strip())
            for el in tree.findall("item")
            if el.attrib.get("type") == "dimen" and el.attrib.get("format") == "float"
        }
        min_f = items["config_screenBrightnessSettingMinimumFloat"]
        max_f = items["config_screenBrightnessSettingMaximumFloat"]
        dft_f = items["config_screenBrightnessSettingDefaultFloat"]
        self.assertGreater(min_f, 0.0)
        self.assertLess(min_f, dft_f)
        self.assertEqual(max_f, 1.0)

        cfg = lpg_calc_freq(backlight_dt.ALIGNED_PERIOD_NS)
        R, A, B, C = 0.5, 0.17883277, 0.28466892, 0.55991073
        prev_pwm = 0
        for pct in range(0, 101):
            norm = pct / 100.0
            ret = (norm / R) ** 2 if norm <= R else math.exp((norm - C) / A) + B
            brt = min_f + (max_f - min_f) * (min(12.0, max(0.0, ret)) / 12.0)
            sysfs_level = int(math.floor(brt * 511))
            pwm_val = lpg_calc_duty((sysfs_level * backlight_dt.ALIGNED_PERIOD_NS) // 511, cfg)
            self.assertGreaterEqual(pwm_val, 3, f"slider {pct}% dropped below minimum visible PWM")
            self.assertGreaterEqual(pwm_val, prev_pwm)
            prev_pwm = pwm_val
        self.assertEqual(prev_pwm, 511)


if __name__ == "__main__":
    unittest.main()
