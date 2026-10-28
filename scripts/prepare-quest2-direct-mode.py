#!/usr/bin/env python3
"""Prepare an isolated, offline v819 Direct-rendering comparison runtime.

This does not contact a device or install a system driver. It copies the pinned
private runtime and changes only one proven qgl_config directory string slot.
See docs/QUEST2-VULKAN-DIRECT.md for the parser and render-mode evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ORIGINAL_SHA256 = 'b4b4132c3f6c1092563200686d21933ed83ffab865a1f99edda93218e96e1e27'
RUNTIME_SHA256 = 'c3563d942c3bd4d7d9c2d33b99eaf9a5af0f145f74cd19f229a432d6d2870ec2'
DIRECTORY_OFFSET = 0x6e6ef
ORIGINAL_DIRECTORY = b'/data/vendor/gpu/\0'
PRIVATE_DIRECTORY = b'/data/sq2vk-test/\0'
CONFIG = b'0x0=0x08675309\nRenderMode=Direct\n'


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def within(path, parent):
    return path == parent or parent in path.parents


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, default=ROOT / 'out/quest2-gpu/runtime')
    parser.add_argument('--original', type=Path,
                        default=ROOT / 'downloads/quest2-gpu/v819/vulkan.adreno.so')
    parser.add_argument('--output', type=Path,
                        default=ROOT / 'out/quest2-gpu/vulkan-direct-runtime')
    args = parser.parse_args()
    source, original, output = args.runtime.resolve(), args.original.resolve(), args.output.resolve()
    output_root = (ROOT / 'out').resolve()
    if not within(output, output_root) or output == output_root:
        parser.error('Output must be a new directory under this repository out/')
    if within(output, source) or within(source, output):
        parser.error('Output must be separate from the source runtime')
    if output.exists():
        parser.error('Output already exists; choose a new directory to avoid stale test files')

    manifest = json.loads((ROOT / 'configs/quest2-gpu-research.json').read_text(encoding='utf-8'))
    expected = manifest['runtime_artifact_sha256']
    if expected.get('vulkan-bridge.so') != RUNTIME_SHA256:
        parser.error('Runtime manifest changed; review the disassembly before updating this script')
    donor = original.read_bytes()
    if sha256(donor) != ORIGINAL_SHA256:
        parser.error('Unexpected original v819 Vulkan SHA256')

    files = {}
    for name, checksum in expected.items():
        if Path(name).name != name or not name.endswith('.so'):
            parser.error('Unexpected runtime manifest filename')
        data = (source / name).read_bytes()
        if sha256(data) != checksum:
            parser.error(f'Unverified runtime file: {name}')
        files[name] = data
    runtime = files['vulkan-bridge.so']
    if sha256(runtime) != RUNTIME_SHA256:
        parser.error('Unexpected private v819 Vulkan SHA256')

    end = DIRECTORY_OFFSET + len(ORIGINAL_DIRECTORY)
    if len(PRIVATE_DIRECTORY) != len(ORIGINAL_DIRECTORY):
        parser.error('Private directory must fit the exact proven string slot')
    for label, data in [('original', donor), ('runtime', runtime)]:
        if data[DIRECTORY_OFFSET:end] != ORIGINAL_DIRECTORY:
            parser.error(f'Unexpected qgl_config directory slot in {label}')
        if data[0x6e701:0x6e70f] != b'qgl_config.txt':
            parser.error(f'Unexpected qgl_config filename slot in {label}')
    patched = runtime[:DIRECTORY_OFFSET] + PRIVATE_DIRECTORY + runtime[end:]
    changed = [i for i, (a, b) in enumerate(zip(runtime, patched)) if a != b]
    if len(patched) != len(runtime) or not changed or any(
            i < DIRECTORY_OFFSET or i >= end - 1 for i in changed):
        parser.error('Patch changed bytes outside the proven directory text')
    files['vulkan-bridge.so'] = patched

    # All inputs and bounds are checked before creating any outputs.
    output.mkdir(parents=True)
    for name, data in files.items():
        (output / name).write_bytes(data)
    (output / 'qgl_config.txt').write_bytes(CONFIG)
    hashes = {name: sha256(data) for name, data in sorted(files.items())}
    hashes['qgl_config.txt'] = sha256(CONFIG)
    for name, checksum in hashes.items():
        if sha256((output / name).read_bytes()) != checksum:
            raise SystemExit(f'Output verification failed: {name}')
    report = {
        'status': 'offline_prepared_not_device_tested',
        'original_vulkan_sha256': ORIGINAL_SHA256,
        'source_runtime_vulkan_sha256': RUNTIME_SHA256,
        'private_config_directory': PRIVATE_DIRECTORY[:-1].decode(),
        'directory_file_offset': hex(DIRECTORY_OFFSET),
        'changed_file_offsets': [hex(i) for i in changed],
        'only_proven_directory_bytes_changed': True,
        'runtime_files_unchanged_except_vulkan_directory': True,
        'sha256': hashes,
    }
    (output / 'direct-mode-manifest.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8')
    (output / 'SHA256SUMS').write_text(
        ''.join(f'{checksum}  {name}\n' for name, checksum in hashes.items()), encoding='utf-8')
    print(json.dumps({'output': str(output), 'changed_bytes': len(changed),
                      'vulkan_sha256': hashes['vulkan-bridge.so'],
                      'status': report['status']}, indent=2))


if __name__ == '__main__':
    main()
