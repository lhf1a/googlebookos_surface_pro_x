#!/usr/bin/env python3
"""Create a private CPU-access candidate; never changes the frozen runtime."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

OLD = b'/vendor/lib64/hw/mapper.minigbm.so\0'
NEW = b'/vendor/lib64/egl/libsq2map.so\0'
MAPPER_SHA = 'dc0f8d0530154501fa849408d115fb22c14732b4e18a38977a01c791bbb3a0ac'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime', type=Path, required=True)
    p.add_argument('--shim', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.output.exists(): raise ValueError('Refusing existing candidate')
    records = {}
    for line in (a.runtime/'SHA256SUMS').read_text().splitlines():
        digest, name = line.split()
        if Path(name).name != name or name in records: raise ValueError('Invalid baseline manifest')
        f = a.runtime/name
        if f.is_symlink() or sha(f.read_bytes()) != digest: raise ValueError('Baseline mismatch: '+name)
        records[name] = digest
    if len(records) != 15 or len([n for n in records if n.endswith('.so')]) != 14:
        raise ValueError('Expected exact 14-library baseline plus qgl_config')
    name = 'libqti-mapper3-bridge.so'
    original = (a.runtime/name).read_bytes()
    if sha(original) != MAPPER_SHA or original.count(OLD) != 1 or len(NEW) > len(OLD):
        raise ValueError('Unexpected private mapper baseline/path')
    at = original.index(OLD)
    after = NEW + bytes(len(OLD)-len(NEW))
    data = original[:at]+after+original[at+len(OLD):]
    # The one string is data, not instructions. No ELF tables, relocations,
    # symbol versions, imports or executable sections are edited.
    shoff = struct.unpack_from('<Q', original, 40)[0]
    shsize, shcount = struct.unpack_from('<HH', original, 58)
    if shsize != 64: raise ValueError('Unexpected ELF section table')
    for i in range(shcount):
        section = struct.unpack_from('<IIQQQQIIQQ', original, shoff+i*shsize)
        if section[2] & 4 and original[section[4]:section[4]+section[5]] != data[section[4]:section[4]+section[5]]:
            raise ValueError('Executable section changed')
    shim = a.shim.read_bytes()
    if shim[:6] != b'\x7fELF\x02\x01' or int.from_bytes(shim[18:20], 'little') != 183:
        raise ValueError('Expected ARM64 private shim')
    a.output.mkdir(parents=True)
    for filename in records: shutil.copyfile(a.runtime/filename, a.output/filename)
    (a.output/name).write_bytes(data)
    (a.output/'libsq2map.so').write_bytes(shim)
    report = json.loads((a.runtime/'RUNTIME.json').read_text())
    report.update(kind='private-full-qualcomm-v77-cpu-access-candidate', baseline_runtime=str(a.runtime.resolve()),
                  baseline_runtime_manifest_sha256=sha((a.runtime/'SHA256SUMS').read_bytes()),
                  device_tested=False, device_operations=[],
                  exact_changes=['one non-executable mapper3 dlopen pathname', 'new private libsq2map.so'],
                  binding='explicit local dlopen handle; no global dlsym/LD_PRELOAD/system mapper changes',
                  scope='real mapper5 BAD_VALUE fallback only for verified linear single-plane nonprotected GPU RGB buffers without allocation CPU usage',
                  safety='real acquire fence wait, DMA-BUF START/END, retained import/map/FD on failed END; no fake metadata/CPU allocation flags',
                  rollback='restore original mapper3 path and omit libsq2map.so; frozen original runtime is untouched')
    report['files'][name].update(sha256=sha(data),bytes=len(data),operation='single private dlopen path replacement')
    report['files']['libsq2map.so']={'sha256':sha(shim),'bytes':len(shim),'source':str(a.shim.resolve()),'operation':'new private compatibility shim'}
    records[name] = sha(data); records['libsq2map.so'] = sha(shim)
    (a.output/'SHA256SUMS').write_text(''.join(f'{digest}  {filename}\n' for filename,digest in sorted(records.items())))
    (a.output/'RUNTIME.json').write_text(json.dumps(report,indent=2)+'\n')
    patch={'format':'byte-range-patch-v1','target':name,'input_sha256':MAPPER_SHA,'output_sha256':sha(data),
           'offset':at,'before_hex':OLD.hex(),'after_hex':after.hex(),'same_length':True}
    (a.output.parent/'DIFF_FILE.patch.json').write_text(json.dumps(patch,indent=2)+'\n')
    for filename,digest in records.items():
        if sha((a.output/filename).read_bytes()) != digest: raise ValueError('Output reread mismatch')
    for filename,digest in json.loads((a.runtime/'RUNTIME.json').read_text())['files'].items():
        if sha((a.runtime/filename).read_bytes()) != digest['sha256']: raise ValueError('Baseline changed')
    print('CPU_ACCESS_RUNTIME_PREPARED libraries=15 modified_existing_libraries=1 device_tested=0')
    print('mapper_sha256='+sha(data));print('shim_sha256='+sha(shim));print(a.output.resolve())


if __name__ == '__main__': main()
