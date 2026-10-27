#!/usr/bin/env python3
from pathlib import Path
import hashlib, re, subprocess, difflib
ROOT=Path(__file__).resolve().parents[1]
K=Path('/root/.cache/sq2-probe-build/kernel')
p=K/'arch/arm64/boot/dts/qcom/sc8180x.dtsi'
old=p.read_text()
a='adreno_smmu: iommu@2ca0000 {\n\t\t\tcompatible = "qcom,sc8180x-smmu-500", "arm,mmu-500";'
b='adreno_smmu: iommu@2ca0000 {\n\t\t\tcompatible = "qcom,sc8180x-smmu-500", "qcom,adreno-smmu",\n\t\t\t\t     "qcom,smmu-500", "arm,mmu-500";'
assert a in old or b in old
new=old.replace(a,b)
if new!=old:
    (ROOT/'configs/test3-adreno-smmu.patch').write_text(''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/arch/arm64/boot/dts/qcom/sc8180x.dtsi',tofile='b/arch/arm64/boot/dts/qcom/sc8180x.dtsi')))
    p.write_text(new)
cert=ROOT/'downloads/wens.hex'
assert hashlib.sha256(cert.read_bytes()).hexdigest()=='6485f5ddecbecbf73113dc669fe23da07f1a5ab60fb1ad932b0062f9d7fe8753'
(K/'net/wireless/certs/wens.hex').write_bytes(cert.read_bytes())
der=ROOT/'out/test2/wens.der'; pem=ROOT/'out/test2/wens.pem'
der.write_bytes(bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',cert.read_text())))
subprocess.run(['openssl','x509','-inform','DER','-in',str(der),'-out',str(pem)],check=True)
subprocess.run(['openssl','cms','-verify','-binary','-inform','DER','-in','/lib/firmware/regulatory.db.p7s','-content','/lib/firmware/regulatory.db','-nointern','-certfile',str(pem),'-noverify','-out','/dev/null'],check=True)
print('Adreno SMMU DT patch applied; regulatory.db signature verified against pinned upstream wens certificate.')
