#!/usr/bin/env python3
"""Build a bounded SQ2 GPU OPP experiment from the exact deployed DTB.

Requires python3-libfdt. No disk deployment. Existing OPPs, identities, clocks
and GMU firmware selection stay byte-identical as properties. The optional
thermal guard explicitly adds a GPU-top passive trip and cooling binding.
New frequencies use the already-present TURBO_L1 vote (416), NOT an inferred mV.
This is not an OEM voltage curve or a claim that SC8280XP and SQ2 are identical.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import libfdt

BASE_SHA = '7a4d1c73cc8d64ffef979edf00e1240bef8bf15678690b7a53c45b72434dda55'
GPU = '/soc@0/gpu@2c00000'
TABLE = GPU + '/opp-table'
BASE_OPPS = {177000000:64, 256000000:128, 315000000:192,
             405000000:256, 461000000:320, 500000000:384, 514000000:416}

def snapshot(fdt):
    result = {}
    def walk(node, path):
        props = {}
        for off in iter_offsets(fdt.first_property_offset, fdt.next_property_offset, node):
            prop = fdt.get_property_by_offset(off)
            props[prop.name] = bytes(prop)
        result[path] = props
        for child in iter_offsets(fdt.first_subnode, fdt.next_subnode, node):
            walk(child, path.rstrip('/') + '/' + fdt.get_name(child))
    walk(0, '/')
    return result

def iter_offsets(first, next_, node):
    try:
        off = first(node)
        while off >= 0:
            yield off
            off = next_(off)
    except libfdt.FdtException as e:
        if e.err != -libfdt.NOTFOUND:
            raise

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('baseline', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--max-mhz', type=int, choices=[590,661], required=True)
    p.add_argument('--thermal-guard', action='store_true', help='Add GPU-top passive cooling at 75C / 5C hysteresis; requires TSENS module')
    a = p.parse_args()
    raw = a.baseline.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == BASE_SHA, 'Wrong baseline DTB'
    assert not a.output.exists(), 'Refusing overwrite'
    fdt = libfdt.Fdt(raw)
    before = snapshot(fdt)
    assert before[GPU]['compatible'] == b'qcom,adreno-680.1\0qcom,adreno\0'
    for hz, level in BASE_OPPS.items():
        props = before[f'{TABLE}/opp-{hz}']
        assert struct.unpack('>Q', props['opp-hz'])[0] == hz
        assert struct.unpack('>I', props['opp-level'])[0] == level
    extra = [590000000] + ([661000000] if a.max_mhz == 661 else [])
    fdt.resize(len(raw)+4096)
    for hz in extra:
        node = fdt.add_subnode(fdt.path_offset(TABLE), f'opp-{hz}')
        fdt.setprop(node, 'opp-hz', struct.pack('>Q', hz))
        node = fdt.path_offset(f'{TABLE}/opp-{hz}')
        fdt.setprop(node, 'opp-level', struct.pack('>I', 416))
    changed_allowed = set()
    if a.thermal_guard:
        top='/thermal-zones/gpu-thermal-top'
        assert top in before
        phandles=[struct.unpack('>I',props['phandle'])[0] for props in before.values() if 'phandle' in props]
        new_phandle=max(phandles)+1
        gpu_phandle=before[GPU].get('phandle',struct.pack('>I',new_phandle))
        if 'phandle' not in before[GPU]:
            fdt.setprop(fdt.path_offset(GPU),'phandle',gpu_phandle)
            new_phandle+=1
        fdt.setprop(fdt.path_offset(GPU),'#cooling-cells',struct.pack('>I',2))
        changed_allowed.add(GPU)
        trip=fdt.add_subnode(fdt.path_offset(top+'/trips'),'sq2-gpu-passive')
        fdt.setprop(trip,'temperature',struct.pack('>I',75000))
        trip_path=top+'/trips/sq2-gpu-passive'
        fdt.setprop(fdt.path_offset(trip_path),'hysteresis',struct.pack('>I',5000))
        fdt.setprop(fdt.path_offset(trip_path),'type',b'passive\0')
        fdt.setprop(fdt.path_offset(trip_path),'phandle',struct.pack('>I',new_phandle))
        assert top+'/cooling-maps' not in before
        maps=fdt.add_subnode(fdt.path_offset(top),'cooling-maps')
        fdt.add_subnode(maps,'sq2-gpu')
        map_path=top+'/cooling-maps/sq2-gpu'
        fdt.setprop(fdt.path_offset(map_path),'trip',struct.pack('>I',new_phandle))
        fdt.setprop(fdt.path_offset(map_path),'cooling-device',gpu_phandle+struct.pack('>II',0xffffffff,0xffffffff))
    fdt.pack()
    after = snapshot(fdt)
    added = set(after)-set(before)
    expected={f'{TABLE}/opp-{hz}' for hz in extra}
    if a.thermal_guard:
        expected |= {trip_path,top+'/cooling-maps',map_path}
        assert {k:v for k,v in after[GPU].items() if k not in ('phandle','#cooling-cells')} == {k:v for k,v in before[GPU].items() if k not in ('phandle','#cooling-cells')}
    assert added == expected
    assert all(after[key] == val for key, val in before.items() if key not in changed_allowed), 'Unrelated DT changes'
    original = libfdt.Fdt(raw)
    assert fdt.num_mem_rsv() == original.num_mem_rsv()
    for i in range(fdt.num_mem_rsv()):
        assert fdt.get_mem_rsv(i) == original.get_mem_rsv(i)
    result = bytes(fdt.as_bytearray())
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('xb') as stream:
        stream.write(result)
    report = {'baseline_sha256':BASE_SHA, 'sha256':hashlib.sha256(result).hexdigest(),
              'added_hz':extra, 'opp_level':416, 'added_nodes':sorted(added),
              'other_properties_unchanged':True, 'validated_on_hardware':False,
              'thermal_guard':a.thermal_guard,
              'changed_existing_nodes':sorted(changed_allowed),
              'note':'Experimental frequency extension at existing TURBO_L1 firmware corner; not an OEM mV curve.'}
    a.output.with_suffix('.audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__ == '__main__':
    main()
