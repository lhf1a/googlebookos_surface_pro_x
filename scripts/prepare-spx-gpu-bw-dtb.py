#!/usr/bin/env python3
"""Add SC8180X Adreno 680 GPU opp-peak-kBps bandwidth votes to the DTB.

`gpu@2c00000` already binds the `gfx-mem` interconnect path (`gem_noc`
MASTER_GRAPHICS_3D -> `mc_virt` SLAVE_EBI_CH0), and `a6xx_gmu.c` calls
`dev_pm_opp_set_opp(&gpu->pdev->dev, opp)` on resume/DCVS transitions and
`dev_pm_opp_set_opp(&gpu->pdev->dev, NULL)` in `a6xx_gmu_stop()` on idle and
s2idle suspend. Without `opp-peak-kBps` on the GPU OPP nodes, `drivers/opp/of.c`
never acquires the `gfx-mem` interconnect path and `mas_qnm_gpu` votes 0 for
DDR (`MC0`), LLCC (`SH0`), and `ACV` regardless of GPU load.

Bandwidth tiers use SC8180X's discrete LPDDR4X-4266 per-channel DDR tiers
already defined in mainline `sc8180x.dtsi` (`cpu0`/`cpu4` OPP tables:
1804000..8368000 kB/s, scaled 8x across 8 channels by `bcm-voter.c`). Applies
uniformly to both SQ1 (177..514 MHz) and SQ2 (177..700 MHz) OPPs via runtime
DT discovery with no hardcoded phandles or device gating.
"""
import argparse
import importlib.util
from pathlib import Path
import struct
import libfdt

spec = importlib.util.spec_from_file_location("dt", Path(__file__).with_name("spx-dt.py"))
dt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dt)

# Ordered (max_gpu_hz, opp_peak_kBps) tiers mapped to SC8180X LPDDR4X-4266
# discrete DDR frequency levels (4 B/ch * DDR_MHz * 1000).
GPU_OPP_PEAK_KBPS_TIERS = (
    (177_000_000, 1_804_000),  # 451 MHz DDR (14.43 GB/s across 8ch)
    (256_000_000, 2_188_000),  # 547 MHz DDR (17.50 GB/s)
    (315_000_000, 3_072_000),  # 768 MHz DDR (24.58 GB/s)
    (405_000_000, 4_068_000),  # 1017 MHz DDR (32.54 GB/s)
    (461_000_000, 6_220_000),  # 1555 MHz DDR (49.76 GB/s)
    (500_000_000, 7_216_000),  # 1804 MHz DDR (57.73 GB/s)
    (514_000_000, 8_368_000),  # 2092 MHz DDR (66.94 GB/s, max)
)
MAX_GPU_PEAK_KBPS = 8_368_000


def peak_kbps_for_freq(hz):
    if hz <= 0:
        raise ValueError(f"invalid non-positive GPU opp-hz: {hz}")
    for max_hz, peak_kbps in GPU_OPP_PEAK_KBPS_TIERS:
        if hz <= max_hz:
            return peak_kbps
    return MAX_GPU_PEAK_KBPS


def _find_gpu_opp_table(before):
    gpu_paths = sorted(
        p for (p, n), v in before.items()
        if n == "compatible"
        and b"qcom,adreno-680.1" in v.split(b"\0")
        and b"qcom,adreno" in v.split(b"\0")
    )
    if len(gpu_paths) != 1:
        raise ValueError(f"expected one qcom,adreno-680.1 GPU node, found {len(gpu_paths)}")
    gpu_path = gpu_paths[0]

    icc_names = before.get((gpu_path, "interconnect-names"))
    if icc_names != b"gfx-mem\0":
        raise ValueError(f"expected interconnect-names = \"gfx-mem\" on {gpu_path}")
    icc = before.get((gpu_path, "interconnects"))
    if not icc or len(icc) < 16 or len(icc) % 4 != 0:
        raise ValueError(f"missing or invalid interconnects property on {gpu_path}")

    opp_ref = before.get((gpu_path, "operating-points-v2"))
    if opp_ref is None or len(opp_ref) != 4:
        raise ValueError(f"missing or invalid operating-points-v2 phandle on {gpu_path}")
    opp_phandle = struct.unpack(">I", opp_ref)[0]
    if opp_phandle == 0:
        raise ValueError(f"zero operating-points-v2 phandle on {gpu_path}")

    table_paths = sorted(
        p for (p, n), v in before.items()
        if n in ("phandle", "linux,phandle") and v == opp_ref
    )
    if len(table_paths) != 1:
        raise ValueError(
            f"expected one OPP table node for phandle {opp_phandle:#x}, found {len(table_paths)}"
        )
    table_path = table_paths[0]
    if before.get((table_path, "compatible")) != b"operating-points-v2\0":
        raise ValueError(f"expected operating-points-v2 compatible on {table_path}")

    prefix = table_path + "/"
    opp_nodes = sorted({
        p for (p, _) in before
        if p.startswith(prefix) and "/" not in p[len(prefix):]
    })
    if not opp_nodes:
        raise ValueError(f"no OPP child nodes found under {table_path}")

    targets = {}
    for opp_path in opp_nodes:
        hz_raw = before.get((opp_path, "opp-hz"))
        if hz_raw is None or len(hz_raw) != 8:
            raise ValueError(f"missing or invalid 64-bit opp-hz on {opp_path}")
        hz = struct.unpack(">Q", hz_raw)[0]
        targets[opp_path] = peak_kbps_for_freq(hz)
    return targets


def prepare(data):
    fdt = libfdt.Fdt(data)
    before = dt.properties(fdt)
    boards = before.get(("/", "compatible"), b"").split(b"\0")
    if b"microsoft,surface-pro-x" not in boards or b"qcom,sc8180x" not in boards:
        raise ValueError("Surface Pro X / SC8180X required")

    targets = _find_gpu_opp_table(before)

    pending = {}
    for path, peak_kbps in targets.items():
        desired = struct.pack(">I", peak_kbps)
        existing = before.get((path, "opp-peak-kBps"))
        if existing is not None:
            if existing == desired:
                continue
            raise ValueError(
                f"conflicting opp-peak-kBps on {path}: "
                f"existing={existing.hex()} desired={desired.hex()}"
            )
        pending[path] = desired

    if not pending:
        return data

    fdt.resize(len(data) + 512 + 64 * len(pending))
    for path, desired in pending.items():
        off = fdt.path_offset(path)
        fdt.setprop(off, "opp-peak-kBps", desired)
    fdt.pack()

    expected = dict(before)
    for path, desired in pending.items():
        expected[path, "opp-peak-kBps"] = desired
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
