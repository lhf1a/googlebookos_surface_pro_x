#!/usr/bin/env python3
"""Complete SQ2 hardware-backed CPU OPPs and add cluster passive cooling.

The hardware LUT, not this file, supplies voltages. Missing high CPU OPPs inherit
the existing highest interconnect vote; lower OPPs and GPU properties stay intact.
"""
import argparse,hashlib,importlib.util,json,struct
from pathlib import Path
import libfdt

spec=importlib.util.spec_from_file_location('gpu_opp',Path(__file__).with_name('build-sq2-gpu-opp.py'))
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('base',type=Path);p.add_argument('output',type=Path)
a=p.parse_args();raw=a.base.read_bytes()
assert hashlib.sha256(raw).hexdigest()=='09b6d3a804f8ac87ec1355daf8dfe2714813b908b765d6c7c1fb577418d270d6'
assert not a.output.exists()
fdt=libfdt.Fdt(raw);before=helper.snapshot(fdt);fdt.resize(len(raw)+8192)
table='/opp-table-cpu4';template=before[table+'/opp-2995200000']
assert set(template)=={'opp-hz','opp-peak-kBps'}
added=set()
for hz in (3072000000,3148800000):
 path=f'{table}/opp-{hz}';assert path not in before
 fdt.add_subnode(fdt.path_offset(table),f'opp-{hz}')
 for key,value in template.items():
  fdt.setprop(fdt.path_offset(path),key,struct.pack('>Q',hz) if key=='opp-hz' else value)
 added.add(path)
phandle=max(struct.unpack('>I',v['phandle'])[0] for v in before.values() if 'phandle' in v)+1
for cluster,cpu in ((0,'cpu@0'),(1,'cpu@400')):
 zone=f'/thermal-zones/cluster{cluster}-thermal';assert zone in before
 cpu_props=before['/cpus/'+cpu];assert cpu_props['#cooling-cells']==struct.pack('>I',2)
 trip=zone+'/trips/sq2-cpu-passive';assert trip not in before
 fdt.add_subnode(fdt.path_offset(zone+'/trips'),'sq2-cpu-passive')
 for key,val in {'temperature':struct.pack('>I',80000),'hysteresis':struct.pack('>I',5000),'type':b'passive\0','phandle':struct.pack('>I',phandle)}.items():
  fdt.setprop(fdt.path_offset(trip),key,val)
 maps=zone+'/cooling-maps';assert maps not in before
 fdt.add_subnode(fdt.path_offset(zone),'cooling-maps')
 fdt.add_subnode(fdt.path_offset(maps),'sq2-cpu')
 fdt.setprop(fdt.path_offset(maps+'/sq2-cpu'),'trip',struct.pack('>I',phandle))
 fdt.setprop(fdt.path_offset(maps+'/sq2-cpu'),'cooling-device',cpu_props['phandle']+struct.pack('>II',0xffffffff,0xffffffff))
 added|={trip,maps,maps+'/sq2-cpu'};phandle+=1
fdt.pack();after=helper.snapshot(fdt)
assert set(after)-set(before)==added
assert all(after[k]==v for k,v in before.items())
original=libfdt.Fdt(raw)
assert fdt.num_mem_rsv()==original.num_mem_rsv()
assert all(fdt.get_mem_rsv(i)==original.get_mem_rsv(i) for i in range(fdt.num_mem_rsv()))
output=bytes(fdt.as_bytearray());a.output.parent.mkdir(parents=True,exist_ok=True)
with a.output.open('xb') as f:f.write(output)
report={'sha256':hashlib.sha256(output).hexdigest(),'added_nodes':sorted(added),'original_properties_unchanged':True,'cpu_voltages':'read from firmware LUT by qcom-cpufreq-hw'}
a.output.with_suffix('.audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
