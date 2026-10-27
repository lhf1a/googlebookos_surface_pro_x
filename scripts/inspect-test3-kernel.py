from pathlib import Path
import struct
B=Path('/root/.cache/sq2-probe-build/kernel')
root=Path(__file__).resolve().parents[1]
for name in ['qcadsp8180.mbn','qccdsp8180.mbn','qcmpss8180.mbn']:
    data=(root/'out/test2/firmware/qcom/msft/surface/pro-x-sq2'/name).read_bytes()
    phoff=struct.unpack_from('<I',data,28)[0]; size,n=struct.unpack_from('<HH',data,42)
    print(name,[(hex(p[3]),hex(p[5]),hex(p[6]),bool(p[6]&0x8000000)) for i in range(n) if (p:=struct.unpack_from('<8I',data,phoff+i*size))[0]==1])
for path,start,end in [('drivers/soc/qcom/mdt_loader.c',205,258),('net/wireless/Makefile',1,60)]:
    print(path);print('\n'.join((B/path).read_text().splitlines()[start-1:end]))
