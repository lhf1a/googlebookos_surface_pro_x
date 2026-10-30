#!/usr/bin/env python3
"""Experimental mode matrix, software EDID only; not a bandwidth certification.

Retain native65 preferred, add non-native60 control and two non-native70 modes.
No EEPROM writes. A control failure invalidates bandwidth-limit conclusions.
"""
import argparse,hashlib,json
from pathlib import Path

def dtd(width,height,hz,template):
    hblank,vblank=120,55
    units=((width+hblank)*(height+vblank)*hz)//10000
    b=bytearray(template)
    b[0:2]=units.to_bytes(2,'little')
    b[2]=width&255;b[3]=hblank&255;b[4]=((width>>8)<<4)|(hblank>>8)
    b[5]=height&255;b[6]=vblank&255;b[7]=((height>>8)<<4)|(vblank>>8)
    return bytes(b)

def decode(b):
    width=b[2]+((b[4]>>4)<<8);hblank=b[3]+((b[4]&15)<<8)
    height=b[5]+((b[7]>>4)<<8);vblank=b[6]+((b[7]&15)<<8)
    clock=int.from_bytes(b[:2],'little')*10000
    return dict(width=width,height=height,htotal=width+hblank,vtotal=height+vblank,pixel_clock_khz=clock//1000,hz=clock/((width+hblank)*(height+vblank)),rgb24_budget_gbps=clock*24/1e9)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('output',type=Path)
    a=parser.parse_args();old=a.source.read_bytes()
    assert hashlib.sha256(old).hexdigest()=='83101509b096d6ddfc83e96bd08917d4989f78b80e2549489bd12f8d14e5a014'
    assert len(old)==128 and sum(old)%256==0 and old[126]==0
    assert old[24]&2,'Native first DTD must remain preferred'
    assert not a.output.exists() and a.source.resolve()!=a.output.resolve()
    new=bytearray(old);template=old[54:72]
    for offset,mode in [(72,(2560,1706,60)),(90,(2560,1706,70)),(108,(2700,1800,70))]:
        new[offset:offset+18]=dtd(*mode,template)
    new[127]=(-sum(new[:127]))&255
    assert sum(new)%256==0 and new[:72]==old[:72] and new[126]==old[126]
    with a.output.open('xb') as f:f.write(new)
    assert a.output.read_bytes()==new and a.source.read_bytes()==old
    print(json.dumps(dict(source_sha256=hashlib.sha256(old).hexdigest(),output_sha256=hashlib.sha256(new).hexdigest(),modes=[decode(new[i:i+18]) for i in (54,72,90,108)],native65_preferred=True,software_override_only=True,original_descriptors_replaced_with_test_timings=True),indent=2))

if __name__=='__main__':main()
