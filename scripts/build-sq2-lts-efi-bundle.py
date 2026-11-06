#!/usr/bin/env python3
"""Assemble an isolated private SQ2 EFI candidate; never deploy or reboot.

Only unsigned project router/launcher strings are relocated. Signed TCB and
firmware loader remain byte-identical. This is not a generic SQ1 delivery.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct


def strings(raw):
    return [(m.start(), m.end(), m[0][:-2].decode('utf-16le'))
            for m in re.finditer(rb'(?:[\x20-\x7e]\x00){8,}\x00\x00', raw)]


def relocate(raw, directory, expected):
    assert re.fullmatch(r'[A-Za-z0-9_-]{1,10}', directory)
    pe = struct.unpack_from('<I', raw, 60)[0]
    opt = pe + 24
    assert raw[pe:pe+4] == b'PE\0\0'
    assert struct.unpack_from('<H', raw, pe+4)[0] == 0xaa64
    assert struct.unpack_from('<H', raw, opt)[0] == 0x20b
    assert struct.unpack_from('<II', raw, opt+112+32) == (0, 0), 'signed binary must not be modified'
    assert struct.unpack_from('<I', raw, opt+64)[0] == 0
    old = '\\EFI\\SQ2MesaDev\\'
    new = '\\EFI\\' + directory + '\\'
    changed = []
    result = bytearray(raw)
    for start, end, text in strings(raw):
        if old not in text:
            continue
        assert text.startswith(old) or text.startswith('console=')
        replacement = text.replace(old, new).encode('utf-16le') + b'\0\0'
        assert len(replacement) <= end-start
        result[start:end] = replacement + b'\0' * (end-start-len(replacement))
        changed.append((start, end))
    assert len(changed) == expected, 'unexpected launcher layout'
    assert old.encode('utf-16le') not in result
    cursor = 0
    for start, end in changed:
        assert result[cursor:start] == raw[cursor:start]
        cursor = end
    assert result[cursor:] == raw[cursor:]
    return bytes(result)


def inspect(router, launcher, options, directory):
    prefix = '\\EFI\\' + directory + '\\'
    rs = [s for _, _, s in strings(router)]
    ls = [s for _, _, s in strings(launcher)]
    assert rs.count(prefix + 'el2launch.efi') == 1
    for name in ('Image', 'android-el2.dtb', 'qebspilaa64.efi', 'tcblaunch.exe'):
        assert ls.count(prefix + name) == 1, name
    command = [s for s in ls if s.startswith('console=')]
    assert len(command) == 1
    assert command[0] == options.strip(), 'text options differ from effective EFI command'
    tokens = command[0].split()
    for token in ('suspend.screen_only=0', 'suspend.screen_only_autosuspend=0', 'nohibernate', 'sq2.el2=1', 'panic=0'):
        assert tokens.count(token) == 1, token
    assert [t for t in tokens if t.startswith('initrd=')] == ['initrd='+prefix+'initramfs.cpio.gz']
    return {'directory': directory, 'paths_consistent': True, 'unrestricted': True, 'nohibernate': True}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--efi-baseline', type=Path, required=True)
    p.add_argument('--paired', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--directory', default='SQ2L618')
    a = p.parse_args()
    assert not a.output.exists()
    manifest = json.loads((a.paired/'MANIFEST.json').read_text())
    sha = lambda b: hashlib.sha256(b).hexdigest()
    files = {}
    for n in ('Image', 'initramfs.cpio.gz', 'android-el2.dtb'):
        files[n] = (a.paired/n).read_bytes()
        assert sha(files[n]) == manifest['files'][n]['sha256'], n
    for n in ('qebspilaa64.efi', 'tcblaunch.exe'):
        files[n] = (a.efi_baseline/n).read_bytes()
    files['start.efi'] = relocate((a.efi_baseline/'start.efi').read_bytes(), a.directory, 1)
    files['el2launch.efi'] = relocate((a.efi_baseline/'el2launch.efi').read_bytes(), a.directory, 5)
    command = [s for _, _, s in strings(files['el2launch.efi']) if s.startswith('console=')]
    assert len(command) == 1
    files['kernel-options.txt'] = (command[0]+'\n').encode()
    audit = inspect(files['start.efi'], files['el2launch.efi'], command[0], a.directory)
    dest = a.output/'EFI'/a.directory
    dest.mkdir(parents=True)
    for n, b in files.items():
        (dest/n).write_bytes(b)
        assert (dest/n).read_bytes() == b
    hashes = {n: sha(b) for n, b in files.items()}
    (dest/'SHA256SUMS').write_text(''.join(h+'  '+n+'\n' for n,h in sorted(hashes.items())))
    report = dict(audit, sha256=hashes, private_identity_present=True, sq1_profile_included=False,
                  device_deployed=False, hardware_tested=False,
                  signed_tcb_unchanged=True, firmware_loader_unchanged=True,
                  original_router_sq1_branch_preserved=True)
    (a.output/'MANIFEST.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
