#!/usr/bin/env python3
"""Restore the private v77 GLES candidate alongside the accepted v8 Vulkan set.

Host-only: no device access, deployment or reboot. Original donor files and the
parent initramfs are never modified. The GLES hardware-bitmap upload defect is
NOT fixed by this reconstruction. This is a separate full-stack debug runtime,
not an accepted replacement for the Mesa baseline.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE_SHA = '5b3a70c349735e58b404b0d3825dc0dde12ae991706cbbab7e15ed65c9911185'
GL = {
    'libEGL_adreno.so': '026b25fe82d174fe9e4a327d4e6f9b05be800a59853b5cb729cae53a6b2892fc',
    'libGLESv1_CM_adreno.so': '396987f605b4f7aa1be5d10cacf0216bce400a3c13bb36f9cf29fac1d9bc5ace',
    'libGLESv2_adreno.so': '99d4c18d35e69e8589f2550b242d5f6722031404852b9831e328df8d1778551f',
    'eglSubDriverAndroid.so': 'eaa30f3fa6ed9dcf79ad12c9c214aab9d11aebd2cf6d8b3388e8d0075ef17012',
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), ROOT / 'scripts' / (name + '.py'))
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


def require(value, message):
    if not value:
        raise ValueError(message)


def run(*args):
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True)
    require(result.returncode == 0, f'{args}: exit={result.returncode}\n{result.stdout}\n{result.stderr}')
    return result.stdout


def patch_gles(original, audit):
    """Replay exact historical patches; each complete intermediate is pinned."""
    require(sha(original) == GL['libGLESv2_adreno.so'], 'Linked GLES input differs')
    data = bytearray(original)
    for address, old, new in [(0x2a6100, 0x530a3506, 0x52800086),
                              (0x2a61b0, 0x5280020a, 0x528001ea)]:
        require(struct.unpack_from('<I', data, address)[0] == old, 'CPU-layout instruction differs')
        struct.pack_into('<I', data, address, new)
    require(sha(data) == '71eba2e1d10daaf842f8667654af711c63582dc9be3c42df14d6d4191faac668', 'CPU4/bank15 mismatch')
    require(struct.unpack_from('<I', data, 0x1bb758)[0] == 0x34000400, 'PBO eligibility differs')
    struct.pack_into('<I', data, 0x1bb758, 0x14000020)
    require(sha(data) == '677287b130ac3ac0dfd2c0a8bc1ac73b642466c64f67d39f662a2bb09d823ded', 'PBO fallback mismatch')
    zero = load('prepare-quest2-zero-area')
    data, zero_report, jump, stub = zero.inspect_and_patch(bytes(data))
    zero_dir = audit / 'zero-area'; zero_dir.mkdir()
    zero_report['qemu'] = zero.qemu_verify(zero_dir, jump, stub)
    (audit / 'zero-area.json').write_text(json.dumps(zero_report, indent=2) + '\n')
    prune = load('prepare-quest2-fence-prune')
    assembly = audit / 'prune.S'; assembly.write_text(prune.ASSEMBLY)
    obj, binary = audit / 'prune.o', audit / 'prune.bin'
    run('aarch64-linux-gnu-gcc', '-c', assembly, '-o', obj)
    run('aarch64-linux-gnu-objcopy', '-O', 'binary', '-j', '.text', obj, binary)
    stub_bytes = binary.read_bytes()
    require(not any(data[prune.STUB:0x3eb000]), 'Fence stub padding not zero')
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phsize, phcount = struct.unpack_from('<HH', data, 54)
    require(phsize == 56 and phcount == 12, 'Unexpected program headers')
    matches = [i for i in range(phcount) if struct.unpack_from('<IIQQQQQQ', data, phoff+i*56) ==
               (1,5,0xe8000,0xe8000,0xe8000,0x3022b4,0x3022b4,0x1000)]
    require(len(matches) == 1, 'Fence RX layout mismatch')
    require(struct.unpack_from('<4I', data, prune.ENTRY-8) ==
            (0xf9404109,0xf9400534,0xf95fd681,0xb4000181), 'Fence entry differs')
    struct.pack_into('<I', data, prune.ENTRY, zero.branch(prune.ENTRY, prune.STUB) | 0x80000000)
    data[prune.STUB:prune.STUB+len(stub_bytes)] = stub_bytes
    struct.pack_into('<QQ', data, phoff+matches[0]*56+32,
                     prune.STUB+len(stub_bytes)-0xe8000, prune.STUB+len(stub_bytes)-0xe8000)
    require(sha(data) == prune.OUTPUT_SHA, 'Historical fence-prune complete ELF mismatch')
    old, new = b'libllvm-glnext.so\0', b'libq2v77glnext.so\0'
    require(data.count(old) == 1 and len(old) == len(new), 'Compiler name occurrence differs')
    data = bytes(data).replace(old, new)
    require(sha(data) == 'd7e41532779ba5be94eae207ce3ad6800489a93ba16ccc1fe7b80aa9778995ec',
            'Final combined-v5 GLES ELF differs')
    return data


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base', type=Path, required=True)
    p.add_argument('--donor-lib64', type=Path, required=True)
    p.add_argument('--egl-wrapper', type=Path, required=True,
                   help='Fresh ARM64 Android build of probe/graphics/quest2-egl-wrapper.c')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    require(not a.output.exists(), 'Refusing existing output')
    require(shutil.which('patchelf') and shutil.which('qemu-aarch64') and
            shutil.which('aarch64-linux-gnu-objcopy'), 'Requires patchelf, qemu-aarch64 and AArch64 objcopy')
    require(shutil.which('aarch64-linux-gnu-gcc') or shutil.which('clang'),
            'Requires AArch64 GCC or clang with the installed AArch64 GNU libc toolchain')
    base = a.base.read_bytes(); require(sha(base) == BASE_SHA, 'Unexpected parent initramfs')
    entries = load('pack-sq2-vulkan').entries(gzip.decompress(base))
    donor_manifest = json.loads((ROOT/'configs/quest2-gles-v77-inputs.json').read_text(encoding='utf-8-sig'))
    original_hashes = {}
    for n, digest in donor_manifest['files'].items():
        path = a.donor_lib64 / n
        require(not path.is_symlink() and sha(path.read_bytes()) == digest, 'Donor mismatch: '+n)
        original_hashes[str(path.resolve())] = digest
    wrapper_bytes = a.egl_wrapper.read_bytes()
    require(wrapper_bytes[:6] == b'\x7fELF\x02\x01' and int.from_bytes(wrapper_bytes[18:20], 'little') == 183,
            'Expected ARM64 ELF wrapper')
    runtime_expected = load('pack-sq2-closed-vulkan').EXPECTED
    # Only reuse actual pinned v8 libraries and qgl_config, never credential files.
    selected = {n: h for n,h in runtime_expected.items() if n.endswith('.so') or n == 'qgl_config.txt'}
    require(len([n for n in selected if n.endswith('.so')]) == 9, 'Expected nine v8 libraries')
    for name, digest in selected.items():
        require(sha(entries['etc/sq2-vulkan-closed/'+name][1]) == digest, 'v8 mismatch: '+name)
    a.output.mkdir(parents=True)
    audit = a.output.parent / (a.output.name + '-build-evidence'); audit.mkdir()
    if not shutil.which('aarch64-linux-gnu-gcc'):
        # The historical instruction harness names GCC. A local adapter uses
        # the installed clang cross target; it never changes a system tool.
        compiler = Path(shutil.which('clang')).resolve()
        shim = audit/'aarch64-linux-gnu-gcc'
        shim.write_text('#!/bin/sh\nexec "'+str(compiler)+'" --target=aarch64-linux-gnu "$@"\n')
        shim.chmod(0o755)
        os.environ['PATH'] = str(audit.resolve())+os.pathsep+os.environ['PATH']
    records = {}
    for name, digest in selected.items():
        (a.output/name).write_bytes(entries['etc/sq2-vulkan-closed/'+name][1])
        records[name] = {'source': 'parent-initramfs:etc/sq2-vulkan-closed/'+name,
                         'sha256': digest, 'operation': 'byte-identical accepted Vulkan v8 reuse'}
    for name, digest in GL.items():
        source = a.donor_lib64/'egl'/name
        (a.output/name).write_bytes(source.read_bytes())
        run('patchelf', '--set-rpath', '$ORIGIN', a.output/name)
        require(sha((a.output/name).read_bytes()) == digest, 'Linked donor hash mismatch: '+name)
        records[name] = {'source': str(source.resolve()), 'input_sha256': sha(source.read_bytes()),
                         'operation': 'historical $ORIGIN link; GLES2 also exact CPU4/bank15/PBO/zero-area/fence-prune/private-compiler patches'}
    gl_path = a.output/'libGLESv2_adreno.so'
    gl_path.write_bytes(patch_gles(gl_path.read_bytes(), audit))
    wrapper = a.output/'libquest2-egl-wrapper.so'; wrapper.write_bytes(wrapper_bytes)
    for name in ('libEGL_adreno.so','libquest2-ion-dmaheap.so','libqti-mapper3-bridge.so'):
        run('patchelf', '--add-needed', name, wrapper)
    run('patchelf', '--set-rpath', '$ORIGIN', wrapper)
    needed = run('patchelf', '--print-needed', wrapper).splitlines()
    require(needed[:3] == ['libqti-mapper3-bridge.so','libquest2-ion-dmaheap.so','libEGL_adreno.so'],
            'EGL native bridge load order differs')
    records[wrapper.name] = {'source': str(a.egl_wrapper.resolve()), 'input_sha256': sha(wrapper_bytes),
                             'operation': 'fresh NDK build of existing EGL adapter + historical linked dependency order'}
    for name, record in records.items():
        data = (a.output/name).read_bytes()
        record.update(sha256=sha(data), bytes=len(data))
        if name.endswith('.so'):
            require(data[:6] == b'\x7fELF\x02\x01' and int.from_bytes(data[18:20],'little') == 183,
                    'Non ARM64 library: '+name)
            record['needed'] = run('patchelf','--print-needed',a.output/name).splitlines()
            record['runpath'] = run('patchelf','--print-rpath',a.output/name).strip()
    require(sha(a.base.read_bytes()) == BASE_SHA, 'Parent changed during restoration')
    for path, digest in original_hashes.items():
        require(sha(Path(path).read_bytes()) == digest, 'Donor changed during restoration')
    require(len([n for n in records if n.endswith('.so')]) == 14, 'Full runtime must contain 14 libraries')
    report = {'schema': 1, 'kind': 'private-full-qualcomm-v77-debug-candidate',
              'base_sha256': BASE_SHA, 'base': str(a.base.resolve()), 'files': records,
              'original_inputs_unchanged': True, 'gles2_matches_historical_combined_v5': True,
              'vulkan_v8_byte_identical': True, 'angle_used_for_gles': False,
              'wrapper_source_sha256': sha((ROOT/'probe/graphics/quest2-egl-wrapper.c').read_bytes()),
              'device_operations': [], 'kernel_6_18_runtime_tested': False,
              'limitations': ['HWUI HARDWARE Bitmap external EGLImage CPU upload historically reads zero; unresolved',
                             'full SurfaceFlinger/UI correctness not established',
                             'GLES native/window wrapper keeps historical stderr tracing; debug entry only',
                             'v8 shared libraries plus restored GLES were not hardware-regressed as one set',
                             'keep original minigbm/mapper5/DRM HWC, Mesa baseline and rollback',
                             'cross-backend switches must invalidate confirmed graphical caches for every user/app before app restart'],
              'host_validation': {'zero_area_actual_arm64_instructions_qemu_cases':176,
                                  'all_historical_gles_intermediate_and_final_hashes_matched': True,
                                  'fence_stub_actual_instructions_reassembled_and_complete_elf_hash_matched': True}}
    (a.output/'SHA256SUMS').write_text(''.join(f"{v['sha256']}  {n}\n" for n,v in sorted(records.items())))
    (a.output/'RUNTIME.json').write_text(json.dumps(report,indent=2)+'\n')
    for n,v in records.items(): require(sha((a.output/n).read_bytes()) == v['sha256'], 'Reopen mismatch')
    require(json.loads((a.output/'RUNTIME.json').read_text()) == report, 'Manifest reopen mismatch')
    print('QUALCOMM_FULL_RUNTIME_RESTORED libraries=14 private=1 deployed=0 gles_hwui_fixed=0')
    print(a.output.resolve())


if __name__ == '__main__':
    main()
