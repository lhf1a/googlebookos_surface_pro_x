#!/usr/bin/env python3
"""Stage unmodified, pinned Quest 2 OMX libraries for an isolated 6.18 trial.

This does not replace the Android Codec2 service, copy firmware, change the GPU
backend, or write a device. A successful stage/load is NOT successful decoding.
Donor binaries remain private and must not be committed or publicly packaged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = {'libc.so', 'libm.so', 'libdl.so', 'liblog.so'}
ROOT_LIBRARIES = ('libOmxCore.so', 'libOmxVdec.so')


def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def elf64_arm(path):
    with path.open('rb') as f:
        head = f.read(20)
    if len(head) != 20 or head[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', head, 18)[0] != 183:
        raise ValueError(f'Expected little-endian AArch64 ELF: {path.name}')


def prepare(reference, probe, output, readelf='readelf'):
    reference, probe, output = reference.resolve(), probe.resolve(), output.resolve()
    if output.exists():
        raise ValueError('Output must be a fresh directory')
    if output == reference or reference in output.parents:
        raise ValueError('Output must not modify the donor reference')
    cfg = json.loads((ROOT/'configs/quest2-v77-video-reference.json').read_text())
    inventory = json.loads((reference/'inventory.json').read_text())
    if inventory['image_sha256'] != cfg['image_sha256'] or inventory['build'] != cfg['build']:
        raise ValueError('Donor partition identity mismatch')
    entries = {e['path']: e for e in inventory['entries'] if e.get('regular')}
    pending, selected, external = list(ROOT_LIBRARIES), {}, set()
    while pending:
        name = pending.pop()
        if name in selected or name in PLATFORM:
            continue
        if Path(name).name != name:
            raise ValueError('Unsafe dependency filename')
        key = '/lib64/' + name
        entry = entries.get(key)
        if entry is None:
            if name in ROOT_LIBRARIES:
                raise ValueError(f'Missing donor decoder: {name}')
            external.add(name)
            continue
        path = (reference/'vendor'/key.lstrip('/')).resolve()
        if not path.is_relative_to(reference/'vendor') or path.is_symlink():
            raise ValueError('Donor path escaped reference')
        actual = digest(path)
        if actual != entry['sha256'] or path.stat().st_size != entry['bytes']:
            raise ValueError(f'Donor inventory mismatch: {name}')
        if key in cfg['files'] and actual != cfg['files'][key]:
            raise ValueError(f'Pinned video file mismatch: {name}')
        elf64_arm(path)
        dynamic = subprocess.run([readelf, '-dW', str(path)], check=True, capture_output=True, text=True)
        needed = re.findall(r'\(NEEDED\).*\[(.*?)\]', dynamic.stdout)
        selected[name] = {'source': str(path), 'sha256': actual, 'bytes': entry['bytes'], 'needed': needed}
        pending.extend(needed)
    elf64_arm(probe)
    output.mkdir(parents=True)
    (output/'lib64').mkdir()
    for name, record in selected.items():
        dst = output/'lib64'/name
        shutil.copyfile(record['source'], dst)
        if digest(dst) != record['sha256']:
            raise ValueError('Copy hash mismatch: ' + name)
    shutil.copyfile(probe, output/'quest2-omx-probe')
    (output/'quest2-omx-probe').chmod(0o755)
    shutil.copyfile(ROOT/'probe/video/run-quest2-omx-isolated.sh', output/'run.sh')
    (output/'run.sh').chmod(0o755)
    report = dict(schema=1, backend='quest2-original-omx', status='staged-not-decoding-verified',
        build=cfg['build'], vendor_sha256=cfg['image_sha256'], oem_signature_verified=False,
        firmware_replaced=False, system_service_replaced=False, gpu_backend_changed=False,
        private_only=True, files=selected, platform_dependencies=sorted(external | PLATFORM),
        probe_sha256=digest(probe), next_command='./run.sh --enum',
        limit='Original OMX workflow candidate, not a working MediaCodec replacement. No frames validated.')
    (output/'MANIFEST.json').write_text(json.dumps(report, indent=2)+'\n')
    lines = []
    for path in sorted(output.rglob('*')):
        if path.is_file():
            lines.append(digest(path)+'  '+path.relative_to(output).as_posix())
    (output/'SHA256SUMS').write_text('\n'.join(lines)+'\n')
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference', type=Path, required=True, help='Verified extracted vendor directory with inventory.json')
    p.add_argument('--probe', type=Path, required=True, help='AArch64 OMX API client; no decoder implementation')
    p.add_argument('--output', type=Path, required=True, help='Fresh private staging directory')
    p.add_argument('--readelf', default='readelf')
    a = p.parse_args()
    try:
        result = prepare(a.reference, a.probe, a.output, a.readelf)
    except (OSError, ValueError, subprocess.CalledProcessError) as e:
        p.exit(1, str(e)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
