#!/usr/bin/env python3
"""Build-only VIDEOCC reference fix; preserve every unrelated effective entry."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import zlib

ROOT=Path(__file__).resolve().parents[1]
OLD_GUARD=b'ph=$(od -An -tx1 /sys/firmware/devicetree/base/soc@0/clock-controller@ab00000/phandle | tr -d \' \\n\')\ntest "$ph" = 000000b5\n'
NEW_GUARD=b'# HFI overlay resolves the live provider; no numeric phandle startup gate.\n'
PAS_MARK=b'cat /proc/sys/kernel/random/boot_id > /tmp/sq2-native-pas-attempted\n'
CORE=b'insmod venus-core.ko sq2_platform_allow=1 sq2_no_tz=1\n'


def patch_helper(helper):
    if helper.count(OLD_GUARD)==1:
        helper=helper.replace(OLD_GUARD,NEW_GUARD)
    elif helper.count(NEW_GUARD)!=1:
        raise ValueError('Unknown VIDEOCC guard; refuse unsafe helper rewrite')
    if CORE in helper:
        old=CORE+b'insmod venus-dec.ko\n'+PAS_MARK
        new=PAS_MARK+CORE+b'insmod venus-dec.ko\n'
        if helper.count(old)==1:helper=helper.replace(old,new)
        elif helper.count(new)!=1:raise ValueError('Unknown no-TZ first-start marker order')
    if b'RESIDENT_REFUSED=' not in helper or b'test ! -e /tmp/sq2-native-pas-attempted' not in helper:
        raise ValueError('Resident/no-retry guard missing')
    subprocess.run(['sh','-n'],input=helper,check=True)
    return helper


def integrate(raw,module,cpio):
    before=cpio.entries(gzip.decompress(raw));entries=dict(before)
    module_path='etc/sq2-video-native/sq2_hfi_overlay.ko'
    manifest_path='etc/sq2-video-native/SHA256SUMS'
    helper_path='bin/video-decoder-lab-start'
    digest=hashlib.sha256(module).hexdigest()
    rows=before[manifest_path][1].decode().splitlines();new_rows=[];seen=set()
    for row in rows:
        expected,name=row.split()
        if Path(name).name!=name or name in seen:raise ValueError('Unsafe or duplicate native manifest entry')
        seen.add(name);path='etc/sq2-video-native/'+name
        if hashlib.sha256(before[path][1]).hexdigest()!=expected:raise ValueError('Original native manifest mismatch: '+name)
        new_rows.append((digest if path==module_path else expected)+'  '+name+'\n')
    if 'sq2_hfi_overlay.ko' not in seen:raise ValueError('HFI overlay absent from manifest')
    entries[module_path]=(before[module_path][0],module)
    entries[manifest_path]=(before[manifest_path][0],''.join(new_rows).encode())
    entries[helper_path]=(before[helper_path][0],patch_helper(before[helper_path][1]))
    rest=raw;identity=None
    while rest:
        d=zlib.decompressobj(31);member_entries=cpio.entries(d.decompress(rest))
        if not d.eof:raise ValueError('Incomplete gzip member')
        if 'etc/shadow' in member_entries and 'bin/dropbear' in member_entries:
            identity=rest[:len(rest)-len(d.unused_data)]
        rest=d.unused_data
    if identity is None:raise ValueError('Credential member absent')
    body=b''.join(cpio.entry(n,b,m) for n,(m,b) in sorted(entries.items(),key=lambda row:(row[0].count('/'),row[0])))
    body+=cpio.entry('TRAILER!!!',b'',0);body+=b'\0'*(-len(body)%512)
    result=gzip.compress(body,compresslevel=3,mtime=0)+identity
    after=cpio.entries(gzip.decompress(result))
    if after!=entries:raise ValueError('Last-entry audit failed')
    changed=sorted(n for n in after if after[n]!=before.get(n))
    allowed={helper_path,module_path,manifest_path}
    if set(changed)-allowed:raise ValueError('Unrelated runtime change')
    return result,dict(changed=changed,overlay_sha256=digest,credential_member_last=True,
                       unrelated_entries_byte_identical=True,kernel_and_firmware_unchanged=True,
                       dynamic_videocc_identity_checked=True,hardware_tested=False)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('base',type=Path);p.add_argument('module',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--expected-base-sha256',required=True);p.add_argument('--module-sha256',required=True)
    a=p.parse_args();raw=a.base.read_bytes();module=a.module.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=a.expected_base_sha256:p.error('Original runtime hash mismatch')
    if hashlib.sha256(module).hexdigest()!=a.module_sha256:p.error('Module hash mismatch')
    if b'SQ2_HFI_VIDEOCC' not in module:p.error('Not the runtime VIDEOCC binding module')
    for field,expected in [('name','sq2_hfi_overlay'),('vermagic',__import__('kernel_baseline').policy()['kernel_release'])]:
        value=subprocess.check_output(['modinfo','-F',field,str(a.module)],text=True).strip()
        if (value if field=='name' else value.split()[0])!=expected:p.error('Overlay '+field+' mismatch')
    spec=importlib.util.spec_from_file_location('cpio',ROOT/'scripts/pack-sq2-vulkan.py')
    c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
    result,audit=integrate(raw,module,c)
    with a.output.open('xb') as f:f.write(result)
    subprocess.run(['gzip','-t',str(a.output)],check=True)
    audit.update(base_sha256=a.expected_base_sha256,sha256=hashlib.sha256(result).hexdigest(),bytes=len(result))
    a.output.with_suffix(a.output.suffix+'.audit.json').write_text(json.dumps(audit,indent=2)+'\n')
    print(json.dumps(audit,indent=2))


if __name__=='__main__':main()
