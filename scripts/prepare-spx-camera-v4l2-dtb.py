#!/usr/bin/env python3
"""Replace ID-only consumers with front/rear I2C sensors in an offline SPX DT.
CSI endpoints describe the existing sensor-driver modes; the CAMSS receiver
and physical PHY routing still need to be connected before capture.
"""
import argparse
import importlib.util
from pathlib import Path
import libfdt

spec = importlib.util.spec_from_file_location("board", Path(__file__).with_name("prepare-spx-camera-test-dtb.py"))
board = importlib.util.module_from_spec(spec)
spec.loader.exec_module(board)

def prepare(data):
    f = libfdt.Fdt(data)
    before = board.base.properties(f)
    if b"microsoft,surface-pro-x" not in before.get(("/", "compatible"), b"").split(b"\0"):
        raise ValueError("Surface Pro X DT required")
    original_reserved = [f.get_mem_rsv(i) for i in range(f.num_mem_rsv())]
    f.resize(len(data)+8192)
    def prop(path, name):
        return before[path, name]
    def add(path, properties):
        if f.path_offset(path, quiet=(libfdt.NOTFOUND,)) >= 0:
            raise ValueError(f"node already exists: {path}")
        parent, name = path.rsplit("/", 1)
        f.add_subnode(f.path_offset(parent), name)
        for key, value in properties.items():
            f.setprop(f.path_offset(path), key, value)
    added = []
    for name, address, master, compatible, frequency, lanes in [
        ("front", 0x36, 1, "ovti,ov5693", 419200000, (1, 2)),
        ("rear", 0x10, 0, "ovti,ov13858", 540000000, (1, 2, 3, 4))]:
        old = "/camera-id-"+name
        path = f"/soc@0/cci@ac4a000/i2c-bus@{master}/camera@{address:x}"
        fields = {k: prop(old,k) for k in ("power-domains","clocks","reset-gpios","pinctrl-names","pinctrl-0")}
        fields.update({"compatible":board.strings(compatible), "reg":board.cells(address),
                       "assigned-clocks":prop(old,"clocks"), "assigned-clock-rates":board.cells(19200000),
                       "clock-names":board.strings("xvclk" if name=="front" else "xclk"),
                       "orientation":board.cells(0 if name=="front" else 1)})
        if name=="front":
            # This module has one external switched input per its native ACPI.
            fields.update({key+"-supply":prop(old,"iovdd-supply") for key in ("avdd","dovdd","dvdd")})
        else:
            fields.update({key+"-supply":prop(old,key+"-supply") for key in ("iovdd","avdd","dvdd")})
        add(path,fields)
        add(path+"/port",{})
        add(path+"/port/endpoint",{"bus-type":board.cells(4), "clock-lanes":board.cells(0),
             "data-lanes":board.cells(*lanes), "link-frequencies":frequency.to_bytes(8,"big")})
        added.append(path)
    removed=["/camera-id-front","/camera-id-rear","/camera-id-ir"]
    for path in removed:
        f.del_node(f.path_offset(path))
    f.pack()
    after=board.base.properties(f)
    assert all(after.get(k)==v for k,v in before.items()
               if not any(k[0]==p or k[0].startswith(p+"/") for p in removed))
    assert all(k in before or any(k[0]==p or k[0].startswith(p+"/") for p in added) for k in after)
    assert original_reserved == [f.get_mem_rsv(i) for i in range(f.num_mem_rsv())]
    return bytes(f.as_bytearray())

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("input",type=Path); p.add_argument("output",type=Path)
    a=p.parse_args()
    result=prepare(a.input.read_bytes())
    with a.output.open("xb") as out:out.write(result)
