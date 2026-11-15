#!/usr/bin/env python3
"""Prepare SC8180X SLPI/FastRPC resources in an offline DT. Requires EFI PAS12 handoff."""
import argparse
import importlib.util
from pathlib import Path
import struct
import libfdt

spec = importlib.util.spec_from_file_location("dt", Path(__file__).with_name("spx-dt.py"))
dt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dt)


def cells(*v):
    return struct.pack(">" + "I"*len(v), *v)


def strings(*v):
    return b"".join(s.encode()+b"\0" for s in v)


def firmware_span(data):
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01" or struct.unpack_from("<H", data, 18)[0] != 164:
        raise ValueError("ELF32 little-endian Qualcomm DSP firmware required")
    offset = struct.unpack_from("<I", data, 28)[0]
    size, count = struct.unpack_from("<HH", data, 42)
    if size != 32 or offset + size*count > len(data):
        raise ValueError("invalid firmware program headers")
    segments = []
    for i in range(count):
        kind, pos, _, addr, length, memory, flags, _ = struct.unpack_from("<8I", data, offset+i*size)
        if (length and pos+length > len(data)) or (kind == 1 and (memory < length or addr+memory > 2**32)):
            raise ValueError("invalid firmware segment")
        if kind == 1 and memory:
            if not flags & 0x8000000:
                raise ValueError("SLPI relocation requires relocatable LOAD segments")
            segments.append((addr, addr+memory))
    if not segments:
        raise ValueError("no firmware LOAD segments")
    return max(end for _, end in segments) - min(start for start, _ in segments)


def prepare(data, firmware, firmware_name):
    span = firmware_span(firmware)
    if not firmware_name or firmware_name.startswith("/") or ".." in firmware_name.split("/"):
        raise ValueError("relative firmware name required")
    fdt = libfdt.Fdt(data)
    before = dt.properties(fdt)
    if not {b"microsoft,surface-pro-x", b"qcom,sc8180x"} <= set(before.get(("/", "compatible"), b"").split(b"\0")):
        raise ValueError("Surface Pro X / SC8180X required")
    fdt.resize(len(data)+16384)
    maximum = max((int.from_bytes(v,"big") for (_,n),v in before.items()
                   if n in ("phandle","linux,phandle")), default=0)
    def find(compatible, reg=None):
        paths = [p for (p,n),v in before.items() if n=="compatible" and compatible.encode() in v.split(b"\0")]
        if reg is not None:
            paths = [p for p in paths if before.get((p,"reg")) == reg]
        if len(paths)!=1:
            raise ValueError(f"expected one {compatible}")
        return paths[0]
    def ref(path):
        nonlocal maximum
        n=fdt.path_offset(path)
        value=fdt.get_phandle(n)
        if not value:
            maximum+=1
            if maximum>=0xffffffff: raise ValueError("no unused phandle")
            value=maximum
            fdt.setprop_u32(n,"phandle",value)
        return value
    def add(path, props):
        if fdt.path_offset(path, quiet=(libfdt.NOTFOUND,))>=0:
            raise ValueError(f"node already exists: {path}")
        parent,name=path.rsplit("/",1)
        fdt.add_subnode(fdt.path_offset(parent or "/"),name)
        for key,value in props.items(): fdt.setprop(fdt.path_offset(path),key,value)
    # This board already reserves SCSS independently of ADSP/CDSP/MPSS.
    regions = [p for (p,n),v in before.items() if n=="reg" and p.startswith("/reserved-memory/")
               and v==cells(0,0x99000000,0,0x1400000)]
    if len(regions)!=1 or span>0x1400000:
        raise ValueError("firmware does not fit the existing 20 MiB SCSS reservation")
    memory=regions[0]
    if (memory,"no-map") not in before: raise ValueError("SCSS must be no-map")
    existing=before.get((memory,"phandle"))
    if existing and any(existing in [v[i:i+4] for i in range(0,len(v),4)]
                        for (_,n),v in before.items() if n=="memory-region"):
        raise ValueError("SCSS reservation already has a consumer")
    for (p,n),v in before.items():
        if n=="reg" and p.startswith("/reserved-memory/") and p!=memory and len(v)==16:
            start,length=struct.unpack(">QQ",v)
            if start < 0x9a400000 and start+length > 0x99000000:
                raise ValueError(f"overlapping reservation: {p}")
    gic=ref(find("arm,gic-v3"))
    rpmh=ref(find("qcom,sc8180x-rpmh-clk"))
    pd=ref(find("qcom,sc8180x-rpmhpd"))
    qmp=ref(find("qcom,sc8180x-aoss-qmp"))
    mailbox=ref(find("qcom,sc8180x-apss-shared"))
    smmu=ref(find("qcom,sc8180x-smmu-500",cells(0,0x15000000,0,0x100000)))
    inbound=ref("/smp2p-slpi/slave-kernel")
    outbound=ref("/smp2p-slpi/master-kernel")
    node=find("qcom,sc8180x-adsp-pas").rsplit("/",1)[0]+"/remoteproc@2400000"
    add(node, {
        "compatible":strings("qcom,sc8180x-slpi-pas"), "reg":cells(0,0x2400000,0,0x4040),
        "interrupts-extended":cells(gic,0,494,1,inbound,0,1,inbound,1,1,inbound,2,1,inbound,3,1),
        "interrupt-names":strings("wdog","fatal","ready","handover","stop-ack"),
        "clocks":cells(rpmh,0), "clock-names":strings("xo"),
        "power-domains":cells(pd,4,pd,5), "power-domain-names":strings("lcx","lmx"),
        "qcom,qmp":cells(qmp), "qcom,smem-states":cells(outbound,0),
        "qcom,smem-state-names":strings("stop"), "memory-region":cells(ref(memory)),
        "firmware-name":strings(firmware_name), "status":strings("okay"),
        "microsoft,el2-preboot":b"", "microsoft,el2-preboot-complete":cells(0),
        "microsoft,el2-preboot-status":cells(0,0,0,0,0,0), "qcom,broken-reset":b""})
    add(node+"/glink-edge", {
        "interrupt-parent":cells(gic), "interrupts":cells(0,170,1),
        "label":strings("dsps"), "qcom,remote-pid":cells(3), "mboxes":cells(mailbox,24)})
    rpc=node+"/glink-edge/fastrpc"
    add(rpc, {"compatible":strings("qcom,fastrpc"),
        "qcom,glink-channels":strings("fastrpcglink-apps-dsp"), "label":strings("sdsp"),
        "qcom,non-secure-domain":b"", "#address-cells":cells(1), "#size-cells":cells(0)})
    for i in (1,2,3):
        add(rpc+f"/compute-cb@{i}", {"compatible":strings("qcom,fastrpc-compute-cb"),
            "reg":cells(i), "iommus":cells(smmu,0x5a0+i,0)})
    fdt.pack()
    after=dt.properties(fdt)
    assert all(after[k]==v for k,v in before.items()), "existing property changed"
    original=libfdt.Fdt(data)
    assert [fdt.get_mem_rsv(i) for i in range(fdt.num_mem_rsv())]==[
        original.get_mem_rsv(i) for i in range(original.num_mem_rsv())]
    return bytes(fdt.as_bytearray())


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("input",type=Path)
    p.add_argument("firmware",type=Path)
    p.add_argument("output",type=Path)
    p.add_argument("--firmware-name",default="qcom/msft/surface/pro-x/qcslpi8180.mbn")
    a=p.parse_args()
    result=prepare(a.input.read_bytes(),a.firmware.read_bytes(),a.firmware_name)
    with a.output.open("xb") as out: out.write(result)
