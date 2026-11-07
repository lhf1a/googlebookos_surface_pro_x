#!/usr/bin/env python3
"""Build an offline opt-in EFI entry; never deploy, mount, flash or reboot.

Only unsigned project's UTF-16 constants are changed. The accepted Image, DT,
signed TCB and firmware loader are copied byte-for-byte. Payload correctness is
the caller's responsibility; its explicit SHA-256 is required. A short directory
keeps the existing PE allocations intact; this tool never grows a PE section.
"""
import argparse
import base64
import difflib
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = {'Image', 'android-el2.dtb', 'initramfs.cpio.gz', 'start.efi',
            'el2launch.efi', 'kernel-options.txt', 'qebspilaa64.efi', 'tcblaunch.exe'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def utf16_strings(data):
    return [(m.start(), m.end(), m[0][:-2].decode('utf-16le'))
            for m in re.finditer(rb'(?:[\x20-\x7e]\x00){8,}\x00\x00', data)]


def unsigned_arm64_pe(data):
    require(len(data) >= 64 and data[:2] == b'MZ', 'PE DOS header missing')
    pe = struct.unpack_from('<I', data, 60)[0]
    require(pe + 24 + 152 <= len(data), 'Truncated PE header')
    opt = pe + 24
    require(data[pe:pe+4] == b'PE\0\0' and
            struct.unpack_from('<H', data, pe+4)[0] == 0xaa64 and
            struct.unpack_from('<H', data, opt)[0] == 0x20b,
            'Unsigned AArch64 PE32+ required')
    require(struct.unpack_from('<II', data, opt+112+32) == (0, 0),
            'Signed EFI must not be modified')
    require(struct.unpack_from('<I', data, opt+64)[0] == 0,
            'Unexpected nonzero PE checksum')


def rewrite_efi(data, old_directory, directory, expected, graphics_argument):
    unsigned_arm64_pe(data)
    old, new = '\\EFI\\'+old_directory+'\\', '\\EFI\\'+directory+'\\'
    result = bytearray(data)
    edits = []
    for start, end, text in utf16_strings(data):
        if old not in text:
            continue
        require(text.startswith(old) or text.startswith('console='), 'Unexpected path constant')
        updated = text.replace(old, new)
        if text.startswith('console='):
            require(text.split().count('sq2.vulkan=mesa') == 1, 'Accepted Mesa option missing')
            updated = updated.replace('sq2.vulkan=mesa', graphics_argument)
        encoded = updated.encode('utf-16le') + b'\0\0'
        require(len(encoded) <= end-start, 'New constant exceeds original UTF-16 allocation; use a shorter directory')
        result[start:end] = encoded + b'\0'*(end-start-len(encoded))
        edits.append({'offset': start, 'bytes': end-start, 'before': text, 'after': updated})
    require(len(edits) == expected, 'Unexpected number of embedded EFI paths')
    require(old.encode('utf-16le') not in result, 'Original payload directory remains referenced')
    cursor = 0
    for edit in edits:
        require(result[cursor:edit['offset']] == data[cursor:edit['offset']], 'Non-constant bytes changed')
        cursor = edit['offset']+edit['bytes']
    require(result[cursor:] == data[cursor:] and len(result) == len(data), 'PE layout changed')
    return bytes(result), edits


def inspect_efi(router, launcher, options, directory, graphics_argument):
    unsigned_arm64_pe(router)
    unsigned_arm64_pe(launcher)
    prefix = '\\EFI\\'+directory+'\\'
    rs = [s for _, _, s in utf16_strings(router)]
    ls = [s for _, _, s in utf16_strings(launcher)]
    require(rs.count(prefix+'el2launch.efi') == 1, 'Router target mismatch')
    for name in ('Image', 'android-el2.dtb', 'qebspilaa64.efi', 'tcblaunch.exe'):
        require(ls.count(prefix+name) == 1, 'Launcher target mismatch: '+name)
    commands = [s for s in ls if s.startswith('console=')]
    require(commands == [options.strip()], 'Effective UTF-16 options differ from text options')
    tokens = commands[0].split()
    for token in ('suspend.screen_only=0', 'suspend.screen_only_autosuspend=0',
                  'nohibernate', 'panic=0', 'sq2.el2=1', graphics_argument):
        require(tokens.count(token) == 1, 'Required option missing/duplicated: '+token)
    require([t for t in tokens if t.startswith('initrd=')] ==
            ['initrd='+prefix+'initramfs.cpio.gz'], 'Initramfs path mismatch')
    return commands[0]


def inspect_grub(data):
    text = data.decode('utf-8')
    defaults = re.findall(r'^\s*set\s+default\s*=\s*(.*?)\s*$', text, re.M)
    entries = re.findall(r'^\s*menuentry\s+.*$', text, re.M)
    return {'default': defaults, 'menu_entries': len(entries),
            'debug_entries': sum('--id sq2-qcom-618-debug' in e for e in entries)}


def append_entry(data, source_directory, directory):
    text = data.decode('utf-8')
    observed = inspect_grub(data)
    require(observed['menu_entries'] == 2 and observed['debug_entries'] == 0,
            'Expected original two entries; do not replace or duplicate menus')
    require(len(observed['default']) == 1, 'Expected one unchanged default selection')
    require(directory not in text and directory != source_directory, 'Debug directory already used')
    blocks = re.findall(r'^\s*menuentry[^\n]*\{\s*\n(.*?)^\s*\}', text, re.M | re.S)
    require(len(blocks) == 2, 'Unsupported original GRUB block syntax')
    matching = [b for b in blocks if 'chainloader /EFI/'+source_directory+'/start.efi' in b]
    require(len(matching) == 1, 'Accepted default router entry not found')
    searches = re.findall(r'^\s*(search\s+--no-floppy\s+--fs-uuid\s+--set=root\s+[A-Za-z0-9-]+)\s*$', matching[0], re.M)
    require(len(searches) == 1, 'Expected one existing ESP UUID search; no guessed identity')
    addition = ("\n# Offline opt-in candidate; original default and fallback above are unchanged.\n"
                "menuentry 'SQ2 Linux 6.18 LTS (Qualcomm graphics debug)' --id sq2-qcom-618-debug {\n"
                ' '+searches[0]+'\n chainloader /EFI/'+directory+'/start.efi\n}\n')
    candidate = data + (b'' if data.endswith(b'\n') else b'\n') + addition.encode()
    require(candidate.startswith(data), 'Original menu bytes changed')
    after = inspect_grub(candidate)
    require(after == dict(observed, menu_entries=3, debug_entries=1), 'Menu invariant failed')
    return candidate


def rollback_script(original):
    # Restores the menu only; unreferenced debug files are retained as evidence.
    # Requires Python3, available on the build host; no device commands exist.
    encoded = base64.b64encode(original).decode()
    return ('''#!/bin/sh
set -eu
test "$#" -eq 1 || { echo 'usage: ROLLBACK.sh OFFLINE_PACKAGE_COPY' >&2; exit 2; }
exec python3 - "$1" <<'PY'
from pathlib import Path
import base64,hashlib,json,sys
root=Path(sys.argv[1]).resolve()
if root == Path(root.anchor): raise SystemExit('refusing filesystem root')
m=json.loads((root/'MANIFEST.json').read_text())
if m.get('kind') != 'sq2-618-offline-debug-entry': raise SystemExit('not an offline package copy')
target=root/'EFI/BOOT/grub.cfg'
if not target.is_file() or target.resolve()!=target: raise SystemExit('unexpected menu target')
current=hashlib.sha256(target.read_bytes()).hexdigest()
if current not in (m['grub_before_sha256'],m['grub_after_sha256']): raise SystemExit('menu changed since build; refusing overwrite')
data=base64.b64decode("'''+encoded+'''")
if hashlib.sha256(data).hexdigest()!=m['grub_before_sha256']: raise SystemExit('baseline mismatch')
temp=target.with_name('grub.cfg.rollback-tmp')
with temp.open('xb') as f: f.write(data)
temp.replace(target)
if target.read_bytes()!=data: raise SystemExit('rollback readback failed')
print('ROLLBACK_MENU_SHA256='+hashlib.sha256(data).hexdigest())
print('DEBUG_PAYLOAD_RETAINED_UNREFERENCED=1')
PY
''').encode()


def verify_output(out):
    report = json.loads((out/'MANIFEST.json').read_text())
    for name, expected in report['files'].items():
        require(sha((out/name).read_bytes()) == expected, 'Output hash mismatch: '+name)
    dest = out/'EFI'/report['directory']
    inspect_efi((dest/'start.efi').read_bytes(), (dest/'el2launch.efi').read_bytes(),
                (dest/'kernel-options.txt').read_text(), report['directory'], report['graphics_argument'])
    original = (out/'baseline/grub.cfg').read_bytes()
    require((out/'EFI/BOOT/grub.cfg').read_bytes().startswith(original), 'Original GRUB prefix changed')
    require(inspect_grub((out/'EFI/BOOT/grub.cfg').read_bytes()) ==
            dict(inspect_grub(original), menu_entries=3, debug_entries=1), 'Appended menu mismatch')
    return report


def build(a):
    require(not a.output.exists(), 'Fresh output directory required')
    require(re.fullmatch(r'[A-Za-z0-9_-]+', a.directory), 'Invalid EFI directory')
    require(re.fullmatch(r'sq2\.vulkan=(?:qcom|qualcomm|closed)', a.graphics_argument), 'Invalid graphics option')
    baseline_manifest = json.loads(a.manifest.read_text())
    require(baseline_manifest['kernel_release'].startswith('6.18.'), 'Linux 6.18 baseline required')
    require(set(baseline_manifest['files']) == REQUIRED, 'Unexpected baseline file manifest')
    source_directory = Path(baseline_manifest['bundle_directory']).name
    require(re.fullmatch(r'[A-Za-z0-9_-]+', source_directory), 'Invalid baseline directory')
    files = {}
    for name, expected in baseline_manifest['files'].items():
        files[name] = (a.baseline/name).read_bytes()
        require(sha(files[name]) == expected, 'Baseline hash mismatch: '+name)
    inspect_efi(files['start.efi'], files['el2launch.efi'], files['kernel-options.txt'].decode(),
                source_directory, 'sq2.vulkan=mesa')
    require(files['Image'][0x38:0x3c] == b'ARMd', 'ARM64 Image header missing')
    require(files['android-el2.dtb'][:4] == b'\xd0\x0d\xfe\xed', 'DTB header missing')
    payload = a.payload.read_bytes()
    require(re.fullmatch('[0-9a-f]{64}', a.payload_sha256) and sha(payload) == a.payload_sha256,
            'Explicit payload SHA-256 mismatch')
    require(payload[:2] == b'\x1f\x8b', 'Expected gzip initramfs')
    original_grub = a.grub_input.read_bytes()
    candidate_grub = append_entry(original_grub, source_directory, a.directory)
    edits = {}
    for name, count in [('start.efi', 1), ('el2launch.efi', 5)]:
        files[name], edits[name] = rewrite_efi(files[name], source_directory, a.directory, count, a.graphics_argument)
    options = [s for _, _, s in utf16_strings(files['el2launch.efi']) if s.startswith('console=')]
    require(len(options) == 1, 'Expected one effective command line')
    files['kernel-options.txt'] = (options[0]+'\n').encode()
    files['initramfs.cpio.gz'] = payload
    inspect_efi(files['start.efi'], files['el2launch.efi'], options[0], a.directory, a.graphics_argument)
    dest = a.output/'EFI'/a.directory
    dest.mkdir(parents=True)
    for name, data in files.items():
        (dest/name).write_bytes(data)
    (dest/'SHA256SUMS').write_text(''.join(sha(b)+'  '+n+'\n' for n,b in sorted(files.items())))
    (a.output/'EFI/BOOT').mkdir()
    (a.output/'EFI/BOOT/grub.cfg').write_bytes(candidate_grub)
    (a.output/'baseline').mkdir()
    (a.output/'baseline/grub.cfg').write_bytes(original_grub)
    lines = difflib.unified_diff(original_grub.decode().splitlines(True),
                    candidate_grub.decode().splitlines(True), 'a/EFI/BOOT/grub.cfg', 'b/EFI/BOOT/grub.cfg')
    patch = ''.join(line if line.endswith('\n') else line+'\n\\ No newline at end of file\n'
                    for line in lines)
    (a.output/'GRUB.patch').write_text(patch)
    (a.output/'ROLLBACK.sh').write_bytes(rollback_script(original_grub))
    (a.output/'ROLLBACK.sh').chmod(0o755)
    report = dict(kind='sq2-618-offline-debug-entry', directory=a.directory,
                  graphics_argument=a.graphics_argument, kernel_release=baseline_manifest['kernel_release'],
                  baseline_sha256=baseline_manifest['files'], payload_sha256=sha(payload),
                  grub_before_sha256=sha(original_grub), grub_after_sha256=sha(candidate_grub),
                  efi_edits=edits, effective_utf16_options=options[0],
                  original_default_and_fallback_unchanged=True, shared_firmware_unchanged=True,
                  image_and_dtb_unchanged=True, device_deployed=False, hardware_tested=False,
                  private_identity_present=True, payload_contents_verified_by_this_tool=False,
                  rollback='Restore original GRUB in an explicit offline copy; retain unreferenced debug files',
                  roles={'MODIFIED_FILE': str((a.output/'EFI/BOOT/grub.cfg').resolve()),
                         'DIFF_FILE': str((a.output/'GRUB.patch').resolve()),
                         'VERIFICATION': str((a.output/'VERIFICATION.txt').resolve()),
                         'ROLLBACK': str((a.output/'ROLLBACK.sh').resolve())})
    report['files'] = {str(p.relative_to(a.output)).replace('\\','/'):sha(p.read_bytes())
                       for p in sorted(a.output.rglob('*')) if p.is_file()}
    (a.output/'MANIFEST.json').write_text(json.dumps(report, indent=2)+'\n')
    # Matching read-only command/input across three separate menu copies.
    restored = a.output/'rollback-check'
    (restored/'EFI/BOOT').mkdir(parents=True)
    shutil.copyfile(a.output/'MANIFEST.json', restored/'MANIFEST.json')
    shutil.copyfile(a.output/'EFI/BOOT/grub.cfg', restored/'EFI/BOOT/grub.cfg')
    results = []
    def run(role, command):
        p = subprocess.run(command, capture_output=True, text=True)
        results.append(dict(role=role, command=command, stdout=p.stdout, stderr=p.stderr, exit=p.returncode))
        require(p.returncode == 0, role+' failed: '+p.stderr)
    inspect = [sys.executable, str(Path(__file__).resolve()), '--inspect-grub']
    run('BASELINE', inspect+[str((a.output/'baseline/grub.cfg').resolve())])
    run('MODIFIED', inspect+[str((a.output/'EFI/BOOT/grub.cfg').resolve())])
    run('ROLLBACK_EXECUTE', ['sh', str((a.output/'ROLLBACK.sh').resolve()), str(restored.resolve())])
    run('ROLLBACK', inspect+[str((restored/'EFI/BOOT/grub.cfg').resolve())])
    require((restored/'EFI/BOOT/grub.cfg').read_bytes() == original_grub, 'Restored hash differs')
    (a.output/'VERIFICATION.txt').write_text(json.dumps({'commands': results, 'hashes': report['files'],
        'restored_sha256': sha((restored/'EFI/BOOT/grub.cfg').read_bytes()),
        'observed': 'two original entries -> third opt-in entry -> original two entries; candidate retained'}, indent=2)+'\n')
    report['files']['VERIFICATION.txt'] = sha((a.output/'VERIFICATION.txt').read_bytes())
    (a.output/'MANIFEST.json').write_text(json.dumps(report, indent=2)+'\n')
    verify_output(a.output)
    for path in report['roles'].values():
        require(Path(path).read_bytes(), 'Missing role artifact: '+path)
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inspect-grub', type=Path)
    p.add_argument('--verify-output', type=Path)
    p.add_argument('--baseline', type=Path, default=ROOT/'private/build-inputs/kernel-6.18/EFI/SQ2L618C')
    p.add_argument('--manifest', type=Path, default=ROOT/'configs/kernel-baseline.json')
    p.add_argument('--payload', type=Path)
    p.add_argument('--payload-sha256')
    p.add_argument('--grub-input', type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--directory', default='SQ2QDbg')
    p.add_argument('--graphics-argument', default='sq2.vulkan=qcom')
    a = p.parse_args()
    try:
        if a.inspect_grub:
            print(json.dumps(inspect_grub(a.inspect_grub.read_bytes()), sort_keys=True)); return
        if a.verify_output:
            report = verify_output(a.verify_output)
        else:
            require(all((a.payload, a.payload_sha256, a.grub_input, a.output)),
                    '--payload, --payload-sha256, --grub-input and --output are required')
            report = build(a)
        print(json.dumps(report, indent=2))
    except (ValueError, OSError, KeyError, struct.error) as exc:
        p.exit(2, str(exc)+'\n')


if __name__ == '__main__':
    main()
