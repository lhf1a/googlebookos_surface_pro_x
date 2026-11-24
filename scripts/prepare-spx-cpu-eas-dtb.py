#!/usr/bin/env python3
"""Add SC8180X CPU dynamic-power-coefficient properties to enable Linux EAS.

Without `dynamic-power-coefficient` on `/cpus/cpu@*`, `qcom-cpufreq-hw`'s
`cpufreq_register_em_with_opp()` -> `dev_pm_opp_of_register_em()` skips Energy
Model registration even though `qcom-cpufreq-hw` populates real hardware OSM
LUT voltages into each OPP and `board.dts` already defines asymmetric
`capacity-dmips-mhz` (602 on Silver / 1024 on Gold). As a result, Energy Aware
Scheduling (`sched_energy_aware`) never activates under `schedutil`.

Values are chosen for SC8180X (7nm Kryo 495: 4x Cortex-A55 Silver + 4x
Cortex-A76 512KB L2 Gold) so `C / capacity_dmips_mhz` reflects the ~1.85x
efficiency gap between Silver (140 / 602 = 0.233) and Gold (440 / 1024 = 0.430),
consistent with mainline Qualcomm `sdm845.dtsi` (1.71x) and `sm8250.dtsi` (1.85x).
Applies identically to SQ1 and SQ2 via runtime DT CPU discovery.
"""
import argparse
import importlib.util
from pathlib import Path
import struct
import libfdt

spec = importlib.util.spec_from_file_location("dt", Path(__file__).with_name("spx-dt.py"))
dt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dt)

LITTLE_DYNAMIC_POWER_COEFF = 140
BIG_DYNAMIC_POWER_COEFF = 440
BIG_CAPACITY_THRESHOLD = 1024


def _classify_cpus(before):
    cpu_nodes = sorted(
        p for (p, n), v in before.items()
        if p.startswith("/cpus/") and p.count("/") == 2 and n == "device_type" and v == b"cpu\0"
    )
    if not cpu_nodes:
        raise ValueError('no /cpus/* nodes with device_type = "cpu" found')

    targets = {}
    little_count = 0
    big_count = 0
    for path in cpu_nodes:
        cap_raw = before.get((path, "capacity-dmips-mhz"))
        if cap_raw is None or len(cap_raw) != 4:
            raise ValueError(f"missing or invalid capacity-dmips-mhz on {path}")
        cap = struct.unpack(">I", cap_raw)[0]
        if cap == 0:
            raise ValueError(f"zero capacity-dmips-mhz on {path}")
        if cap < BIG_CAPACITY_THRESHOLD:
            targets[path] = LITTLE_DYNAMIC_POWER_COEFF
            little_count += 1
        else:
            targets[path] = BIG_DYNAMIC_POWER_COEFF
            big_count += 1

    if little_count == 0 or big_count == 0:
        raise ValueError(
            f"expected asymmetric little/big CPU clusters, got little={little_count} big={big_count}"
        )
    return targets


def prepare(data):
    fdt = libfdt.Fdt(data)
    before = dt.properties(fdt)
    boards = before.get(("/", "compatible"), b"").split(b"\0")
    if b"microsoft,surface-pro-x" not in boards or b"qcom,sc8180x" not in boards:
        raise ValueError("Surface Pro X / SC8180X required")

    targets = _classify_cpus(before)

    pending = {}
    for path, coeff in targets.items():
        desired = struct.pack(">I", coeff)
        existing = before.get((path, "dynamic-power-coefficient"))
        if existing is not None:
            if existing == desired:
                continue
            raise ValueError(
                f"conflicting dynamic-power-coefficient on {path}: "
                f"existing={existing.hex()} desired={desired.hex()}"
            )
        pending[path] = desired

    if not pending:
        return data

    fdt.resize(len(data) + 512 + 64 * len(pending))
    for path, desired in pending.items():
        off = fdt.path_offset(path)
        fdt.setprop(off, "dynamic-power-coefficient", desired)
    fdt.pack()

    expected = dict(before)
    for path, desired in pending.items():
        expected[path, "dynamic-power-coefficient"] = desired
    assert dt.properties(fdt) == expected

    original = libfdt.Fdt(data)
    assert [fdt.get_mem_rsv(i) for i in range(fdt.num_mem_rsv())] == [
        original.get_mem_rsv(i) for i in range(original.num_mem_rsv())
    ]
    return bytes(fdt.as_bytearray())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(prepare(args.input.read_bytes()))


if __name__ == "__main__":
    main()
