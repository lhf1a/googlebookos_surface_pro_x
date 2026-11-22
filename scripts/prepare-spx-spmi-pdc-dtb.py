#!/usr/bin/env python3
"""Route SC8180X SPMI PMIC arbiter periph_irq through PDC wakeup pin 1.

Without this DT fix, board.dts connects spmi@c440000 (qcom,spmi-pmic-arb)
directly to GIC_SPI 481 via `interrupts`. qcom-pdc disables all PDC pins at
probe time, and GICv3 sets IRQCHIP_SKIP_SET_WAKE, so pm8941_pwrkey's
enable_irq_wake() never enables PDC pin 1 (which maps to GIC_SPI 481). Once APSS
and GIC power-collapse during extended s2idle, the power key cannot wake the SoC.

Switching spmi@c440000 to `interrupts-extended = <&pdc 1 IRQ_TYPE_LEVEL_HIGH>`
matches mainline sm8250/sc7180/sc8280xp and places PDC in the irq_chip
hierarchy so irq_set_irq_wake() arms PDC pin 1 on suspend.
"""
import argparse
import importlib.util
from pathlib import Path
import struct
import libfdt

spec = importlib.util.spec_from_file_location("dt", Path(__file__).with_name("spx-dt.py"))
dt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dt)

GIC_SPI = 0
SPMI_GIC_SPI = 481
PDC_SPMI_PIN = 1
IRQ_TYPE_LEVEL_HIGH = 4


def _validate_pdc_mapping(before, pdc_path):
    if before.get((pdc_path, "#interrupt-cells")) != struct.pack(">I", 2):
        raise ValueError(f"expected #interrupt-cells = <2> on {pdc_path}")
    ranges = before.get((pdc_path, "qcom,pdc-ranges"))
    if not ranges or len(ranges) % 12 != 0:
        raise ValueError(f"expected valid qcom,pdc-ranges on {pdc_path}")
    for i in range(0, len(ranges), 12):
        pin_base, parent_base, count = struct.unpack(">3I", ranges[i:i + 12])
        if pin_base <= PDC_SPMI_PIN < pin_base + count:
            mapped_spi = parent_base + (PDC_SPMI_PIN - pin_base)
            if mapped_spi == SPMI_GIC_SPI:
                return
            raise ValueError(
                f"PDC pin {PDC_SPMI_PIN} maps to SPI {mapped_spi}, expected {SPMI_GIC_SPI}"
            )
    raise ValueError(f"qcom,pdc-ranges on {pdc_path} does not cover pin {PDC_SPMI_PIN}")


def prepare(data):
    fdt = libfdt.Fdt(data)
    before = dt.properties(fdt)
    boards = before.get(("/", "compatible"), b"").split(b"\0")
    if b"microsoft,surface-pro-x" not in boards or b"qcom,sc8180x" not in boards:
        raise ValueError("Surface Pro X / SC8180X required")

    pdc_paths = [
        p for (p, n), v in before.items()
        if n == "compatible" and b"qcom,sc8180x-pdc" in v.split(b"\0")
    ]
    if len(pdc_paths) != 1:
        raise ValueError("expected one qcom,sc8180x-pdc node")
    pdc_path = pdc_paths[0]
    _validate_pdc_mapping(before, pdc_path)

    spmi_paths = [
        p for (p, n), v in before.items()
        if n == "compatible" and b"qcom,spmi-pmic-arb" in v.split(b"\0")
    ]
    if len(spmi_paths) != 1:
        raise ValueError("expected one qcom,spmi-pmic-arb node")
    spmi_path = spmi_paths[0]

    if before.get((spmi_path, "interrupt-names")) != b"periph_irq\0":
        raise ValueError(f"unexpected interrupt-names on {spmi_path}")

    pdc_phandle = fdt.get_phandle(fdt.path_offset(pdc_path))
    alloc_pdc_phandle = False
    if not pdc_phandle:
        maximum = max(
            (
                int.from_bytes(v, "big")
                for (_, n), v in before.items()
                if n in ("phandle", "linux,phandle") and len(v) == 4
            ),
            default=0,
        )
        if maximum >= 0xFFFFFFFF:
            raise ValueError("no unused phandle")
        pdc_phandle = maximum + 1
        alloc_pdc_phandle = True

    expected_ext = struct.pack(">3I", pdc_phandle, PDC_SPMI_PIN, IRQ_TYPE_LEVEL_HIGH)
    expected_legacy = struct.pack(">3I", GIC_SPI, SPMI_GIC_SPI, IRQ_TYPE_LEVEL_HIGH)

    if (not alloc_pdc_phandle
            and before.get((spmi_path, "interrupts-extended")) == expected_ext
            and (spmi_path, "interrupts") not in before):
        return data

    if before.get((spmi_path, "interrupts")) != expected_legacy:
        raise ValueError(f"unexpected interrupts property on {spmi_path}")
    if (spmi_path, "interrupts-extended") in before:
        raise ValueError(f"interrupts-extended already present alongside interrupts on {spmi_path}")

    fdt.resize(len(data) + 512)
    if alloc_pdc_phandle:
        fdt.setprop_u32(fdt.path_offset(pdc_path), "phandle", pdc_phandle)
    spmi_off = fdt.path_offset(spmi_path)
    fdt.delprop(spmi_off, "interrupts")
    fdt.setprop(spmi_off, "interrupts-extended", expected_ext)
    fdt.pack()

    expected = dict(before)
    if alloc_pdc_phandle:
        expected[pdc_path, "phandle"] = struct.pack(">I", pdc_phandle)
    del expected[spmi_path, "interrupts"]
    expected[spmi_path, "interrupts-extended"] = expected_ext
    assert dt.properties(fdt) == expected
    original = libfdt.Fdt(data)
    assert [fdt.get_mem_rsv(i) for i in range(fdt.num_mem_rsv())] == [
        original.get_mem_rsv(i) for i in range(original.num_mem_rsv())
    ]
    return bytes(fdt.as_bytearray())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = prepare(args.input.read_bytes())
    with args.output.open("xb") as output:
        output.write(result)
