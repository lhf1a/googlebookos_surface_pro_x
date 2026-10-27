#!/usr/bin/env python3
from pathlib import Path
import hashlib, json, shutil, subprocess, struct
ROOT=Path(__file__).resolve().parents[1]
src=ROOT/'private/sq2-vendor-firmware-unpatched'
up=ROOT/'upstream/aarch64-firmware'
out=ROOT/'out/test2/firmware'
out.mkdir(parents=True,exist_ok=True)
manifest=json.loads((src/'provenance.json').read_text())
for item in manifest['files']:
    f=src/item['output']
    assert hashlib.sha256(f.read_bytes()).hexdigest()==item['sha256'],f
shutil.copytree(up/'firmware',out,dirs_exist_ok=True)
# Target device's own verified Windows blobs override the community versions.
for item in manifest['files']:
    f=src/item['output']; d=out/item['output']; d.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(f,d)
# The 2026 Windows ADSP needs 28 MiB; the pinned SPX DT reserves 26 MiB.
# Use the signed community ADSP from the already pinned firmware commit, which
# fits that layout. Never enlarge this region into the adjacent CDSP allocation.
adsp='qcom/msft/surface/pro-x-sq2/qcadsp8180.mbn'
def load_span(path):
    data=path.read_bytes(); off=struct.unpack_from('<I',data,28)[0]
    size,count=struct.unpack_from('<HH',data,42)
    segments=[struct.unpack_from('<8I',data,off+i*size) for i in range(count)]
    segments=[p for p in segments if p[0]==1 and p[5] and (p[6]&0x7000000)!=0x2000000]
    return max((p[3]+p[5]+4095)&~4095 for p in segments)-min(p[3] for p in segments)
assert load_span(up/'firmware'/adsp)<=0x1a00000
shutil.copy2(up/'firmware'/adsp,out/adsp)
(ROOT/'out/test2/adsp-selection.json').write_text(json.dumps({'reason':'Match pinned DT 26 MiB ADSP carveout; Windows blob exceeds it','windows_span':load_span(src/adsp),'selected_span':load_span(out/adsp),'selected_sha256':hashlib.sha256((out/adsp).read_bytes()).hexdigest(),'community_commit':'9f07579ee64aba56419cfd0fbbca9f26741edc90'},indent=2)+'\n')
ath=out/'ath10k/WCN3990/hw1.0'
spec=ROOT/'out/test2/board-spec.json'
spec.write_text(json.dumps([{'data':str(ath/'boards/bdwlan.b58'),'names':['bus=snoc,qmi-board-id=ff,qmi-chip-id=30224']}]))
encoder=up/'scripts/third-party/qca-swiss-army-knife/tools/scripts/ath10k'
subprocess.run(['python3',str(encoder/'ath10k-bdencoder'),'-c',str(spec),'-o',str(ath/'board-2.bin')],check=True)
subprocess.run(['python3',str(encoder/'ath10k-fwencoder'),'--modify','--features=wowlan,mgmt-tx-by-ref,non-bmi,single-chan-info-per-channel',str(ath/'firmware-5.bin')],check=True)
records=[{'path':p.relative_to(out).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(out.rglob('*')) if p.is_file()]
(ROOT/'out/test2/firmware-manifest.json').write_text(json.dumps({'community_commit':'9f07579ee64aba56419cfd0fbbca9f26741edc90','encoder_commit':'29b0a2607f7d1871b8f388c159593fe80113f8a0','windows_inputs':'private/sq2-vendor-firmware-unpatched/provenance.json','files':records},indent=2)+'\n')
print(f'Firmware files: {len(records)}, bytes: {sum(x["bytes"] for x in records)}')
