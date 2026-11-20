#!/usr/bin/env python3
"""Configure linear 1:1 9-bit PWM backlight levels on Surface Pro X (SQ1/SQ2).

Without an explicit brightness-levels table in DT, Linux pwm-backlight generates
a 4096-entry CIE 1931 cubic curve (scale=non-linear).  Android's UI brightness
slider already applies an HLG/gamma curve expecting linear panel luminance, so
stacking both curves crushes slider 0%..21.3% (sysfs 0..61/4095) below 1 LSB of
the 9-bit pmc8180c LPG PWM channel (actual period 4,258,334 ns; 1/511 = 8,333 ns),
producing PWM_VALUE=0 (completely black screen) across the bottom fifth of the
slider and compressing most usable luminance into 80%..100%.

Setting pwms period to 4,260,000 ns keeps the exact same pmc8180c LPG clock,
pre-divider, exponent, and actual period (19.2 MHz / (5 * 32 * 511) = 234.83 Hz,
4,258,334 ns) while mapping interpolated linear brightness-levels 0..511
bijectively 1:1 onto hardware PWM_VALUE 0..511 with zero bottom truncation and
zero top clipping.
"""
import argparse
import importlib.util
from pathlib import Path
import struct
import libfdt

spec = importlib.util.spec_from_file_location("dt", Path(__file__).with_name("spx-dt.py"))
dt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dt)

EXPECTED_CHANNEL = 4
UPSTREAM_PERIOD_NS = 5_000_000
ALIGNED_PERIOD_NS = 4_260_000
MAX_LEVEL = 511
DEFAULT_LEVEL = 256


def prepare(data):
    fdt = libfdt.Fdt(data)
    before = dt.properties(fdt)
    boards = before.get(("/", "compatible"), b"").split(b"\0")
    if b"microsoft,surface-pro-x" not in boards or b"qcom,sc8180x" not in boards:
        raise ValueError("Surface Pro X / SC8180X required")
    matches = [p for (p, n), v in before.items()
               if n == "compatible" and b"pwm-backlight" in v.split(b"\0")]
    if len(matches) != 1:
        raise ValueError("expected one pwm-backlight node")
    path = matches[0]
    pwms = before.get((path, "pwms"))
    if pwms is None or len(pwms) != 12:
        raise ValueError("expected 3-cell pwms property on pwm-backlight")
    phandle, channel, period_ns = struct.unpack(">3I", pwms)
    if channel != EXPECTED_CHANNEL:
        raise ValueError(f"unexpected backlight PWM channel {channel}")
    if period_ns not in (UPSTREAM_PERIOD_NS, ALIGNED_PERIOD_NS):
        raise ValueError(f"unexpected backlight PWM period {period_ns} ns")

    new_pwms = struct.pack(">3I", phandle, channel, ALIGNED_PERIOD_NS)
    new_levels = struct.pack(">2I", 0, MAX_LEVEL)
    new_steps = struct.pack(">I", MAX_LEVEL)
    new_default = struct.pack(">I", DEFAULT_LEVEL)

    if (before.get((path, "pwms")) == new_pwms
            and before.get((path, "brightness-levels")) == new_levels
            and before.get((path, "num-interpolated-steps")) == new_steps
            and before.get((path, "default-brightness-level")) == new_default):
        return data

    fdt.resize(len(data) + 512)
    off = fdt.path_offset(path)
    fdt.setprop(off, "pwms", new_pwms)
    fdt.setprop(off, "brightness-levels", new_levels)
    fdt.setprop(off, "num-interpolated-steps", new_steps)
    fdt.setprop(off, "default-brightness-level", new_default)
    fdt.pack()

    expected = dict(before)
    expected[path, "pwms"] = new_pwms
    expected[path, "brightness-levels"] = new_levels
    expected[path, "num-interpolated-steps"] = new_steps
    expected[path, "default-brightness-level"] = new_default
    assert dt.properties(fdt) == expected
    original = libfdt.Fdt(data)
    assert [fdt.get_mem_rsv(i) for i in range(fdt.num_mem_rsv())] == [
        original.get_mem_rsv(i) for i in range(original.num_mem_rsv())]
    return bytes(fdt.as_bytearray())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = prepare(args.input.read_bytes())
    with args.output.open("xb") as output:
        output.write(result)
