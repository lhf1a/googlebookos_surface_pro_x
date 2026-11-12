#!/usr/bin/env python3
"""Deterministic raw-FDT builder for isolated candidate EFI/SQ2Sde DTB.

Modifies ONLY:
1. /soc@0/gpu@2c00000 (+ qcom,gpu-pwrlevels and status="disabled" on experimental
   590/661/700 MHz OPPs to bind the official SDMSHRIKE V2 7-OPP indexed policy)
2. /soc@0/display-subsystem@ae00000 (dual compatible qcom,sde-kms + qcom,sc8180x-mdss,
   SC8180X SDE 5.1 catalog properties with 6 interfaces, clocks, interconnects,
   connectors=<0xbc>, and status="disabled" on upstream DPU/DSI/eDP child nodes)
3. /soc@0/qcom,dp_display@ae9a000 (phandle=<0xbc>, internal eDP INTF_5 controller
   with generic phy-qcom-edp, RPMHPD_CX OPP table <0x7f>, gpio130 VCC_EN, pwm-backlight)

All other nodes, raw property bytes, reservation map entries, and boot CPU ID
from the baseline SQ2 DTB are preserved byte-for-byte without dtc text round-trip.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("dtgate", ROOT / "scripts/check-kgsl-device-dtb.py")
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)

GPU = GATE.GPU
MDSS = GATE.MDSS
DP_EDP = GATE.DP_EDP
EDP_PHANDLE = 0xBC


def u32(*values):
    return struct.pack(">" + "I" * len(values), *values)


def strs(*items):
    return b"".join(s.encode("ascii") + b"\0" for s in items)


def serialize_fdt(nodes, meta):
    reserve, boot = meta
    strings = bytearray()
    offsets = {}
    for props in nodes.values():
        for name in props:
            if name not in offsets:
                offsets[name] = len(strings)
                strings.extend(name.encode("ascii") + b"\0")

    children = {p: [] for p in nodes}
    for path in nodes:
        if path != "/":
            parent = path.rsplit("/", 1)[0] or "/"
            children[parent].append(path)

    def emit(path):
        name = ("" if path == "/" else path.rsplit("/", 1)[-1]).encode("ascii") + b"\0"
        out = bytearray(u32(1) + name + b"\0" * (-len(name) % 4))
        for key, value in nodes[path].items():
            out.extend(u32(3, len(value), offsets[key]))
            out.extend(value)
            out.extend(b"\0" * (-len(value) % 4))
        for child in children[path]:
            out.extend(emit(child))
        out.extend(u32(2))
        return out

    block = bytes(emit("/")) + u32(9)
    rsv_bytes = b"".join(struct.pack(">QQ", a, s) for a, s in reserve) + bytes(16)
    off_mem_rsvmap = 40
    off_dt_struct = off_mem_rsvmap + len(rsv_bytes)
    off_dt_strings = off_dt_struct + len(block)
    totalsize = off_dt_strings + len(strings)
    header = struct.pack(
        ">10I",
        0xD00DFEED,
        totalsize,
        off_dt_struct,
        off_dt_strings,
        off_mem_rsvmap,
        17,
        16,
        boot,
        len(strings),
        len(block),
    )
    return header + rsv_bytes + block + bytes(strings)


def build_candidate(baseline_bytes, policy):
    before, meta = GATE.read_fdt(baseline_bytes)
    nodes = copy.deepcopy(before)

    existing_phandles = {
        GATE.cells(props["phandle"])[0]
        for props in nodes.values()
        if "phandle" in props and len(props["phandle"]) == 4
    }
    if EDP_PHANDLE in existing_phandles:
        raise ValueError(f"phandle 0x{EDP_PHANDLE:x} already in use")

    # 1. GPU: disable 3 experimental OPPs and attach official SDMSHRIKE V2 7-OPP indexed policy
    opp_map = GATE.opps(before)
    for hz in sorted(GATE.EXPERIMENTAL_HIGH_HZ):
        if hz not in opp_map:
            raise ValueError(f"expected experimental OPP {hz} in baseline DTB")
        nodes[opp_map[hz]]["status"] = b"disabled\0"

    ddr_table = policy["ddr_table_kBps"]
    nodes[GPU]["qcom,kgsl-bus-policy"] = b"indexed\0"
    nodes[GPU]["qcom,bus-table-ddr"] = u32(*ddr_table)

    pwrlevels_path = GPU + "/qcom,gpu-pwrlevels"
    nodes[pwrlevels_path] = {
        "#address-cells": u32(1),
        "#size-cells": u32(0),
    }
    rows = sorted(
        [r for r in policy["frequency_bindings"] if r["gpu_hz"] > 0],
        key=lambda r: r["gpu_hz"],
        reverse=True,
    )
    rows.append({"gpu_hz": 0, "min_index": 0, "default_index": 0, "max_index": 0})
    for idx, row in enumerate(rows):
        nodes[f"{pwrlevels_path}/qcom,gpu-pwrlevel@{idx}"] = {
            "reg": u32(idx),
            "qcom,gpu-freq": u32(row["gpu_hz"]),
            "qcom,bus-min": u32(row["min_index"]),
            "qcom,bus-freq": u32(row["default_index"]),
            "qcom,bus-max": u32(row["max_index"]),
        }

    # 2. MDSS / SDE: disable upstream DPU/DSI/DP/panel children while keeping OPP phandles intact
    for path in list(nodes):
        if path.startswith(MDSS + "/"):
            rel = path[len(MDSS) + 1:]
            if "/" not in rel or path.endswith("/aux-bus/panel"):
                nodes[path]["status"] = b"disabled\0"

    mdss_orig = before[MDSS]
    mdss_props = {
        "compatible": strs("qcom,sde-kms", "qcom,sc8180x-mdss"),
        "reg": u32(
            0, 0x0AE00000, 0, 0x00090000,
            0, 0x0AEB0000, 0, 0x00003000,
            0, 0x0AEAC000, 0, 0x00000214,
        ),
        "reg-names": strs("mdp_phys", "vbif_phys", "regdma_phys"),
        "power-domains": mdss_orig["power-domains"],
        "clocks": u32(
            0x2C, 0x12,
            0x2C, 0x13,
            0x6A, 0x00,
            0x6A, 0x20,
            0x6A, 0x2C,
            0x6A, 0x22,
            0x6A, 0x28,
        ),
        "clock-names": strs(
            "gcc_bus",
            "gcc_nrt_bus",
            "iface_clk",
            "core_clk",
            "vsync_clk",
            "lut_clk",
            "rot_clk",
        ),
        "clock-rate": u32(0, 0, 0, 200000000, 19200000, 200000000, 19200000),
        "clock-max-rate": u32(0, 0, 0, 460000000, 19200000, 460000000, 460000000),
        "assigned-clocks": u32(0x6A, 0x2C),
        "assigned-clock-rates": u32(19200000),
        "resets": mdss_orig["resets"],
        "interrupts": mdss_orig["interrupts"],
        "interrupt-controller": b"",
        "#interrupt-cells": u32(1),
        "interconnects": mdss_orig["interconnects"],
        "interconnect-names": strs(
            "qcom,sde-data-bus0",
            "qcom,sde-data-bus1",
            "qcom,sde-reg-bus",
        ),
        "qcom,sde-reg-bus,vectors-KBps": u32(0, 0, 0, 76800, 0, 150000, 0, 300000),
        "iommus": mdss_orig["iommus"],
        "connectors": u32(EDP_PHANDLE),
        "#address-cells": u32(2),
        "#size-cells": u32(2),
        "#power-domain-cells": u32(0),
        "status": b"okay\0",
        "phandle": mdss_orig["phandle"],
        # SC8180X (SDMSHRIKE SDE 5.1) hardware catalog
        "qcom,sde-off": u32(0x1000),
        "qcom,sde-len": u32(0x45C),
        "qcom,sde-ctl-off": u32(0x2000, 0x2200, 0x2400, 0x2600, 0x2800, 0x2A00),
        "qcom,sde-ctl-size": u32(0x1E0),
        "qcom,sde-ctl-display-pref": strs("primary", "primary", "none", "none", "none", "none"),
        "qcom,sde-mixer-off": u32(0x45000, 0x46000, 0x47000, 0x48000, 0x49000, 0x4A000),
        "qcom,sde-mixer-size": u32(0x320),
        "qcom,sde-mixer-display-pref": strs("primary", "primary", "none", "none", "none", "none"),
        "qcom,sde-dspp-top-off": u32(0x1300),
        "qcom,sde-dspp-top-size": u32(0x80),
        "qcom,sde-dspp-off": u32(0x55000, 0x57000, 0x59000, 0x5B000),
        "qcom,sde-dspp-size": u32(0x1800),
        "qcom,sde-dest-scaler-top-off": u32(0x61000),
        "qcom,sde-dest-scaler-top-size": u32(0x1C),
        "qcom,sde-dest-scaler-off": u32(0x800, 0x1000),
        "qcom,sde-dest-scaler-size": u32(0x800),
        "qcom,sde-wb-off": u32(0x66000),
        "qcom,sde-wb-size": u32(0x2C8),
        "qcom,sde-wb-xin-id": u32(6),
        "qcom,sde-wb-id": u32(2),
        "qcom,sde-wb-clk-ctrl": u32(0x3B8, 24),
        "qcom,sde-intf-off": u32(0x6B000, 0x6B800, 0x6C000, 0x6C800, 0x6D000, 0x6D800),
        "qcom,sde-intf-size": u32(0x280),
        "qcom,sde-intf-type": strs("dp", "dsi", "dsi", "dp", "dp", "dp"),
        "qcom,sde-pp-off": u32(0x71000, 0x71800, 0x72000, 0x72800, 0x73000, 0x73800),
        "qcom,sde-pp-slave": u32(0, 0, 0, 0, 0, 0),
        "qcom,sde-pp-size": u32(0xD4),
        "qcom,sde-pp-merge-3d-id": u32(0, 0, 1, 1, 2, 2),
        "qcom,sde-merge-3d-off": u32(0x84000, 0x84100, 0x84200),
        "qcom,sde-merge-3d-size": u32(0x100),
        "qcom,sde-te2-off": u32(0x2000, 0x2000, 0, 0, 0, 0),
        "qcom,sde-cdm-off": u32(0x7A200),
        "qcom,sde-cdm-size": u32(0x224),
        "qcom,sde-dsc-off": u32(0x81000, 0x81400, 0x81800, 0x81C00),
        "qcom,sde-dsc-size": u32(0x140),
        "qcom,sde-dither-off": u32(0x30E0, 0x30E0, 0x30E0, 0x30E0, 0x30E0, 0x30E0),
        "qcom,sde-dither-version": u32(0x00010000),
        "qcom,sde-dither-size": u32(0x20),
        "qcom,sde-sspp-type": strs("vig", "vig", "vig", "vig", "dma", "dma", "dma", "dma"),
        "qcom,sde-sspp-off": u32(0x5000, 0x7000, 0x9000, 0xB000, 0x25000, 0x27000, 0x29000, 0x2B000),
        "qcom,sde-sspp-src-size": u32(0x1F0),
        "qcom,sde-sspp-xin-id": u32(0, 4, 8, 12, 1, 5, 9, 13),
        "qcom,sde-sspp-excl-rect": u32(1, 1, 1, 1, 1, 1, 1, 1),
        "qcom,sde-sspp-smart-dma-priority": u32(5, 6, 7, 8, 1, 2, 3, 4),
        "qcom,sde-smart-dma-rev": strs("smart_dma_v2p5"),
        "qcom,sde-mixer-pair-mask": u32(2, 1, 4, 3, 6, 5),
        "qcom,sde-mixer-blend-op-off": u32(
            0x20, 0x38, 0x50, 0x68, 0x80, 0x98, 0xB0, 0xC8, 0xE0, 0xF8, 0x110
        ),
        "qcom,sde-sspp-clk-ctrl": u32(
            0x2AC, 0, 0x2B4, 0, 0x2BC, 0, 0x2C4, 0,
            0x2AC, 8, 0x2B4, 8, 0x2BC, 8, 0x2C4, 8,
        ),
        "qcom,sde-sspp-csc-off": u32(0x1A00),
        "qcom,sde-csc-type": strs("csc-10bit"),
        "qcom,sde-qseed-type": strs("qseedv3"),
        "qcom,sde-sspp-qseed-off": u32(0xA00),
        "qcom,sde-mixer-linewidth": u32(2560),
        "qcom,sde-sspp-linewidth": u32(4096),
        "qcom,sde-wb-linewidth": u32(4096),
        "qcom,sde-mixer-blendstages": u32(0xB),
        "qcom,sde-highest-bank-bit": u32(0x2),
        "qcom,sde-ubwc-version": u32(0x300),
        "qcom,sde-ubwc-bw-calc-version": u32(0x1),
        "qcom,sde-panic-per-pipe": b"",
        "qcom,sde-has-cdp": b"",
        "qcom,sde-has-src-split": b"",
        "qcom,sde-pipe-order-version": u32(0x1),
        "qcom,sde-has-dim-layer": b"",
        "qcom,sde-has-idle-pc": b"",
        "qcom,sde-has-dest-scaler": b"",
        "qcom,sde-max-dest-scaler-input-linewidth": u32(2048),
        "qcom,sde-max-dest-scaler-output-linewidth": u32(2560),
        "qcom,sde-max-bw-low-kbps": u32(9600000),
        "qcom,sde-max-bw-high-kbps": u32(9600000),
        "qcom,sde-min-core-ib-kbps": u32(2400000),
        "qcom,sde-min-llcc-ib-kbps": u32(800000),
        "qcom,sde-min-dram-ib-kbps": u32(800000),
        "qcom,sde-dram-channels": u32(2),
        "qcom,sde-num-nrt-paths": u32(0),
        "qcom,sde-dspp-ad-version": u32(0x00040000),
        "qcom,sde-dspp-ad-off": u32(0x28000, 0x27000),
        "qcom,sde-vbif-off": u32(0),
        "qcom,sde-vbif-size": u32(0x1040),
        "qcom,sde-vbif-id": u32(0),
        "qcom,sde-vbif-memtype-0": u32(3, 3, 3, 3, 3, 3, 3, 3),
        "qcom,sde-vbif-memtype-1": u32(3, 3, 3, 3, 3, 3),
        "qcom,sde-vbif-qos-rt-remap": u32(3, 3, 4, 4, 5, 5, 6, 6),
        "qcom,sde-vbif-qos-nrt-remap": u32(3, 3, 3, 3, 3, 3, 3, 3),
        "qcom,sde-danger-lut": u32(0x0000000F, 0x0000FFFF, 0x00000000, 0x00000000),
        "qcom,sde-safe-lut-linear": u32(4, 0xFFF8, 0, 0xFFF0),
        "qcom,sde-safe-lut-macrotile": u32(10, 0xFE00, 11, 0xFC00, 12, 0xF800, 0, 0xF000),
        "qcom,sde-safe-lut-nrt": u32(0, 0xFFFF),
        "qcom,sde-safe-lut-cwb": u32(0, 0xFFFF),
        "qcom,sde-qos-lut-linear": u32(
            4, 0x00000000, 0x00000357,
            5, 0x00000000, 0x00003357,
            6, 0x00000000, 0x00023357,
            7, 0x00000000, 0x00223357,
            8, 0x00000000, 0x02223357,
            9, 0x00000000, 0x22223357,
            10, 0x00000002, 0x22223357,
            11, 0x00000022, 0x22223357,
            12, 0x00000222, 0x22223357,
            13, 0x00002222, 0x22223357,
            14, 0x00012222, 0x22223357,
            0, 0x00112222, 0x22223357,
        ),
        "qcom,sde-qos-lut-macrotile": u32(
            10, 0x00000003, 0x44556677,
            11, 0x00000033, 0x44556677,
            12, 0x00000233, 0x44556677,
            13, 0x00002233, 0x44556677,
            14, 0x00012233, 0x44556677,
            0, 0x00112233, 0x44556677,
        ),
        "qcom,sde-qos-lut-nrt": u32(0, 0x00000000, 0x00000000),
        "qcom,sde-qos-lut-cwb": u32(0, 0x75300000, 0x00000000),
        "qcom,sde-cdp-setting": u32(1, 1, 1, 0),
        "qcom,sde-qos-cpu-mask": u32(0x3),
        "qcom,sde-qos-cpu-dma-latency": u32(300),
        "qcom,sde-reg-dma-off": u32(0),
        "qcom,sde-reg-dma-version": u32(0x00010001),
        "qcom,sde-reg-dma-trigger-off": u32(0x119C),
        "qcom,sde-secure-sid-mask": u32(0x04200801),
    }
    nodes[MDSS] = mdss_props

    nodes[MDSS + "/qcom,sde-sspp-vig-blocks"] = {
        "qcom,sde-vig-csc-off": u32(0x1A00),
        "qcom,sde-vig-qseed-off": u32(0xA00),
        "qcom,sde-vig-qseed-size": u32(0xA0),
        "qcom,sde-vig-gamut": u32(0x1D00, 0x00050000),
        "qcom,sde-vig-igc": u32(0x1D00, 0x00050000),
        "qcom,sde-vig-inverse-pma": b"",
    }
    nodes[MDSS + "/qcom,sde-sspp-dma-blocks"] = {}
    nodes[MDSS + "/qcom,sde-sspp-dma-blocks/dgm@0"] = {
        "qcom,sde-dma-igc": u32(0x400, 0x00050000),
        "qcom,sde-dma-gc": u32(0x600, 0x00050000),
        "qcom,sde-dma-inverse-pma": b"",
        "qcom,sde-dma-csc-off": u32(0x200),
    }
    nodes[MDSS + "/qcom,sde-sspp-dma-blocks/dgm@1"] = {
        "qcom,sde-dma-igc": u32(0x1400, 0x00050000),
        "qcom,sde-dma-gc": u32(0x600, 0x00050000),
        "qcom,sde-dma-inverse-pma": b"",
        "qcom,sde-dma-csc-off": u32(0x1200),
    }
    nodes[MDSS + "/qcom,sde-dspp-blocks"] = {
        "qcom,sde-dspp-igc": u32(0x0, 0x00030001),
        "qcom,sde-dspp-hsic": u32(0x800, 0x00010007),
        "qcom,sde-dspp-memcolor": u32(0x880, 0x00010007),
        "qcom,sde-dspp-hist": u32(0x800, 0x00010007),
        "qcom,sde-dspp-sixzone": u32(0x900, 0x00010007),
        "qcom,sde-dspp-vlut": u32(0xA00, 0x00010008),
        "qcom,sde-dspp-gamut": u32(0x1000, 0x00040001),
        "qcom,sde-dspp-pcc": u32(0x1700, 0x00040000),
        "qcom,sde-dspp-gc": u32(0x17C0, 0x00010008),
    }

    # 3. Internal eDP display node (/soc@0/qcom,dp_display@ae9a000)
    edp_orig = before[MDSS + "/displayport-controller@ae9a000"]
    panel_orig = before[MDSS + "/displayport-controller@ae9a000/aux-bus/panel"]
    nodes[DP_EDP] = {
        "compatible": strs("qcom,edp-display"),
        "cell-index": u32(0),
        "qcom,intf-index": u32(3),
        "qcom,phy-version": u32(0x420),
        "qcom,phy-mode": strs("edp"),
        "qcom,widebus-enable": b"",
        "qcom,max-pclk-frequency-khz": u32(810000),
        "qcom,max-lclk-frequency-khz": u32(810000),
        "reg": edp_orig["reg"],
        "reg-names": strs("dp_ahb", "dp_aux", "dp_link", "dp_p0", "dp_p1"),
        "interrupt-parent": edp_orig["interrupt-parent"],
        "interrupts": edp_orig["interrupts"],
        "phys": edp_orig["phys"],
        "phy-names": edp_orig["phy-names"],
        "clocks": edp_orig["clocks"],
        "clock-names": strs(
            "core_iface_clk",
            "core_aux_clk",
            "link_clk",
            "link_iface_clk",
            "strm0_pixel_clk",
        ),
        "assigned-clocks": edp_orig["assigned-clocks"],
        "assigned-clock-parents": edp_orig["assigned-clock-parents"],
        "operating-points-v2": edp_orig["operating-points-v2"],
        "power-domains": edp_orig["power-domains"],
        "pinctrl-names": strs("mdss_dp_active", "mdss_dp_sleep"),
        "pinctrl-0": edp_orig["pinctrl-0"] + panel_orig["pinctrl-0"],
        "pinctrl-1": edp_orig["pinctrl-0"] + panel_orig["pinctrl-0"],
        "qcom,edp-vcc-en-gpio": panel_orig["enable-gpios"],
        "backlight": panel_orig["backlight"],
        "status": b"okay\0",
        "phandle": u32(EDP_PHANDLE),
    }

    return serialize_fdt(nodes, meta)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--policy", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    baseline_bytes = args.baseline.read_bytes()
    policy = json.loads(args.policy.read_text(encoding="utf-8"))
    candidate_bytes = build_candidate(baseline_bytes, policy)
    report = GATE.check(
        baseline_bytes,
        candidate_bytes,
        policy,
        allow_disabled_high_opps=True,
        allow_sde_display=True,
    )
    if not report["passed"]:
        raise RuntimeError("Candidate DTB failed gate: " + json.dumps(report, indent=2))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(candidate_bytes)
    report_path = args.output.parent / "DTB-BUILD-REPORT.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "output": str(args.output),
        "sha256": hashlib.sha256(candidate_bytes).hexdigest(),
        "bytes": len(candidate_bytes),
        "enabled_gpu_opps": report["candidate_enabled_hz"],
        "passed": report["passed"],
    }, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
