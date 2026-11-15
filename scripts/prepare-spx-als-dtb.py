#!/usr/bin/env python3
"""Correct the SPX ALS compatible using the ACPI resource and TCS3430 identity."""
import argparse
import importlib.util
from pathlib import Path
import libfdt

spec = importlib.util.spec_from_file_location("dt", Path(__file__).with_name("spx-dt.py"))
dt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dt)


def prepare(data):
    fdt = libfdt.Fdt(data)
    before = dt.properties(fdt)
    boards = before.get(("/", "compatible"), b"").split(b"\0")
    if b"microsoft,surface-pro-x" not in boards or b"qcom,sc8180x" not in boards:
        raise ValueError("Surface Pro X / SC8180X required")
    matches = [p for (p, n), v in before.items()
               if n == "reg" and v == bytes.fromhex("00000000008880000000000000004000")
               and b"qcom,geni-i2c" in before.get((p, "compatible"), b"").split(b"\0")]
    if len(matches) != 1:
        raise ValueError("expected ACPI I2C2 controller at 0x888000")
    parent = matches[0]
    matches = [p for (p, n), v in before.items()
               if n == "reg" and p.rsplit("/", 1)[0] == parent and v == b"\0\0\0\x39"]
    if len(matches) != 1:
        raise ValueError("expected one existing ALS at address 0x39")
    path = matches[0]
    old = before.get((path, "compatible"))
    if old == b"ams,tcs3430\0":
        return data
    if old != b"avago,apds9960\0":
        raise ValueError("unexpected ALS compatible")
    fdt.resize(len(data) + 512)
    fdt.setprop(fdt.path_offset(path), "compatible", b"ams,tcs3430\0")
    fdt.pack()
    expected = dict(before)
    expected[path, "compatible"] = b"ams,tcs3430\0"
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
