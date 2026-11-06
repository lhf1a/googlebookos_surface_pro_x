#!/usr/bin/env python3
"""Import the baseline SC8180X IPA binding with target-tree provider references.

No donor identity, calibration, whole-board replacement or runtime firmware reset.
Retains every existing target property. Staging RAM is reserved before Linux boot.
"""
import argparse,hashlib,importlib.util,json,struct
from pathlib import Path
import libfdt
spec=importlib.util.spec_from_file_location('offsets',Path(__file__).with_name('sq2-overlay-phandles.py'))
offsets=importlib.util.module_from_spec(spec);spec.loader.exec_module(offsets)
cells=lambda *v:struct.pack('>'+'I'*len(v),*v)
def words(b):
    if len(b)%4:raise ValueError('unaligned cells')
    return list(struct.unpack('>'+'I'*(len(b)//4),b))
def properties(data):return {k:v for k,(_,v) in offsets.property_offsets(data).items()}
def reserve_entries(dt):return [tuple(dt.get_mem_rsv(i)[-2:]) for i in range(dt.num_mem_rsv())]
def prepare(data,reference):
    before=properties(data);ref=properties(reference);dt=libfdt.Fdt(data)
    for tree in (before,ref):
        if not {b'microsoft,surface-pro-x',b'qcom,sc8180x'} <= set(tree['/','compatible'].split(b'\0')):raise ValueError('incompatible board class')
        if tree['/reserved-memory','#address-cells']!=cells(2) or tree['/reserved-memory','#size-cells']!=cells(2):raise ValueError('reserved memory cell layout')
    node='/soc@0/ipa@1e40000'
    if any(path==node for path,_ in before):raise ValueError('IPA already exists; do not overwrite')
    if ref[node,'compatible']!=b'qcom,sc8180x-ipa\0':raise ValueError('IPA binding')
    ref_handles={words(v)[0]:path for (path,n),v in ref.items() if n in ('phandle','linux,phandle')}
    memory=ref_handles[words(ref[node,'memory-region'])[0]]
    start,size=struct.unpack('>QQ',ref[memory,'reg'])
    if (start,size)!=(0x9b000000,0x60000) or ref.get((memory,'no-map'))!=b'':raise ValueError('baseline IPA staging ABI')
    for (path,n),v in before.items():
        if path.startswith('/reserved-memory/') and n=='reg':
            if len(v)%16:raise ValueError('reserved memory reg length')
            for i in range(0,len(v),16):
                a,l=struct.unpack_from('>QQ',v,i)
                if l and start<a+l and a<start+size:raise ValueError('staging overlaps '+path)
    for a,l in reserve_entries(dt):
        if start<a+l and a<start+size:raise ValueError('staging overlaps reserve map')
    used={words(v)[0] for (path,n),v in before.items() if n in ('phandle','linux,phandle')}
    dt.resize(len(data)+8192);next_id=max(used,default=0)+1;resolved={};bindings=[]
    def get(path,n):
        try:return bytes(dt.getprop(dt.path_offset(path),n))
        except libfdt.FdtException as e:
            if e.err!=-libfdt.NOTFOUND:raise
            return None
    def ensure_node(path):
        try:dt.path_offset(path)
        except libfdt.FdtException as e:
            if e.err!=-libfdt.NOTFOUND:raise
            parent,name=path.rsplit('/',1);dt.add_subnode(dt.path_offset(parent or '/'),name)
    def handle(path):
        nonlocal next_id
        value=get(path,'phandle') or get(path,'linux,phandle')
        if value is not None:return words(value)[0]
        if next_id>=0xffffffff:raise ValueError('phandle exhaustion')
        value=next_id;next_id+=1
        dt.setprop(dt.path_offset(path),'phandle',cells(value));return value
    ensure_node(memory)
    for n in ('reg','no-map'):dt.setprop(dt.path_offset(memory),n,ref[memory,n])
    resolved[memory]=(memory,handle(memory))
    def provider(old):
        path=ref_handles[old]
        if path in resolved:return resolved[path]
        target=path
        if get(target,'compatible') is None and (path,'compatible') in ref:
            compat=ref[path,'compatible'];reg=ref.get((path,'reg'))
            matches=[p for (p,n),v in before.items() if n=='compatible' and v==compat and
                     (reg is None or before.get((p,'reg'),b'')[:8]==reg[:8])]
            if len(matches)!=1:raise ValueError('ambiguous provider '+path)
            target=matches[0]
        # Preserve target-native register sizes (e.g. 6.18 QMP 0x400 vs legacy1MiB),
        # but require the same hardware base and compatible. Never copy the old range.
        for n in ('compatible','qcom,entry-name','#clock-cells','#interrupt-cells','#iommu-cells','#interconnect-cells','#qcom,smem-state-cells'):
            if (path,n) in ref and get(target,n)!=ref[path,n]:raise ValueError('provider mismatch '+path+':'+n)
        if (path,'reg') in ref and (get(target,'reg') or b'')[:8]!=ref[path,'reg'][:8]:raise ValueError('provider address mismatch '+path)
        if get(target,'status') not in (None,b'okay\0',b'ok\0'):raise ValueError('disabled provider '+target)
        result=(target,handle(target));resolved[path]=result;return result
    specs={'memory-region':None,'qcom,qmp':None,'qcom,smem-states':'#qcom,smem-state-cells','interrupts-extended':'#interrupt-cells','iommus':'#iommu-cells','clocks':'#clock-cells','interconnects':'#interconnect-cells'}
    ensure_node(node)
    for (path,n),v in ref.items():
        if path!=node or n in ('phandle','linux,phandle'):continue
        if n in specs:
            values=words(v);i=0
            while i<len(values):
                old=values[i];source=ref_handles[old];target,new=provider(old)
                count=words(ref[source,specs[n]])[0] if specs[n] else 0
                if i+1+count>len(values):raise ValueError('truncated reference '+n)
                bindings.append({'property':n,'cell':i,'source_provider':source,'target_provider':target,'source_phandle':old,'target_phandle':new,'args':values[i+1:i+1+count]})
                values[i]=new;i+=1+count
            v=cells(*values)
        dt.setprop(dt.path_offset(node),n,v)
    rc=libfdt.fdt_add_mem_rsv(dt._fdt,start,size)
    if rc:raise ValueError('reserve-map insertion failed '+str(rc))
    dt.pack();result=bytes(dt.as_bytearray());after=properties(result)
    for key,value in before.items():
        if after.get(key)!=value:raise ValueError('unexpected existing-property change '+str(key))
    return result,{'bindings':bindings,'added_properties':[[p,n] for p,n in after.keys()-before.keys()],'staging_start':start,'staging_size':size,'original_properties_unchanged':True}
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ('input','reference','output'):ap.add_argument(n,type=Path)
    a=ap.parse_args();original=a.input.read_bytes();reference=a.reference.read_bytes();result,report=prepare(original,reference)
    with a.output.open('xb') as f:f.write(result)
    assert a.output.read_bytes()==result and a.input.read_bytes()==original and a.reference.read_bytes()==reference
    report.update(input_sha256=hashlib.sha256(original).hexdigest(),reference_sha256=hashlib.sha256(reference).hexdigest(),sha256=hashlib.sha256(result).hexdigest(),bytes=len(result),deployed=False)
    a.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
