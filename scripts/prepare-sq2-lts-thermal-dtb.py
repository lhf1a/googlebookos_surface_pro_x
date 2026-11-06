#!/usr/bin/env python3
"""Align the SQ2 LTS DT cooling policy without replacing native resources.

Build-only. Preserve the input and require a fresh output. Uses provider
phandles from this DT, not those from an older boot. No voltage fabrication.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import libfdt

spec = importlib.util.spec_from_file_location('gpu', Path(__file__).with_name('build-sq2-gpu-opp.py'))
gpu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gpu)
u32 = lambda x: struct.pack('>I', x)


def build(raw, gpu_profile='baseline-700'):
    assert gpu_profile in ('validated-661', 'baseline-700')
    f = libfdt.Fdt(raw)
    before = gpu.snapshot(f)
    assert b'microsoft,surface-pro-x\0' in before['/']['compatible']
    assert b'qcom,sc8180x\0' in before['/']['compatible']
    gp = '/soc@0/gpu@2c00000'
    assert before[gp]['compatible'] == b'qcom,adreno-680.1\0qcom,adreno\0'
    assert before[gp]['#cooling-cells'] == u32(2)
    table = gp + '/opp-table'
    actual = {struct.unpack('>Q', v['opp-hz'])[0]: struct.unpack('>I', v['opp-level'])[0]
              for p, v in before.items() if p.startswith(table + '/')}
    assert actual == gpu.BASE_OPPS, 'Expected original 514 MHz native table'
    f.resize(len(raw) + 8192)
    added = set()
    changed = set()

    def node(path):
        parent, name = path.rsplit('/', 1)
        assert path not in before
        f.add_subnode(f.path_offset(parent), name)
        added.add(path)

    def put(path, key, value):
        f.setprop(f.path_offset(path), key, value)

    frequencies = (590000000, 661000000, 700000000) if gpu_profile == 'baseline-700' else (590000000, 661000000)
    for hz in frequencies:
        path = table + '/opp-' + str(hz)
        node(path)
        put(path, 'opp-hz', struct.pack('>Q', hz))
        put(path, 'opp-level', u32(432 if gpu_profile == 'baseline-700' and hz >= 661000000 else 416))
    # Keep native GPU hot/critical trips, bottom sensor and cooling map intact.
    zone = '/thermal-zones/gpu-top-thermal'
    trip = zone + '/trips/trip-point0'
    assert before[trip]['type'] == b'passive\0'
    assert before[zone + '/cooling-maps/map0']['trip'] == before[trip]['phandle']
    assert before[zone + '/cooling-maps/map0']['cooling-device'] == before[gp]['phandle'] + u32(0xffffffff) * 2
    put(trip, 'temperature', u32(75000))
    put(trip, 'hysteresis', u32(5000))
    changed.update((trip,))
    next_ph = max(struct.unpack('>I', v['phandle'])[0] for v in before.values() if 'phandle' in v) + 1
    for cluster, cpu in ((0, '/cpus/cpu@0'), (1, '/cpus/cpu@400')):
        zone = f'/thermal-zones/cluster{cluster}-thermal'
        assert before[cpu]['#cooling-cells'] == u32(2)
        trip = zone + '/trips/sq2-cpu-passive'
        node(trip)
        for k, v in {'temperature': u32(80000), 'hysteresis': u32(5000),
                     'type': b'passive\0', 'phandle': u32(next_ph)}.items():
            put(trip, k, v)
        maps = zone + '/cooling-maps'
        node(maps)
        node(maps + '/sq2-cpu')
        put(maps + '/sq2-cpu', 'trip', u32(next_ph))
        put(maps + '/sq2-cpu', 'cooling-device', before[cpu]['phandle'] + u32(0xffffffff) * 2)
        next_ph += 1
    # Match baseline TSENS polling; leave PMIC sensor policies alone.
    by_ph = {v['phandle']: p for p, v in before.items() if 'phandle' in v}
    for p, v in before.items():
        if p.startswith('/thermal-zones/') and 'thermal-sensors' in v:
            provider = before[by_ph[v['thermal-sensors'][:4]]]
            if b'qcom,tsens-v2\0' in provider.get('compatible', b''):
                put(p, 'polling-delay', u32(1000))
                changed.add(p)
    f.pack()
    after = gpu.snapshot(f)
    assert set(after) - set(before) == added
    for p, v in before.items():
        if p not in changed:
            assert after[p] == v, p
        else:
            allowed = {'temperature', 'hysteresis'} if p.endswith('/trip-point0') else {'polling-delay'}
            assert {k: x for k, x in after[p].items() if k not in allowed} == {k: x for k, x in v.items() if k not in allowed}, p
    original = libfdt.Fdt(raw)
    assert f.num_mem_rsv() == original.num_mem_rsv()
    assert all(f.get_mem_rsv(i) == original.get_mem_rsv(i) for i in range(f.num_mem_rsv()))
    result = bytes(f.as_bytearray())
    return result, {'gpu_profile': gpu_profile, 'gpu_max_hz': max(frequencies),
                    'note': '700/432 selected by user; historical GMU timeouts, not stability verified',
                    'added_nodes': sorted(added), 'changed_nodes': sorted(changed),
                    'input_sha256': hashlib.sha256(raw).hexdigest(),
                    'sha256': hashlib.sha256(result).hexdigest(), 'hardware_verified': False}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--gpu-profile', choices=('validated-661', 'baseline-700'), default='baseline-700')
    a = p.parse_args()
    assert not a.output.exists()
    result, report = build(a.input.read_bytes(), a.gpu_profile)
    with a.output.open('xb') as stream:
        stream.write(result)
    assert a.output.read_bytes() == result
    a.output.with_suffix('.audit.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
