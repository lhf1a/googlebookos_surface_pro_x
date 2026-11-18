#!/usr/bin/env python3
"""Prepare an isolated GSL copy with version-cleared, uniquely named imports.

Private donor bytes never enter Git. No opcode changes, no in-place edits,
no system installation. Runtime selection is controlled separately by the bridge.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
RENAMES = {n: 'qti_vm_gsl_' + n for n in ('open', 'close', 'dup', 'fcntl', 'ioctl')}
RENAMES['__open_2'] = 'qti_vm_gsl_open2'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gsl', type=Path, required=True)
    parser.add_argument('--bridge', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    a = parser.parse_args()
    if a.output.exists():
        parser.error('output must be fresh')
    spec = importlib.util.spec_from_file_location('qti_elf', ROOT / 'scripts/prepare-quest2-v77-vulkan.py')
    elf = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(elf)
    gsl = a.gsl.resolve()
    code = elf.code_sections(gsl.read_bytes())
    symbols = subprocess.check_output(['readelf', '--dyn-syms', '-W', str(gsl)], text=True)
    for name in RENAMES:
        hits = [line for line in symbols.splitlines()
                if len(line.split()) >= 8 and line.split()[6] == 'UND'
                and line.split()[7].split('@')[0] == name]
        if len(hits) != 1:
            raise ValueError('Expected one undefined import: ' + name)
    a.output.mkdir(parents=True)
    out = a.output.resolve()
    candidate = out / 'libgsl.so'
    shutil.copyfile(gsl, candidate)
    shutil.copyfile(a.bridge, out / 'libqti-msm-vmbind-bridge.so')
    names = out / 'imports.txt'
    names.write_text(''.join(old + ' ' + new + '\n' for old, new in RENAMES.items()))
    commands = []

    def run(argv):
        command = list(map(str, argv));commands.append(command)
        subprocess.run(command, check=True, capture_output=True, text=True)

    # Imported libc names are versioned LIBC. The new bridge names must not
    # continue requiring those symbols specifically from libc's version map.
    for old in RENAMES:
        run(['patchelf', '--clear-symbol-version', old, candidate])
    run(['patchelf', '--rename-dynamic-symbols', names, candidate])
    run(['patchelf', '--add-needed', 'libqti-msm-vmbind-bridge.so', candidate])
    if elf.code_sections(candidate.read_bytes()) != code:
        raise ValueError('GSL executable sections changed')
    after = subprocess.check_output(['readelf', '--dyn-syms', '-W', str(candidate)], text=True)
    for new in RENAMES.values():
        hits = [line.split()[7] for line in after.splitlines()
                if len(line.split()) >= 8 and line.split()[6] == 'UND'
                and line.split()[7].split('@')[0] == new]
        if hits != [new]:
            raise ValueError('Renamed import is missing or still versioned: ' + new)
    sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
    report = {'gsl_source': str(gsl), 'source_sha256': sha(gsl), 'candidate_sha256': sha(candidate),
              'bridge_sha256': sha(out / 'libqti-msm-vmbind-bridge.so'),
              'renames': RENAMES, 'executable_sections_unchanged': True,
              'commands': commands, 'installed': False}
    (out / 'PREPARE.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
