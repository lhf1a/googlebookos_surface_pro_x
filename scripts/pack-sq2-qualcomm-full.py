#!/usr/bin/env python3
"""Offline, opt-in 6.18 full GLES/Vulkan delta. No deployment or device access."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cpio', ROOT/'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpio)
LIBRARIES = set('libEGL_adreno.so libGLESv1_CM_adreno.so libGLESv2_adreno.so '
    'eglSubDriverAndroid.so libquest2-egl-wrapper.so libadreno_utils.so libgsl.so '
    'libq2v77glnext.so libq2v77qgl.so libqti-mapper3-bridge.so '
    'libquest2-ion-dmaheap.so libquest2-v77-ahb.so libquest2-vulkan-wrapper.so '
    'vulkan-v77-bridge.so'.split())
CPU_SHIM = 'libsq2map.so'
# This is a private artifact integrity pin, NOT a device identity/SoC whitelist.
# Rebuild with prepare-quest2-cpu-access.py then prepare-quest2-618-layout.py and prepare-quest2-618-direct.py.
DEPLOYABLE_RUNTIME_SUMS_SHA = '395520969a5f1ce766dabde0af258204a17d9dc5c5ea37783f1937590b5c0689'
# Paired 6.18 Venus PM/min-output/safe-close candidate, preserved as a private input.
VENUS_618_BASE_SHA = '525d739f0e1d461aab686296d21262cb3e501e3ff591aec69ae0f0d043052c48'
HOOK = b'closed-vulkan-start > /logs/closed-vulkan.txt 2>&1 || fail closed-vulkan-overlay\n'
NEW_HOOK = b'qualcomm-full-start > /logs/qualcomm-full.txt 2>&1 || fail qualcomm-full-graphics\n'

def sha(data):
    return hashlib.sha256(data).hexdigest()

def require(ok, message):
    if not ok:
        raise ValueError(message)

def inspect(path):
    entries = cpio.entries(gzip.decompress(path.read_bytes()))
    init = entries['init'][1]
    full = NEW_HOOK in init
    helper = entries.get('bin/qualcomm-full-start', (0, b''))[1]
    chrome = full and b'chrome-graphics-cache-v1' in helper
    legacy = full and not chrome and b'graphics-cache-v1' in helper
    receipt_required = chrome or legacy
    return dict(backend_selector='qualcomm-full-opt-in' if full else 'baseline-vulkan-selector',
                original_mesa_present=all('etc/googlebook-security/lib64/egl/'+n in entries
                    for n in ('libEGL_freedreno.so', 'libGLESv1_CM_freedreno.so', 'libGLESv2_freedreno.so')),
                full_runtime_libraries=sum('etc/sq2-graphics-full/'+n in entries for n in LIBRARIES|{CPU_SHIM}),
                cache_receipt_required=receipt_required,
                cache_cleanup_trigger='preboot-receipt' if receipt_required else ('postboot-on-demand' if full else None),
                cache_scope='all-app-graphics' if legacy else ('chrome-graphics-only' if full else None),
                cache_backup='transaction-backup' if legacy else ('none' if full else None),
                cache_receipt_schema='chrome-graphics-cache-v1' if chrome else ('graphics-cache-v1' if legacy else None),
                cache_receipt_single_use=full and b'mkdir "$receipt/activation"' in entries.get('bin/qualcomm-full-start', (0, b''))[1],
                hardware_tested=False)

def build(a):
    require(not a.output.exists(), 'Fresh output directory required')
    baseline_manifest = json.loads((ROOT/'configs/kernel-baseline.json').read_text())
    base = a.base.read_bytes()
    require(sha(base) in (baseline_manifest['files']['initramfs.cpio.gz'], VENUS_618_BASE_SHA),
            'Accepted baseline hash mismatch')
    before = cpio.entries(gzip.decompress(base))
    mode, init = before['init']
    require(init.count(HOOK) == 1 and NEW_HOOK not in init, 'Unexpected graphics hook')
    hashes = {}
    runtime_sums = (a.runtime/'SHA256SUMS').read_bytes()
    for line in runtime_sums.decode('utf-8').splitlines():
        digest, name = line.split()
        require(Path(name).name == name and name not in hashes, 'Invalid runtime manifest')
        hashes[name] = digest
    libraries = {n for n in hashes if n.endswith('.so')}
    require(libraries == LIBRARIES|{CPU_SHIM} and set(hashes) == libraries|{'qgl_config.txt'},
            'Exact deployable 15-library layout8/bank15/Direct stack plus qgl_config required; historical 14-library/CPU4 runtime is not deployable here')
    blobs = {}
    for name in libraries:
        path = a.runtime/name
        require(path.is_file() and not path.is_symlink(), 'Not a regular library: '+name)
        data = path.read_bytes()
        require(sha(data) == hashes[name], 'Runtime hash mismatch: '+name)
        require(data[:6] == b'\x7fELF\x02\x01' and int.from_bytes(data[18:20], 'little') == 183, 'Not arm64 ELF: '+name)
        blobs[name] = data
    private_mapper_path = b'/vendor/lib64/egl/libsq2map.so\0'
    require((CPU_SHIM in libraries) == (blobs['libqti-mapper3-bridge.so'].count(private_mapper_path) == 1),
            'Private CPU shim and mapper selector must be paired')
    require(sha(runtime_sums) == DEPLOYABLE_RUNTIME_SUMS_SHA,
            'Private deployable runtime manifest pin mismatch; rebuild the accepted layout8/bank15/Direct candidate')
    config = a.runtime/'qgl_config.txt'
    require(config.is_file() and not config.is_symlink() and sha(config.read_bytes()) == hashes['qgl_config.txt'],
            'Runtime hash mismatch: qgl_config.txt')
    require(config.read_bytes() == before['etc/sq2-vulkan-closed/qgl_config.txt'][1],
            'Runtime qgl_config differs from paired baseline config')
    blobs['LIBRARY_COUNT'] = (str(len(libraries))+'\n').encode()
    for name in ('qgl_config.txt', 'sq2-closed-vulkan.rc', 'vulkan-version.xml'):
        blobs[name] = before['etc/sq2-vulkan-closed/'+name][1]
    mesa = {}
    for name in ('egl/libEGL_freedreno.so', 'egl/libGLESv1_CM_freedreno.so',
                 'egl/libGLESv2_freedreno.so', 'hw/vulkan.freedreno.so'):
        mesa['lib64/'+name] = sha(before['etc/googlebook-security/lib64/'+name][1])
    blobs['MESA_SHA256SUMS'] = ''.join(f'{v}  {k}\n' for k,v in sorted(mesa.items())).encode()
    blobs['SHA256SUMS'] = ''.join(f'{sha(v)}  {k}\n' for k,v in sorted(blobs.items())).encode()
    delta = {'init': (mode, init.replace(HOOK, NEW_HOOK)),
             'etc/sq2-graphics-full': (0o40755, b'')}
    delta.update({'etc/sq2-graphics-full/'+n:(0o100644,b) for n,b in blobs.items()})
    for target, source in [('bin/qualcomm-full-start', 'probe/graphics/qualcomm-full-start'),
                            ('bin/chrome-graphics-cache', 'probe/graphics/chrome-graphics-cache.sh')]:
        delta[target] = (0o100755, (ROOT/source).read_bytes().replace(b'\r\n',b'\n'))
    # Reappend the exact credential member LAST, without exposing its contents.
    rest, credential = base, None
    while rest:
        dec = zlib.decompressobj(31)
        data = dec.decompress(rest)
        require(dec.eof, 'Incomplete gzip member')
        files = cpio.entries(data)
        if 'etc/shadow' in files and 'bin/dropbear' in files:
            credential = rest[:len(rest)-len(dec.unused_data)]
        rest = dec.unused_data
    require(credential is not None, 'Private baseline SSH tail not found')
    archive = b''.join(cpio.entry(n,b,m) for n,(m,b) in delta.items())+cpio.entry('TRAILER!!!',b'',0)
    archive += b'\0'*(-len(archive)%512)
    append = gzip.compress(archive,mtime=0)+credential
    candidate = base+append
    after = cpio.entries(gzip.decompress(candidate))
    changed = {n for n in before.keys()|after.keys() if before.get(n)!=after.get(n)}
    require(changed == set(delta), 'Effective last-entry delta mismatch')
    require(all(after[n] == before[n] for n in before if n != 'init'), 'Non-init parent bytes changed')
    a.output.mkdir(parents=True)
    (a.output/'baseline.cpio.gz').write_bytes(base)
    (a.output/'initramfs.cpio.gz').write_bytes(candidate)
    (a.output/'initramfs.append.gz').write_bytes(append)
    # Native append patch reconstructs the exact candidate from the pristine base.
    apply = f'''#!/bin/sh
set -eu
[ "$#" = 2 ] && [ ! -e "$2" ] || exit 64
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
actual=$(sha256sum "$1"); [ "${{actual%% *}}" = {sha(base)} ] || exit 65
actual=$(sha256sum "$here/initramfs.append.gz"); [ "${{actual%% *}}" = {sha(append)} ] || exit 66
cat "$1" "$here/initramfs.append.gz" > "$2"
actual=$(sha256sum "$2"); [ "${{actual%% *}}" = {sha(candidate)} ]
'''
    rollback = f'''#!/bin/sh
set -eu
[ "$#" = 1 ] && [ -f "$1" ] && [ ! -L "$1" ] || exit 64
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
actual=$(sha256sum "$here/baseline.cpio.gz"); [ "${{actual%% *}}" = {sha(base)} ] || exit 65
cp "$here/baseline.cpio.gz" "$1"
actual=$(sha256sum "$1"); [ "${{actual%% *}}" = {sha(base)} ]
echo RESTORED_ACCEPTED_MESA_INITRAMFS
'''
    for name, data in [('APPLY.sh', apply), ('ROLLBACK.sh', rollback)]:
        (a.output/name).write_text(data); (a.output/name).chmod(0o755)
    records = []
    def run(role, args):
        p = subprocess.run(args, capture_output=True, text=True)
        records.append(dict(role=role,command=args,stdout=p.stdout,stderr=p.stderr,exit=p.returncode))
        require(p.returncode == 0, role+' failed')
    command = [sys.executable,str(Path(__file__).resolve()),'--inspect']
    run('BASELINE',command+[str((a.output/'baseline.cpio.gz').resolve())])
    run('MODIFIED',command+[str((a.output/'initramfs.cpio.gz').resolve())])
    restored = a.output/'restored.cpio.gz'
    shutil.copyfile(a.output/'initramfs.cpio.gz',restored)
    run('ROLLBACK_EXECUTE',['sh',str((a.output/'ROLLBACK.sh').resolve()),str(restored.resolve())])
    run('ROLLBACK',command+[str(restored.resolve())])
    require(sha(restored.read_bytes()) == sha(base), 'Restored hash mismatch')
    run('REAPPLY',['sh',str((a.output/'APPLY.sh').resolve()),str(restored.resolve()),str((a.output/'reapplied.cpio.gz').resolve())])
    require((a.output/'reapplied.cpio.gz').read_bytes() == candidate, 'Exact patch roundtrip mismatch')
    report = dict(schema=1,baseline_sha256=sha(base),candidate_sha256=sha(candidate),
        venus_618_omx_prerequisites=sha(base) == VENUS_618_BASE_SHA,
        append_sha256=sha(append), changed_effective_paths=sorted(changed), runtime_sha256={n:sha(blobs[n]) for n in sorted(libraries)},
        private_cpu_shim_present=CPU_SHIM in libraries,
        private_runtime_manifest_sha256=sha(runtime_sums),
        layout='original donor channel extraction (8 for this verified 6.18 path), retained bank15',
        render_mode='explicit Direct (sysmem) workaround; GMEM root cause unresolved',
        performance_and_power_verified=False,
        cache_receipt_required=False, cache_receipt_single_use=False,
        cache_cleanup_trigger='postboot-on-demand', cache_scope='chrome-graphics-only', cache_backup='none',
        cache_receipt_schema=None,
        all_original_non_init_files_unchanged=True, original_mesa_preserved=True,
        password_addon_last=True, kernel_release=baseline_manifest['kernel_release'],
        hardware_tested=False, deployed=False, rebooted=False, private_identity_present=True,
        activation='No cache receipt required or consumed; boot never cleans caches. Chrome-only/no-backup cleanup is a separate explicit post-boot troubleshooting operation.',
        known_limitations=['Packaging is not device acceptance; see separate real-device evidence for the optional CPU shim',
            'Full desktop/cold-boot acceptance and cross-device validation must be recorded separately',
            'Boot does not clear caches, inspect readiness receipts or require any cache operation',
            'If a post-boot issue warrants cleanup, explicitly stop Chrome and use the separate Chrome-only/no-backup helper; other apps and old backups are not included',
            'No claim that a backend switch or basic graphics pass fixes every application or video'])
    (a.output/'MANIFEST.json').write_text(json.dumps(report,indent=2)+'\n')
    (a.output/'VERIFICATION.txt').write_text(json.dumps(dict(commands=records,hashes=report),indent=2)+'\n')
    for name in ('initramfs.cpio.gz','initramfs.append.gz','APPLY.sh','VERIFICATION.txt','ROLLBACK.sh'):
        require((a.output/name).read_bytes(), 'Role reopen failed: '+name)
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inspect',type=Path)
    p.add_argument('--base',type=Path,default=ROOT/'private/build-inputs/kernel-6.18/EFI/SQ2L618C/initramfs.cpio.gz')
    p.add_argument('--runtime',type=Path)
    p.add_argument('--output',type=Path)
    a=p.parse_args()
    if a.inspect:
        print(json.dumps(inspect(a.inspect),sort_keys=True));return
    require(a.runtime is not None and a.output is not None, '--runtime and --output required')
    print(json.dumps(build(a),indent=2))

if __name__=='__main__':main()
