#!/usr/bin/env python3
"""Copy original AVB slot metadata for the USB bring-up's read-only loop devices."""
from pathlib import Path
import json,hashlib
r=Path(__file__).resolve().parents[1]
layout=json.loads((r/'out/googlebook-layout.json').read_text())
assert layout['gpt_verified']
out=r/'out/googlebook-stage'
records=[]
with (r/'downloads/mica-recovery.raw').open('rb') as f:
    for name in ['vbmeta_a','vbmeta_b']:
        p=next(p for p in layout['partitions'] if p['name']==name)
        f.seek(p['offset']);data=f.read(p['bytes'])
        assert len(data)==p['bytes'] and data[:4]==b'AVB0'
        (out/(name+'.img')).write_bytes(data)
        records.append({'file':name+'.img','offset':p['offset'],'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
(out/'slot-metadata.json').write_text(json.dumps(records,indent=2)+'\n')
print(json.dumps(records))
