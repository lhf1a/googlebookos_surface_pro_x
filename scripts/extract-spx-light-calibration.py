#!/usr/bin/env python3
"""Extract the model-default TCS3430 calibration from the supplied Surface driver."""
import argparse,struct,zlib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('driver',type=Path)
p.add_argument('output',type=Path)
a=p.parse_args()
data=a.driver.read_bytes()
signature=struct.pack('<6I',3,5,4,4,5,16)
matches=[]
pos=0
while (pos:=data.find(signature,pos))>=0:
    start=pos-28;pos+=1
    blob=data[start:start+1480] if start>=0 else b''
    if len(blob)!=1480:continue
    if (zlib.crc32(blob[:1476])^0xffffffff)==struct.unpack_from('<I',blob,1476)[0]:
        matches.append(blob)
if len(matches)!=1: p.error('expected exactly one checked model-default calibration')
with a.output.open('xb') as f:f.write(matches[0])
print('wrote model-default calibration; this is not unit-specific lux calibration')
