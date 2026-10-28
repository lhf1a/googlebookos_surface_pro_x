#!/usr/bin/env python3
"""Stage an isolated ELF-linked EGL adapter; no system writes or preload needed."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'out/quest2-gpu/gles-v77'
OUTPUT = ROOT / 'out/quest2-gpu/gles-linked'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def elf(*args):
    return subprocess.check_output(['patchelf', *map(str, args)], text=True)


def main():
    manifest = json.loads((ROOT / 'configs/quest2-gles-v77-inputs.json').read_text(encoding='utf-8-sig'))
    donors = {Path(name).name: digest for name, digest in manifest['files'].items()}
    probes = {}
    for line in (SOURCE / 'PROBE_SHA256SUMS').read_text().splitlines():
        digest, name = line.split()
        if Path(name).name != name or name in probes:
            raise SystemExit('Invalid probe manifest')
        probes[name] = digest
    if set(probes) != {'gles-readback', 'libqti-mapper3-bridge.so',
                       'libquest2-ion-dmaheap.so', 'libquest2-egl-wrapper.so'}:
        raise SystemExit('Build the current GLES probe first')
    for name, digest in (donors | probes).items():
        if sha(SOURCE / name) != digest:
            raise SystemExit(f'Input hash mismatch: {name}')
    OUTPUT.mkdir()  # Refuse to overwrite a prior candidate.
    for name in donors | probes:
        shutil.copyfile(SOURCE / name, OUTPUT / name)
    wrapper = OUTPUT / 'libquest2-egl-wrapper.so'
    # patchelf prepends: bridge and ION symbols precede original donor libraries.
    for name in ('libEGL_adreno.so', 'libquest2-ion-dmaheap.so', 'libqti-mapper3-bridge.so'):
        elf('--add-needed', name, wrapper)
    elf('--replace-needed', 'libEGL_adreno.so', wrapper.name, OUTPUT / 'gles-readback')
    (OUTPUT / 'gles-readback').chmod(0o755)
    for path in OUTPUT.iterdir():
        elf('--set-rpath', '$ORIGIN', path)
    for filename, names in (('SHA256SUMS', donors), ('PROBE_SHA256SUMS', probes)):
        (OUTPUT / filename).write_text(''.join(f'{sha(OUTPUT / name)}  {name}\n' for name in names))
    print('QUEST2_GLES_LINKED_STAGED', OUTPUT)
    print(elf('--print-needed', wrapper), end='')


if __name__ == '__main__':
    main()
