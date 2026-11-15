#!/usr/bin/env python3
"""Offline SPX camera identification DT using direct SPMI camera regulators.
No streaming driver or Android provider is installed by this experiment.
"""
import argparse
import importlib.util
from pathlib import Path
import struct
import libfdt

spec = importlib.util.spec_from_file_location("camdt", Path(__file__).with_name("prepare-spx-camera-dtb.py"))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)

def cells(*values):
    return struct.pack(">" + "I" * len(values), *values)

def strings(*values):
    return b"".join(v.encode() + b"\0" for v in values)

def prepare(data):
    data = base.prepare(data)
    fdt = libfdt.Fdt(data)
    before = base.properties(fdt)
    reserved = [fdt.get_mem_rsv(i) for i in range(fdt.num_mem_rsv())]
    fdt.resize(len(data) + 16384)
    maximum = max(int.from_bytes(v, "big") for (p, n), v in before.items()
                  if n in ("phandle", "linux,phandle"))
    def find(compatible, pmic=None):
        paths = [p for (p, n), v in before.items() if n == "compatible"
                 and compatible.encode() in v.split(b"\0")
                 and (pmic is None or before.get((p, "qcom,pmic-id")) == strings(pmic))]
        if len(paths) != 1:
            raise ValueError(f"expected one {compatible}/{pmic}")
        return paths[0]
    def ref(path):
        nonlocal maximum
        ph = fdt.get_phandle(fdt.path_offset(path))
        if not ph:
            maximum += 1
            ph = maximum
            fdt.setprop_u32(fdt.path_offset(path), "phandle", ph)
        return ph
    def add(path, props):
        parent, name = path.rsplit("/", 1)
        if fdt.path_offset(path, quiet=(libfdt.NOTFOUND,)) >= 0:
            raise ValueError(f"node already exists: {path}")
        fdt.add_subnode(fdt.path_offset(parent or "/"), name)
        for key, value in props.items():
            fdt.setprop(fdt.path_offset(path), key, value)
        return ref(path)
    cam = ref(find("qcom,sc8180x-camcc"))
    gic = ref(find("arm,gic-v3"))
    tlmm_path = find("qcom,sc8180x-tlmm")
    tlmm = ref(tlmm_path)
    rpmh = find("qcom,pmc8180-rpmh-regulators", "a")
    # Keep the existing shared RPMh provider unchanged. Camera LDOs are direct SPMI.
    pmic = "/soc@0/spmi@c440000/pmic@1/camera-regulators"
    add(pmic, {"compatible": strings("microsoft,spx-camera-spmi-regulators"),
        "vdd_l1-supply": before[rpmh, "vdd-l1-l8-l11-supply"],
        "vdd_l14-supply": before[rpmh, "vdd-l7-l12-l14-l15-supply"],
        "vdd_l17-supply": before[rpmh, "vdd-l13-l16-l17-supply"]})
    # ACPI asks for 2.85V; the RPMh PLDO has 8mV steps, selecting 2.856V.
    rails = {}
    for number, low, high in [(1, 1200000, 1200000), (14, 1800000, 1800000),
                              (17, 2850000, 2856000)]:
        rails[number] = add(f"{pmic}/l{number}", {
            "regulator-name": strings(f"camera-ldo{number}a"),
            "regulator-min-microvolt": cells(low), "regulator-max-microvolt": cells(high)})
    soc = find("qcom,sc8180x-camcc").rsplit("/", 1)[0]
    for index, irq, pins in [(0, 460, (17, 18, 19, 20)), (1, 271, (31, 32, 33, 34))]:
        pin = add(f"{tlmm_path}/camera-cci{index}-pins", {
            "pins": strings(*(f"gpio{i}" for i in pins)), "function": strings("cci_i2c"),
            "drive-strength": cells(2), "bias-pull-up": b""})
        clock = 8 + index * 2
        path = f"{soc}/cci@{0xac4a000 + index * 0x1000:x}"
        add(path, {
            "compatible": strings("qcom,sc8180x-cci", "qcom,msm8996-cci"),
            "reg": cells(0, 0xac4a000 + index * 0x1000, 0, 0x1000),
            "interrupt-parent": cells(gic), "interrupts": cells(0, irq, 1),
            "power-domains": cells(cam, 7),
            "clocks": cells(cam, 5, cam, 125, cam, 17, cam, clock, cam, clock+1),
            "clock-names": strings("camnoc_axi", "slow_ahb_src", "cpas_ahb", "cci", "cci_src"),
            "assigned-clocks": cells(cam, clock+1), "assigned-clock-rates": cells(37500000),
            "pinctrl-names": strings("default"), "pinctrl-0": cells(pin),
            "#address-cells": cells(1), "#size-cells": cells(0)})
        for master in (0, 1):
            add(f"{path}/i2c-bus@{master}", {"reg": cells(master),
                "clock-frequency": cells(100000), "#address-cells": cells(1),
                "#size-cells": cells(0)})
    for name, mclk, gpio in [("front", 2, 12), ("rear", 0, 22), ("ir", 3, 23)]:
        pin = add(f"{tlmm_path}/camera-{name}-mclk-pins", {
            "pins": strings(f"gpio{13+mclk}"), "function": strings("cam_mclk"),
            "drive-strength": cells(2), "bias-disable": b""})
        props = {
            "compatible": strings(f"microsoft,spx-camera-id-{name}"),
            "power-domains": cells(cam, 7), "clocks": cells(cam, 99+2*mclk),
            "clock-names": strings("xclk"), "reset-gpios": cells(tlmm, gpio, 1),
            "pinctrl-names": strings("default"), "pinctrl-0": cells(pin),
            "iovdd-supply": cells(rails[14])}
        if name == "rear":
            props.update({"avdd-supply": cells(rails[17]), "dvdd-supply": cells(rails[1])})
        add(f"/camera-id-{name}", props)
    fdt.pack()
    after = base.properties(fdt)
    assert all(after[k] == v for k, v in before.items()), "existing property changed"
    assert reserved == [fdt.get_mem_rsv(i) for i in range(fdt.num_mem_rsv())]
    return bytes(fdt.as_bytearray())

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path)
    p.add_argument("output", type=Path)
    a = p.parse_args()
    result = prepare(a.input.read_bytes())
    with a.output.open("xb") as out:
        out.write(result)
