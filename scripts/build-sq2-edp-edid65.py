#!/usr/bin/env python3
"""Build a software-only native65/70 EDID override from the verified Pro-X EDID.

This changes no EEPROM/driver/physical panel capability. Activate only through
the isolated eDP-1 boot entry. Original60 fallback must omit edid_firmware.
"""
from pathlib import Path
import argparse,hashlib,json
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('source',type=Path)
parser.add_argument('target',type=Path)
parser.add_argument('--hz',type=int,choices=(65,70),default=65)
args=parser.parse_args()
source,target=args.source,args.target
b=source.read_bytes()
assert len(b)==128 and sum(b)%256==0 and b[126]==0
assert b[8:12]==bytes.fromhex('30e45505') and b[113:126]==b'LP129WT112684'
assert b[54:56]==(35550).to_bytes(2,'little')
assert b[56:72]==bytes.fromhex('4078b08037703020550412b710000019')
assert b[72:77]==bytes.fromhex('000000fd00') and b[78]==60 and b[81]==36
assert not target.exists() and source.resolve()!=target.resolve()
new=bytearray(b)
# Base EDID clock units are10kHz. Preserve all original porches/polarities.
clock_units=(3000*1975*args.hz)//10000
pixel_khz=clock_units*10
new[54:56]=clock_units.to_bytes(2,'little')
new[78]=args.hz
new[81]=(pixel_khz+9999)//10000
new[127]=(-sum(new[:127]))&255
assert sum(new)%256==0
changes=[i for i,(a,c) in enumerate(zip(b,new)) if a!=c]
assert set(changes)<={54,55,78,81,127}
with target.open('xb') as f:f.write(new)
assert target.read_bytes()==new and source.read_bytes()==b
print(json.dumps(dict(source_sha256=hashlib.sha256(b).hexdigest(),output_sha256=hashlib.sha256(new).hexdigest(),bytes=128,changed_offsets=changes,pixel_clock_khz=pixel_khz,nominal_hz=args.hz,computed_hz=pixel_khz*1000/(3000*1975),software_override_only=True),indent=2))
