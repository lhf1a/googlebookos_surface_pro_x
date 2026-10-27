#!/usr/bin/env python3
"""Fetch reviewed AOSP generic legacy HAL and its public ABI headers."""
from pathlib import Path
from urllib.request import urlopen
from concurrent.futures import ThreadPoolExecutor
import base64, hashlib, json
root=Path(__file__).resolve().parents[1]
out=root/'out/connectivity/source'
out.mkdir(parents=True,exist_ok=True)
trees={
 'goldfish':'https://android.googlesource.com/device/generic/goldfish/+/37eb7d78f252d851dc5021d0d24d72c262049cc5/wifi/wifi_hal/',
 'headers':'https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/wifi/legacy_headers/include/hardware_legacy/',
}
jobs=[]
for folder,url in trees.items():
    with urlopen(url+'?format=JSON',timeout=30) as r: tree=json.loads(r.read()[4:])
    for e in tree['entries']:
        if e['type']=='blob' and (e['name'].endswith(('.h','.cpp')) or e['name']=='Android.bp'):
            jobs.append((folder,e['name'],url+e['name']+'?format=TEXT'))
def get(job):
    folder,name,url=job
    with urlopen(url,timeout=30) as r: data=base64.b64decode(r.read())
    p=out/folder/name; p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes(data)
    return {'path':str(p.relative_to(out)),'url':url,'sha256':hashlib.sha256(data).hexdigest()}
with ThreadPoolExecutor(max_workers=4) as pool: records=list(pool.map(get,jobs))
(out/'manifest.json').write_text(json.dumps(records,indent=2)+'\n')
print('Fetched',len(records),'AOSP sources and headers')
