#!/usr/bin/env python3
"""Give the existing SC8180X CAMCC a phandle for camera consumers.
Only edits an offline DTB; does not enable a sensor or write the device.
"""
import argparse
from pathlib import Path
import libfdt


def properties(fdt):
    result = {}
    offset, depth = -1, 0
    while True:
        offset, depth = fdt.next_node(offset, depth)
        if offset < 0 or depth < 0:
            break
        path = fdt.get_path(offset)
        prop = fdt.first_property_offset(offset, quiet=(libfdt.NOTFOUND,))
        while prop >= 0:
            value = fdt.get_property_by_offset(prop)
            result[path, value.name] = bytes(value)
            prop = fdt.next_property_offset(prop, quiet=(libfdt.NOTFOUND,))
    return result


def prepare(data):
    fdt = libfdt.Fdt(data)
    before = properties(fdt)
    boards = before.get(("/", "compatible"), b"").split(b"\0")
    if b"microsoft,surface-pro-x" not in boards or b"qcom,sc8180x" not in boards:
        raise ValueError("Surface Pro X / SC8180X DT required")
    matches = [path for (path, name), value in before.items()
               if name == "compatible" and b"qcom,sc8180x-camcc" in value.split(b"\0")]
    if len(matches) != 1:
        raise ValueError("expected one existing SC8180X CAMCC")
    path = matches[0]
    if fdt.get_phandle(fdt.path_offset(path)):
        return data
    maximum = max((int.from_bytes(value, "big") for (_, name), value in before.items()
                   if name in ("phandle", "linux,phandle")), default=0)
    if maximum >= 0xfffffffe:
        raise ValueError("no unused phandle")
    fdt.resize(len(data) + 1024)
    fdt.setprop_u32(fdt.path_offset(path), "phandle", maximum + 1)
    fdt.pack()
    after = properties(fdt)
    expected = dict(before)
    expected[path, "phandle"] = (maximum + 1).to_bytes(4, "big")
    assert after == expected, "unexpected DT property changes"
    original = libfdt.Fdt(data)
    assert fdt.num_mem_rsv() == original.num_mem_rsv()
    assert all(fdt.get_mem_rsv(i) == original.get_mem_rsv(i)
               for i in range(fdt.num_mem_rsv())), "reserved memory changed"
    return bytes(fdt.as_bytearray())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = prepare(args.input.read_bytes())
    with args.output.open("xb") as output:
        output.write(result)
    print(f"CAMCC reference prepared: {args.output}; no camera enabled")
