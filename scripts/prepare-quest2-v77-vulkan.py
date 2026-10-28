#!/usr/bin/env python3
"""Prepare an offline, same-firmware Quest 2 v77 Vulkan candidate.

Never contacts a device, runs donor code, or replaces a system driver. Requires
Linux/WSL patchelf and readelf. See docs/QUEST2-V77-VULKAN.md for the ABI limits.
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
DONORS = {
    'hw/vulkan.adreno.so': '0f316d0331ef5b4ec60d8d3f21a845f47583b14ccaabe54f316e619b9897bd8d',
    'libgsl.so': '8971ade1009b02ca49a19cde9a78e324b2f7b868253f614c337105bbb669cfb6',
    'libadreno_utils.so': '8bca58979c908b1da4b7caf064c65a5b72eb8ae6989a316e63611b2c1597eb57',
    'libllvm-glnext.so': '64054b91e804c2e021e28bc1225b205cf754a633850c577db445132ed4ea29fb',
    'libllvm-qgl.so': 'e52264451deb74c86e728b3559b99267eb37b1b0a1973f578d584707767ae49a',
}
ADAPTERS = ('libqti-mapper3-bridge.so', 'libquest2-ion-dmaheap.so')
HAL = 'vulkan-v77-bridge.so'
CONFIG_OFFSET = 0x57277
OLD_DIRECTORY = b'/data/vendor/gpu/\0'
NEW_DIRECTORY = b'/data/sq2vk-v77t/\0'
PROFILE_OFFSET = 0x4ee5c
OLD_PROFILE = b'ApplyApplicationProfile\0'
NEW_PROFILE = b'Skip_ApplicationProfile\0'
DIRECT_CONFIG = b'0x0=0x08675309\nRenderMode=Direct\n'
ION_EXPORTS = {'ion_open', 'ion_close', 'ion_alloc_fd'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run(*args):
    return subprocess.check_output([str(arg) for arg in args], text=True).strip()


def elf_info(path):
    return {'needed': run('patchelf', '--print-needed', path).splitlines(),
            'soname': run('patchelf', '--print-soname', path),
            'runpath': run('patchelf', '--print-rpath', path)}


def code_sections(data):
    """Hash executable sections without executing or loading the ARM64 ELF."""
    if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 183:
        raise ValueError('Expected little-endian ELF64 AArch64')
    offset = struct.unpack_from('<Q', data, 40)[0]
    size, count, names_index = struct.unpack_from('<HHH', data, 58)
    if size != 64 or not count or names_index >= count:
        raise ValueError('Unsupported ELF section layout')
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, offset + i * size)
                for i in range(count)]
    names_section = sections[names_index]
    names = data[names_section[4]:names_section[4] + names_section[5]]
    result = {}
    for section in sections:
        if section[2] & 4:  # SHF_EXECINSTR
            name = names[section[0]:].split(b'\0', 1)[0].decode('ascii')
            result[name] = digest(data[section[4]:section[4] + section[5]])
    if not result:
        raise ValueError('ELF has no executable sections')
    return result


def ion_symbols(path, undefined):
    result = set()
    for line in run('readelf', '--dyn-syms', '-W', path).splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[7].startswith('ion_'):
            if (fields[6] == 'UND') == undefined:
                result.add(fields[7].split('@', 1)[0])
    return result


def patch_slot(data, offset, before, after):
    if len(before) != len(after) or data[offset:offset + len(before)] != before:
        raise ValueError(f'Unexpected fixed string slot at {offset:#x}')
    return data[:offset] + after + data[offset + len(before):]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'out/quest2-gpu/q2-v77/lib64')
    parser.add_argument('--adapters', type=Path, default=ROOT / 'out/quest2-gpu/gles-v77')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/quest2-gpu/v77-vulkan-runtime')
    parser.add_argument('--direct', action='store_true', help='Include private RenderMode=Direct config')
    parser.add_argument('--system-loader', action='store_true',
                        help='Use fixed /vendor/lib64/egl RUNPATH for the private system-loader namespace')
    parser.add_argument('--keep-optional-profile', action='store_true',
                        help='Keep the original ApplyApplicationProfile lookup for a separate comparison')
    args = parser.parse_args()
    runpath = '/vendor/lib64/egl' if args.system_loader else '$ORIGIN'
    source, adapters, output = args.source.resolve(), args.adapters.resolve(), args.output.resolve()
    out_root = (ROOT / 'out').resolve()
    if out_root not in output.parents or output.exists():
        parser.error('Output must be a new, nonexistent directory under this repository out/')
    if any(p == output or p in output.parents or output in p.parents for p in (source, adapters)):
        parser.error('Output must be separate from donor and adapter inputs')
    for program in ('patchelf', 'readelf'):
        if not shutil.which(program):
            parser.error(f'{program} is required; run from Linux/WSL')

    inputs = {}
    donor_data = {}
    for relative, expected in DONORS.items():
        path = source / relative
        data = path.read_bytes()
        if digest(data) != expected:
            parser.error(f'Original v77 hash mismatch: {relative}')
        name = HAL if relative.startswith('hw/') else relative
        donor_data[name] = data
        inputs[name] = {'source': relative, 'sha256': expected, 'elf': elf_info(path)}
    bridge_manifest = {}
    for line in (adapters / 'PROBE_SHA256SUMS').read_text().splitlines():
        checksum, name = line.split()
        if not re.fullmatch(r'[0-9a-f]{64}', checksum) or Path(name).name != name or name in bridge_manifest:
            parser.error('Invalid adapter build manifest')
        bridge_manifest[name] = checksum
    adapter_data = {}
    for name in ADAPTERS:
        data = (adapters / name).read_bytes()
        if digest(data) != bridge_manifest.get(name):
            parser.error(f'Adapter hash mismatch: {name}; rebuild the GLES adapters')
        adapter_data[name] = data
        inputs[name] = {'source': name, 'sha256': digest(data), 'elf': elf_info(adapters / name)}
    if ion_symbols(source / 'libgsl.so', True) != ION_EXPORTS:
        parser.error('Unexpected GSL ION ABI; do not replace this dependency')
    if not ION_EXPORTS <= ion_symbols(adapters / ADAPTERS[1], False):
        parser.error('ION control adapter lacks required symbols')
    if 'vendor.qti.hardware.display.mapper@3.0.so' not in inputs[ADAPTERS[0]]['elf']['needed']:
        parser.error('Expected mapper3 bridge, not mapper4')
    if any('mapper@4.0' in name for name in inputs[HAL]['elf']['needed']):
        parser.error('Unexpected mapper4 Vulkan donor')

    original = donor_data[HAL]
    if original[0x57289:0x57298] != b'qgl_config.txt\0':
        parser.error('Unexpected v77 qgl_config filename slot')
    patched = patch_slot(original, CONFIG_OFFSET, OLD_DIRECTORY, NEW_DIRECTORY)
    if not args.keep_optional_profile:
        if original.count(OLD_PROFILE) != 1:
            parser.error('Unexpected v77 profile symbol layout')
        patched = patch_slot(patched, PROFILE_OFFSET, OLD_PROFILE, NEW_PROFILE)
    initial_code = {name: code_sections(data) for name, data in (donor_data | adapter_data).items()}
    donor_data[HAL] = patched

    # No source, system path or device is written. Refuse stale output directories.
    output.mkdir(parents=True)
    for name, data in (donor_data | adapter_data).items():
        (output / name).write_bytes(data)
    run('patchelf', '--set-soname', HAL, '--add-needed', ADAPTERS[0], output / HAL)
    # GSL imports exactly these three unversioned ion_* symbols. Route only this
    # copied GSL dependency to the control adapter; allocation remains ENOTSUP.
    run('patchelf', '--replace-needed', 'libion.so', ADAPTERS[1], output / 'libgsl.so')
    for name in donor_data | adapter_data:
        run('patchelf', '--set-rpath', runpath, output / name)
        if code_sections((output / name).read_bytes()) != initial_code[name]:
            raise SystemExit(f'Executable section changed unexpectedly: {name}')
    if args.direct:
        (output / 'qgl_config.txt').write_bytes(DIRECT_CONFIG)

    resulting_elf = {name: elf_info(output / name) for name in donor_data | adapter_data}
    if 'libion.so' in resulting_elf['libgsl.so']['needed'] or ADAPTERS[1] not in resulting_elf['libgsl.so']['needed']:
        raise SystemExit('ION dependency rewrite verification failed')
    if resulting_elf[HAL]['needed'][0] != ADAPTERS[0] or resulting_elf[HAL]['soname'] != HAL:
        raise SystemExit('Mapper dependency or HAL SONAME verification failed')
    if any(info['runpath'] != runpath for info in resulting_elf.values()):
        raise SystemExit('RUNPATH verification failed')
    names = sorted(donor_data | adapter_data)
    if args.direct:
        names.append('qgl_config.txt')
    hashes = {name: digest((output / name).read_bytes()) for name in names}
    for relative, expected in DONORS.items():
        if digest((source / relative).read_bytes()) != expected:
            raise SystemExit(f'Donor changed during preparation: {relative}')
    report = {
        'status': 'offline_prepared_not_device_tested',
        'source': 'Quest 2 v77 same-vendor-image originals; OEM signature not verified',
        'inputs': inputs, 'output_elf': resulting_elf, 'sha256': hashes,
        'private_config_directory': NEW_DIRECTORY[:-1].decode(),
        'config_mode': 'Direct' if args.direct else 'no configuration file',
        'optional_apply_profile_skipped': not args.keep_optional_profile,
        'system_loader_namespace_variant': args.system_loader,
        'runpath': runpath,
        'string_patches': [{'offset': hex(CONFIG_OFFSET), 'before': OLD_DIRECTORY[:-1].decode(),
                            'after': NEW_DIRECTORY[:-1].decode()}] + ([] if args.keep_optional_profile else [
                                {'offset': hex(PROFILE_OFFSET), 'before': OLD_PROFILE[:-1].decode(),
                                 'after': NEW_PROFILE[:-1].decode()}]),
        'executable_sections_unchanged': True, 'source_files_unchanged': True,
        'ion_allocation_supported': False, 'mapper4_included': False,
        'executable_section_sha256': initial_code,
        'patchelf_version': run('patchelf', '--version'),
    }
    (output / 'v77-vulkan-manifest.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    (output / 'SHA256SUMS').write_text(''.join(f'{hashes[name]}  {name}\n' for name in names), encoding='utf-8')
    print(json.dumps({'output': str(output), 'status': report['status'],
                      'vulkan_sha256': hashes[HAL], 'config_mode': report['config_mode']}, indent=2))


if __name__ == '__main__':
    main()
