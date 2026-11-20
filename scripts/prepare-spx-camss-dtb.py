#!/usr/bin/env python3
"""SC8180X front/rear/IR multi-IFE CAMSS DT with native IORT SIDs and privacy LEDs.
Exposes IFE0/CSID0, IFE1/CSID1, IFE_LITE0/CSID_LITE0, and IFE_LITE1/CSID_LITE1
with CSIPHY0 (OV5693), CSIPHY2 (OV13858), and CSIPHY3 (OV7251 IR).
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
     ('/interconnect-mc-virt','qcom,sc8180x-mc-virt'),
     ('/soc@0/pinctrl@3100000','qcom,sc8180x-tlmm')]
    # Native camera AVS IORT has these exact 16 stream IDs; do not copy SM8150 SID masks.
    sids=[base_sid+0x20*i for base_sid in (0xa00,0xe00) for i in range(8)]
    reg=[
        (0xacb3000,0x1000),(0xacba000,0x1000),(0xacc8000,0x1000),(0xaccf000,0x1000),
        (0xac65000,0x1000),(0xac66000,0x1000),(0xac67000,0x1000),(0xac68000,0x1000),
        (0xacaf000,0x4000),(0xacb6000,0x4000),(0xacc4000,0x4000),(0xaccb000,0x4000),
    ]
    names=[
        'csid0','csid1','csid_lite0','csid_lite1',
        'csiphy0','csiphy1','csiphy2','csiphy3',
        'vfe0','vfe1','vfe_lite0','vfe_lite1',
    ]
    irq=[
        464,466,468,359,
        477,478,479,448,
        465,467,469,360,
    ]
    clocks=[
        (1,242,'gcc_ahb'),(1,9,'gcc_axi_hf'),(1,10,'gcc_axi_sf'),
        (0,5,'camnoc_axi'),(0,7,'camnoc_dcd_xo'),(0,17,'cpas_ahb'),(0,125,'slow_ahb_src'),
    ]
    clocks += [(0,27+i,'csiphy%d'%i) for i in range(4)]+[(0,19+2*i,'csiphy%d_timer'%i) for i in range(4)]
    clocks += [
        (0,39,'vfe0'),(0,38,'vfe0_axi'),(0,41,'vfe0_cphy_rx'),(0,42,'vfe0_csid'),(0,44,'vfe0_dsp'),
        (0,46,'vfe1'),(0,45,'vfe1_axi'),(0,48,'vfe1_cphy_rx'),(0,49,'vfe1_csid'),(0,51,'vfe1_dsp'),
        (0,66,'vfe_lite0'),(0,68,'vfe_lite0_cphy_rx'),(0,69,'vfe_lite0_csid'),
        (0,71,'vfe_lite1'),(0,73,'vfe_lite1_cphy_rx'),(0,74,'vfe_lite1_csid'),
    ]
    has_ir = f.path_offset("/soc@0/cci@ac4a000/i2c-bus@0/camera@60/port/endpoint", quiet=(libfdt.NOTFOUND,)) >= 0
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
    s+='   power-domains = <%s 1>, <%s 2>, <%s 7>;\n   power-domain-names = "ife0", "ife1", "top";\n'%(ref(0),ref(0),ref(0))
    s+='   interconnects = <%s 0 0 %s 9 0>, <%s 1 0 %s 1 0>, <%s 3 0 %s 1 0>, <%s 2 0 %s 1 0>;\n'%(
        ref(4),ref(5),ref(6),ref(7),ref(6),ref(7),ref(6),ref(7))
    s+='   interconnect-names = "ahb", "hf_0", "sf_0", "hf_1";\n'
    s+='   iommus = '+', '.join('<%s 0x%x 0>'%(ref(3),sid) for sid in sids)+';\n'
    s+='   privacy-led-front-gpios = <%s 25 0>;\n'%(ref(8),)
    s+='   privacy-led-rear-gpios = <%s 11 0>;\n'%(ref(8),)
    s+='''   ports {
        #address-cells = <1>; #size-cells = <0>;
        port@2 { reg = <2>; rear_in: endpoint { remote-endpoint = <&rear_out>; clock-lanes = <7>; data-lanes = <0 1 2 3>; }; };
        port@0 { reg = <0>; front_in: endpoint { remote-endpoint = <&front_out>; clock-lanes = <7>; data-lanes = <0 1>; }; };
    '''
    if has_ir:
        s+='    port@3 { reg = <3>; ir_in: endpoint { remote-endpoint = <&ir_out>; clock-lanes = <7>; data-lanes = <0>; }; };\n'
    s+='''   };
      };
     }; };
     fragment@1 { target-path = "/soc@0/cci@ac4a000/i2c-bus@0/camera@10/port/endpoint";
      rear_out: __overlay__ { remote-endpoint = <&rear_in>; };
     };
     fragment@2 { target-path = "/soc@0/cci@ac4a000/i2c-bus@1/camera@36/port/endpoint";
      front_out: __overlay__ { remote-endpoint = <&front_in>; };
     };
    '''
    if has_ir:
        s+=''' fragment@3 { target-path = "/soc@0/cci@ac4a000/i2c-bus@0/camera@60/port/endpoint";
      ir_out: __overlay__ { remote-endpoint = <&ir_in>; };
     };
    '''
    s+='};\n'
    with tempfile.TemporaryDirectory(prefix="camss-dt-") as temp:
        p=Path(temp)
        (p/"overlay.dts").write_text(s)
        subprocess.run(["dtc","-@","-I","dts","-O","dtb","-o",str(p/"overlay.dtbo"),str(p/"overlay.dts")],check=True,capture_output=True)
        overlay=libfdt.Fdt((p/"overlay.dtbo").read_bytes())
        overlay.del_node(overlay.path_offset("/__symbols__"))
        overlay.pack()
        f.resize(len(data)+65536)
        ret=libfdt.fdt_overlay_apply(f._fdt,overlay._fdt)
        if ret:
            raise ValueError("overlay apply failed: "+libfdt.strerror(ret))
    f.pack()
    after=base.properties(f)
    assert all(after.get(k)==v for k,v in before.items())
    assert reserved==[f.get_mem_rsv(i) for i in range(f.num_mem_rsv())]
    checks=[("/soc@0/cci@ac4a000/i2c-bus@0/camera@10",2),
            ("/soc@0/cci@ac4a000/i2c-bus@1/camera@36",0)]
    if has_ir:
        checks.append(("/soc@0/cci@ac4a000/i2c-bus@0/camera@60",3))
    for sensor,phy in checks:
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
