#!/usr/bin/env python3
"""Initial SC8180X front/rear CAMSS DT, using IFE0 and the native camera IORT SIDs.
Front PHY0 and rear PHY2 have produced RAW10 frames on SQ2.
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import libfdt

spec=importlib.util.spec_from_file_location("base",Path(__file__).with_name("prepare-spx-camera-dtb.py"))
base=importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)

def prepare(data):
    f=libfdt.Fdt(data)
    before=base.properties(f)
    if b"microsoft,surface-pro-x" not in before["/","compatible"].split(b"\0"):
        raise ValueError("Surface Pro X DT required")
    if f.path_offset("/soc@0/isp@acb3000",quiet=(libfdt.NOTFOUND,))>=0:
        raise ValueError("CAMSS node already exists")
    reserved=[f.get_mem_rsv(i) for i in range(f.num_mem_rsv())]
    refs=[
     ('/soc@0/clock-controller@ad00000','qcom,sc8180x-camcc'),
     ('/soc@0/clock-controller@100000','qcom,gcc-sc8180x'),
     ('/soc@0/interrupt-controller@17a00000','arm,gic-v3'),
     ('/soc@0/iommu@15000000','qcom,sc8180x-smmu-500'),
     ('/soc@0/interconnect@9680000','qcom,sc8180x-gem-noc'),
     ('/soc@0/interconnect@1500000','qcom,sc8180x-config-noc'),
     ('/soc@0/interconnect@1740000','qcom,sc8180x-mmss-noc'),
     ('/interconnect-mc-virt','qcom,sc8180x-mc-virt')]
    # Native camera AVS IORT has these exact 16 stream IDs; do not copy SM8150 SID masks.
    sids=[base+0x20*i for base in (0xa00,0xe00) for i in range(8)]
    reg=[(0xacb3000,0x1000)]+[(0xac65000+0x1000*i,0x1000) for i in range(4)]+[(0xacaf000,0x4000)]
    names=['csid0']+['csiphy%d'%i for i in range(4)]+['vfe0']
    irq=[464,477,478,479,448,465]
    clocks=[(1,242,'gcc_ahb'),(1,9,'gcc_axi_hf'),(0,5,'camnoc_axi'),(0,17,'cpas_ahb'),(0,125,'slow_ahb_src')]
    clocks += [(0,27+i,'csiphy%d'%i) for i in range(4)]+[(0,19+2*i,'csiphy%d_timer'%i) for i in range(4)]
    clocks += [(0,39,'vfe0'),(0,38,'vfe0_axi'),(0,41,'vfe0_cphy_rx'),(0,42,'vfe0_csid')]
    def ref(i):
        path,compatible=refs[i]
        node=f.path_offset(path)
        if compatible.encode() not in bytes(f.getprop(node,"compatible")).split(b"\0"):
            raise ValueError("incompatible provider: "+path)
        phandle=f.get_phandle(node)
        if not phandle:
            raise ValueError("provider requires phandle: "+path)
        return hex(phandle)
    def strings(v):return ', '.join('"'+x+'"' for x in v)
    s='''/dts-v1/;
    /plugin/;
    / {
     fragment@0 { target-path = "/soc@0"; __overlay__ {
      #address-cells = <2>; #size-cells = <2>;
      isp@acb3000 {
       compatible = "qcom,sc8180x-camss";
    '''
    s+='   reg = '+', '.join('<0 0x%x 0 0x%x>'%v for v in reg)+';\n'
    s+='   reg-names = '+strings(names)+';\n'
    s+='   interrupt-parent = <'+ref(2)+'>;\n'
    s+='   interrupts = '+', '.join('<0 %d 1>'%v for v in irq)+';\n'
    s+='   interrupt-names = '+strings(names)+';\n'
    s+='   clocks = '+', '.join('<%s %d>'%(ref(r),id) for r,id,_ in clocks)+';\n'
    s+='   clock-names = '+strings([v[2] for v in clocks])+';\n'
    s+='   power-domains = <%s 1>, <%s 7>;\n   power-domain-names = "ife0", "top";\n'%(ref(0),ref(0))
    s+='   interconnects = <%s 0 0 %s 9 0>, <%s 1 0 %s 1 0>;\n'%(ref(4),ref(5),ref(6),ref(7))
    s+='   interconnect-names = "ahb", "hf_0";\n'
    s+='   iommus = '+', '.join('<%s 0x%x 0>'%(ref(3),sid) for sid in sids)+';\n'
    s+='''   ports {
        #address-cells = <1>; #size-cells = <0>;
        port@2 { reg = <2>; rear_in: endpoint { remote-endpoint = <&rear_out>; clock-lanes = <7>; data-lanes = <0 1 2 3>; }; };
        port@0 { reg = <0>; front_in: endpoint { remote-endpoint = <&front_out>; clock-lanes = <7>; data-lanes = <0 1>; }; };
       };
      };
     }; };
     fragment@1 { target-path = "/soc@0/cci@ac4a000/i2c-bus@0/camera@10/port/endpoint";
      rear_out: __overlay__ { remote-endpoint = <&rear_in>; };
     };
     fragment@2 { target-path = "/soc@0/cci@ac4a000/i2c-bus@1/camera@36/port/endpoint";
      front_out: __overlay__ { remote-endpoint = <&front_in>; };
     };
    };
    '''
    with tempfile.TemporaryDirectory(prefix="camss-dt-") as temp:
        p=Path(temp)
        (p/"overlay.dts").write_text(s)
        subprocess.run(["dtc","-@","-I","dts","-O","dtb","-o",str(p/"overlay.dtbo"),str(p/"overlay.dts")],check=True,capture_output=True)
        overlay=libfdt.Fdt((p/"overlay.dtbo").read_bytes())
        overlay.del_node(overlay.path_offset("/__symbols__"))
        overlay.pack()
        f.resize(len(data)+32768)
        ret=libfdt.fdt_overlay_apply(f._fdt,overlay._fdt)
        if ret:
            raise ValueError("overlay apply failed: "+libfdt.strerror(ret))
    f.pack()
    after=base.properties(f)
    assert all(after.get(k)==v for k,v in before.items())
    assert reserved==[f.get_mem_rsv(i) for i in range(f.num_mem_rsv())]
    for sensor,phy in [("/soc@0/cci@ac4a000/i2c-bus@0/camera@10",2),
                       ("/soc@0/cci@ac4a000/i2c-bus@1/camera@36",0)]:
        output=f.path_offset(sensor+"/port/endpoint")
        input=f.path_offset("/soc@0/isp@acb3000/ports/port@%d/endpoint"%phy)
        assert f.get_phandle(output)==int.from_bytes(f.getprop(input,"remote-endpoint"),"big")
        assert f.get_phandle(input)==int.from_bytes(f.getprop(output,"remote-endpoint"),"big")
    return bytes(f.as_bytearray())

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input",type=Path)
    parser.add_argument("output",type=Path)
    args=parser.parse_args()
    result=prepare(args.input.read_bytes())
    with args.output.open("xb") as stream:
        stream.write(result)
