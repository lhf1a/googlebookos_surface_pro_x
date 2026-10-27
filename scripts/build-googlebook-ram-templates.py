#!/usr/bin/env python3
"""Create sparse development files, never raw physical disks."""
from pathlib import Path
import os,gzip,struct,subprocess,json,hashlib,importlib.util
r=Path(__file__).resolve().parents[1]
cache=Path('/root/.cache/sq2-probe-build/googlebook');out=r/'out/android-test'
spec=importlib.util.spec_from_file_location('inspect_recovery',r/'scripts/inspect-recovery.py')
mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
raw=r/'downloads/mica-recovery.raw';layout=mod.inspect(raw)
def pack(name,size,chunks):
    dest=out/(name+'.sparse.gz')
    with dest.open('wb') as f,gzip.GzipFile(fileobj=f,mode='wb',mtime=0) as z:
        z.write(b'SQ2SPRS1'+struct.pack('<Q',size))
        for off,data in chunks:
            for i in range(0,len(data),65536):
                b=data[i:i+65536]
                if any(b):z.write(struct.pack('<QI',off+i,len(b))+b)
        z.write(struct.pack('<QI',0,0))
    return {'file':dest.name,'logical_bytes':size,'bytes':dest.stat().st_size,'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()}
data=cache/'native-userdata-template.ext4'
with data.open('wb') as f:f.truncate(2*1024**3)
subprocess.run(['mke2fs','-q','-F','-t','ext4','-b','4096','-O','extent,64bit,metadata_csum,casefold,encrypt,verity','-E','encoding=utf8,lazy_itable_init=0,lazy_journal_init=0','-L','SQ2RAMDATA',str(data)],check=True)
def allocated():
    with data.open('rb') as f:
        off=0
        while off<data.stat().st_size:
            try:off=os.lseek(f.fileno(),off,os.SEEK_DATA)
            except OSError as e:
                if e.errno==6:break
                raise
            end=os.lseek(f.fileno(),off,os.SEEK_HOLE)
            f.seek(off);yield off,f.read(end-off);off=end
records=[pack('userdata',data.stat().st_size,allocated())]
chunks=[]
with raw.open('rb') as f:
    for off,size in [(0,1048576),(layout['image_bytes']-1048576,1048576)]+[(p['offset'],p['bytes']) for p in layout['partitions'] if p['name'] in ['vbmeta_a','vbmeta_b']]:
        f.seek(off);chunks.append((off,f.read(size)))
records.append(pack('bootmeta',layout['image_bytes'],chunks))
parts={p['name']:p for p in layout['partitions']}
# Partition aliases point to RAM loop slices; no automatic partition scan.
(out/'bootmeta-layout').write_text(''.join(f"{name} {parts[name]['index']} {parts[name]['offset']} {parts[name]['bytes']}\n" for name in ['vbmeta_a','vbmeta_b','misc']))
(out/'ram-template-manifest.json').write_text(json.dumps(records,indent=2)+'\n')
print(json.dumps(records,indent=2))
