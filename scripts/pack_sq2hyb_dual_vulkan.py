#!/usr/bin/env python3
"""Build a standalone SQ2Hyb Dual-Vulkan initramfs candidate without touching any disk or target."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / 'scripts'
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

import build_hybrid_vulkan_router  # noqa: E402
from disk_image import module, require  # noqa: E402
import full_image  # noqa: E402

DEFAULT_BASE_INITRAMFS = Path('/mnt/spx-workspace/chromebookOS/out/test-backlight-20261113/initramfs-patched/initramfs.cpio.gz')
DEFAULT_V21_ZIP = Path('/mnt/spx-workspace/exports/quest2-private-100b6d7-20261110/quest2-v21-release-4k-private-resources-100b6d7-20261110.zip')
DEFAULT_TURNIP_DIR = ROOT / 'out/sq2-turnip/runtime'
DEFAULT_POLICY_CONF = ROOT / 'configs/sq2-vulkan-policy.conf'
DEFAULT_OUTPUT_DIR = Path('/mnt/spx-workspace/chromebookOS/out/sq2hyb-dual-vulkan-20261114')

V21_INITRAMFS_MEMBER = 'quest2-v21-release-4k-private-resources/private/build-inputs/kernel-6.18/EFI/SQ2Nat/initramfs.cpio.gz'
V21_INITRAMFS_SHA256 = '0ada8ee0db271abc7a25f5c6ee63b0d484b03bed75c5352ae8f5d65f0902beb5'

V21_QTI_VULKAN_PINNED = {
    'lib64/hw/vulkan.freedreno.so': 'b6704dc13dd7336f6b6a8e1f17980f94e7289e35d57d385c1f36b18a3c11ea84',
    'lib64/vulkan.qti-native.so': '3d67ae4ef6cbb7339f1ddaa27cfed61931f82b5fdcd86767b5e8f765bbc95af6',
    'lib64/libquest2-native-vulkan-memory.so': '95fdf4fcb84ab93bb2f515cfcf2ce81fae7d81731be2b66ba061ea264aebbf0f',
    'lib64/libq2v77glnext.so': '17a9cb625c816bf9560489b53e15f20ad356db04ee0f1e83e5fbac03cb0fe3f3',
    'lib64/libq2v77qgl.so': '100a97a7c9cfa4b6d554cc1a8960a920de963aeba3b6c3a6bc2ea8d986472652',
    'lib64/qgl_config.txt': '1f93a7a1446b2e934f12f23aa55514ee41852bf6df7851788d053cbd90e1f008',
}

BASE_QTI_SHARED_PINNED = {
    'lib64/libgsl.so': 'ca8c2e494915ecb6',
    'lib64/libadreno_utils.so': '6ef886f4b2d0d648',
    'lib64/libquest2-system-ion.so': '84d2946f2bcdbfc0',
}

ANDROID_SYSTEM_VNDK_LIBS = {
    'libc.so',
    'libdl.so',
    'libm.so',
    'libz.so',
    'liblog.so',
    'libcutils.so',
    'libutils.so',
    'libc++.so',
    'libhardware.so',
    'libnativewindow.so',
    'libsync.so',
    'libhidlbase.so',
    'android.hardware.graphics.mapper@2.0.so',
    'android.hardware.graphics.mapper@3.0.so',
    'vendor.qti.hardware.display.mapper@2.0.so',
    'vendor.qti.hardware.display.mapper@3.0.so',
    'vendor.qti.hardware.display.mapperextensions@1.1.so',
}


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def verify_aarch64_elf(data, label):
    require(
        len(data) >= 64 and data[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', data, 18)[0] == 183,
        f'Not a valid ELF64 AArch64 binary: {label}',
    )


def extract_credential_member(raw_gzip, cpio_mod):
    """Verify multi-member gzip structure and return the final SSH credential gzip member."""
    rest = raw_gzip
    credential = None
    last_member = False
    while rest:
        dec = zlib.decompressobj(31)
        payload = dec.decompress(rest)
        require(dec.eof, 'Incomplete gzip member in base initramfs')
        member_len = len(rest) - len(dec.unused_data)
        member_bytes = rest[:member_len]
        files = cpio_mod.entries(payload)
        is_last = not dec.unused_data
        if {'bin/dropbear', 'bin/start-ssh', 'etc/shadow'} <= files.keys():
            credential = member_bytes
            last_member = is_last
        rest = dec.unused_data
    require(credential is not None and last_member, 'Trailing SSH credential gzip member missing or not final')
    return credential


def dt_needed(readelf_bin, elf_path):
    out = subprocess.check_output([str(readelf_bin), '-d', str(elf_path)], text=True)
    return [
        line.split('Shared library: [', 1)[1].split(']', 1)[0]
        for line in out.splitlines()
        if '(NEEDED)' in line and 'Shared library: [' in line
    ]


def simulate_boot_staging_dry_run(bodies, records, readelf_bin):
    """Simulate apply-security-overlay + qti-system-display-stage and verify staged vendor/."""
    with tempfile.TemporaryDirectory(prefix='sq2hyb-stage-dryrun-') as td:
        root = Path(td)
        sec_dir = root / 'etc/googlebook-security'
        qti_dir = root / 'etc/sq2-qti-system'
        vendor_dir = root / 'newroot/vendor'
        vendor_dir.mkdir(parents=True)

        for prefix, out_base in (('etc/googlebook-security', sec_dir), ('etc/sq2-qti-system', qti_dir)):
            for name, rec in records.items():
                if name != prefix and not name.startswith(prefix + '/'):
                    continue
                rel = '' if name == prefix else name[len(prefix) + 1:]
                dst = out_base / rel if rel else out_base
                mode = rec[1][1] & 0o170000
                if mode == 0o040000:
                    dst.mkdir(parents=True, exist_ok=True)
                elif mode == 0o120000:
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    dst.symlink_to(bodies[name].decode())
                else:
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    dst.write_bytes(bodies[name])

        sh_bin = shutil.which('busybox')
        shell_cmd = [sh_bin, 'sh', '-eu'] if sh_bin else ['/bin/sh', '-eu']
        script = f'''
cp -a "{sec_dir}/." "{vendor_dir}/"
source="{qti_dir}"
vendor="{vendor_dir}"
while IFS= read -r name; do
    test -n "$name"
    target="$vendor/$name"
    test ! -d "$target"
    mkdir -p "${{target%/*}}"
    rm -f "$target"
    cp -p "$source/root/$name" "$target"
done < "$source/INSTALL_FILES"
while read -r name destination; do
    test -n "$name" && test -n "$destination"
    target="$vendor/$name"
    test ! -d "$target"
    mkdir -p "${{target%/*}}"
    rm -f "$target"
    ln -s "$destination" "$target"
done < "$source/SYMLINKS"
for name in libsdmextension.so libsdm-color.so libsnapdragoncolor-manager.so libdpps.so; do
    test ! -d "$vendor/lib64/$name"
    rm -f "$vendor/lib64/$name"
done
for name in android.hardware.graphics.mapper@4.0-impl-qti-display.so mapper.minigbm.so mapper.qti.so; do
    test ! -d "$vendor/lib64/hw/$name"
    rm -f "$vendor/lib64/hw/$name"
done
for name in libquest2-vulkan-wrapper.so vulkan-v77-bridge.so libqti-mapper3-bridge.so libquest2-v77-ahb.so libquest2-egl-wrapper.so; do
    test ! -d "$vendor/lib64/egl/$name"
    rm -f "$vendor/lib64/egl/$name"
done
'''
        subprocess.run(shell_cmd, input=script, text=True, check=True)

        # Verify critical staged files & symlinks.
        require(
            (vendor_dir / 'lib64/hw/vulkan.freedreno.so').read_bytes()
            == bodies['etc/googlebook-security/lib64/hw/vulkan.freedreno.so'],
            'Staged vendor/lib64/hw/vulkan.freedreno.so was overwritten',
        )
        require(
            (vendor_dir / 'lib64/hw/vulkan.turnip.so').read_bytes()
            == bodies['etc/googlebook-security/lib64/hw/vulkan.turnip.so'],
            'Staged vendor/lib64/hw/vulkan.turnip.so mismatch',
        )
        require(
            (vendor_dir / 'lib64/hw/vulkan.qti.so').read_bytes()
            == bodies['etc/sq2-qti-system/root/lib64/hw/vulkan.qti.so'],
            'Staged vendor/lib64/hw/vulkan.qti.so mismatch',
        )
        require((vendor_dir / 'lib64/vulkan.qti-native.so').is_file(), 'Missing staged vulkan.qti-native.so')
        require((vendor_dir / 'etc/sq2-vulkan-policy.conf').is_file(), 'Missing staged sq2-vulkan-policy.conf')
        require(
            (vendor_dir / 'etc/permissions/android.hardware.vulkan.version-1_3.xml').is_file(),
            'Missing Vulkan 1.3 permission XML',
        )

        # Keep Mesa Freedreno GLES (not symlink to Adreno) and hybrid gralloc symlink.
        egl_freedreno = vendor_dir / 'lib64/egl/libEGL_freedreno.so'
        require(egl_freedreno.is_file() and not egl_freedreno.is_symlink(), 'libEGL_freedreno.so overwritten or missing')
        gralloc_link = vendor_dir / 'lib64/hw/gralloc.default.so'
        require(gralloc_link.is_symlink() and gralloc_link.readlink().as_posix() == 'gralloc.sq2_mesa.so',
                'gralloc.default.so symlink mismatch')

        for vk_link in ('lib64/egl/libq2v77glnext.so', 'lib64/egl/libq2v77qgl.so'):
            lp = vendor_dir / vk_link
            require(lp.is_symlink(), f'Missing symlink {vk_link}')
            resolved = (lp.parent / lp.readlink()).resolve()
            require(resolved.is_file(), f'Dangling symlink {vk_link} -> {lp.readlink()}')

        # Verify DT_NEEDED closure for staged Dual-Vulkan & QTI libs.
        vendor_lib_names = set()
        for sub in ('lib64', 'lib64/egl', 'lib64/hw'):
            d = vendor_dir / sub
            if d.is_dir():
                for item in d.iterdir():
                    if item.is_file() or (item.is_symlink() and item.exists()):
                        vendor_lib_names.add(item.name)

        checked = (
            'lib64/hw/vulkan.freedreno.so',
            'lib64/hw/vulkan.turnip.so',
            'lib64/hw/vulkan.qti.so',
            'lib64/vulkan.qti-native.so',
            'lib64/libquest2-native-vulkan-memory.so',
            'lib64/libq2v77glnext.so',
            'lib64/libq2v77qgl.so',
        )
        unresolved = {}
        for rel in checked:
            for lib in dt_needed(readelf_bin, vendor_dir / rel):
                if lib not in vendor_lib_names and lib not in ANDROID_SYSTEM_VNDK_LIBS:
                    unresolved.setdefault(rel, []).append(lib)
        require(not unresolved, f'Unresolved DT_NEEDED in staged vendor/: {unresolved}')

        return {
            'shell': ' '.join(shell_cmd),
            'checked_needed_files': list(checked),
            'vendor_lib_count': len(vendor_lib_names),
        }


def build_candidate(base_initramfs, v21_zip, turnip_dir, policy_conf, output_dir):
    require(base_initramfs.is_file(), f'Base initramfs missing: {base_initramfs}')
    require(v21_zip.is_file(), f'v21 private resources zip missing: {v21_zip}')
    clean_turnip_path = turnip_dir / 'vulkan.freedreno.so'
    clean_libdrm_path = turnip_dir / 'libdrm.so'
    require(clean_turnip_path.is_file(), f'Clean Turnip binary missing: {clean_turnip_path}')
    require(clean_libdrm_path.is_file(), f'Clean libdrm binary missing: {clean_libdrm_path}')
    require(policy_conf.is_file(), f'Policy conf missing: {policy_conf}')

    base_bytes = base_initramfs.read_bytes()
    base_sha = sha256_bytes(base_bytes)

    cpio_mod = module('pack_sq2_vulkan_cpio', 'pack-sq2-vulkan.py')
    encode = module('full_image_encode', 'pack-sq2-release.py').encode
    credential_member = extract_credential_member(base_bytes, cpio_mod)

    records, bodies = full_image.records_from_gzip(base_initramfs)
    before_records = {k: [v[0], list(v[1]), v[2]] for k, v in records.items()}
    before_bodies = dict(bodies)

    require(bodies.get('etc/sq2-graphics-mode') == b'hybrid-mesa-qti\n',
            f'Unexpected graphics mode in base: {bodies.get("etc/sq2-graphics-mode")!r}')
    require('etc/sq2-qti-system/root/lib64/sq2-qti-display/libsq2_sdm_backlight.so' in bodies,
            'Base SQ2Hyb initramfs is missing backlight compatibility shim')
    xml_13 = bodies.get('etc/googlebook-security/etc/permissions/android.hardware.vulkan.version-1_3.xml', b'')
    require(b'4206592' in xml_13, 'Base is missing global Vulkan 1.3 version permission XML')

    qti_prefix = 'etc/sq2-qti-system'
    for rel, prefix16 in BASE_QTI_SHARED_PINNED.items():
        path = f'{qti_prefix}/root/{rel}'
        require(path in bodies, f'Base is missing QTI shared dependency: {path}')
        actual = sha256_bytes(bodies[path])
        require(actual.startswith(prefix16), f'Base QTI shared hash mismatch for {rel}: {actual}')

    stage_sh = bodies.get('bin/qti-system-display-stage', b'')
    require(b'ORIGINAL_SHA256SUMS' not in stage_sh and b'INSTALLED_SHA256SUMS' not in stage_sh,
            'bin/qti-system-display-stage unexpectedly contains hash gate')

    # Read and verify the 6 QTI native Vulkan files from v21-release-4k.
    with zipfile.ZipFile(v21_zip) as zf:
        v21_bytes = zf.read(V21_INITRAMFS_MEMBER)
    require(sha256_bytes(v21_bytes) == V21_INITRAMFS_SHA256,
            f'v21 initramfs SHA256 mismatch: {sha256_bytes(v21_bytes)}')
    with tempfile.NamedTemporaryFile(suffix='.cpio.gz') as tf:
        tf.write(v21_bytes)
        tf.flush()
        _, v21_bodies = full_image.records_from_gzip(Path(tf.name))

    v21_vk_symlinks = [
        line.strip()
        for line in v21_bodies[f'{qti_prefix}/SYMLINKS'].decode().splitlines()
        if line.strip().split(None, 1)[0] in full_image.HYBRID_REQUIRED_QTI_VULKAN_SYMLINKS
    ]
    require(
        set(v21_vk_symlinks)
        == {'lib64/egl/libq2v77glnext.so ../libq2v77glnext.so', 'lib64/egl/libq2v77qgl.so ../libq2v77qgl.so'},
        f'Unexpected QTI Vulkan symlinks in v21: {v21_vk_symlinks}',
    )

    clean_turnip_bytes = clean_turnip_path.read_bytes()
    clean_libdrm_bytes = clean_libdrm_path.read_bytes()
    verify_aarch64_elf(clean_turnip_bytes, 'clean Turnip vulkan.turnip.so')
    verify_aarch64_elf(clean_libdrm_bytes, 'clean libdrm.so')
    for marker in full_image.LEGACY_TURNIP_SYSMEM_MARKERS:
        require(marker not in clean_turnip_bytes, f'Clean Turnip still contains legacy marker: {marker!r}')

    router_bytes = build_hybrid_vulkan_router.build_bytes()
    verify_aarch64_elf(router_bytes, 'vulkan.freedreno.so router')
    policy_bytes = policy_conf.read_bytes().replace(b'\r\n', b'\n')

    # Install v21 QTI native Vulkan closure (relocating vulkan.freedreno.so -> vulkan.qti.so).
    qti_vulkan_hashes = {}
    for rel, expected_sha in V21_QTI_VULKAN_PINNED.items():
        data = v21_bodies[f'{qti_prefix}/root/{rel}']
        actual = sha256_bytes(data)
        require(actual == expected_sha, f'v21 SHA256 mismatch for {rel}: {actual} != {expected_sha}')
        if rel.endswith('.so'):
            verify_aarch64_elf(data, f'v21 {rel}')
        dst_rel = 'lib64/hw/vulkan.qti.so' if rel == 'lib64/hw/vulkan.freedreno.so' else rel
        dst_path = f'{qti_prefix}/root/{dst_rel}'
        require(dst_path not in before_bodies, f'Unexpected pre-existing QTI Vulkan file in base: {dst_path}')
        bodies[dst_path] = data
        records[dst_path] = full_image.new_record(0o100644)
        qti_vulkan_hashes[dst_rel] = actual

    # Ensure both libdrm copies use Mesa 2.4.133's libdrm.so before recomputing QTI hashes.
    bodies['etc/googlebook-security/lib64/libdrm.so'] = clean_libdrm_bytes
    bodies[f'{qti_prefix}/root/lib64/sq2-qti-display/libdrm.so'] = clean_libdrm_bytes

    # Install Dual-Vulkan files in etc/googlebook-security.
    records.setdefault('etc/googlebook-security/etc', full_image.new_record(0o040755))
    bodies.setdefault('etc/googlebook-security/etc', b'')
    bodies['etc/googlebook-security/lib64/hw/vulkan.turnip.so'] = clean_turnip_bytes
    records['etc/googlebook-security/lib64/hw/vulkan.turnip.so'] = full_image.new_record(0o100644)
    bodies['etc/googlebook-security/lib64/hw/vulkan.freedreno.so'] = router_bytes
    records['etc/googlebook-security/lib64/hw/vulkan.freedreno.so'] = full_image.new_record(0o100644)
    bodies['etc/googlebook-security/etc/sq2-vulkan-policy.conf'] = policy_bytes
    records['etc/googlebook-security/etc/sq2-vulkan-policy.conf'] = full_image.new_record(0o100644)

    # Update vendor_file_contexts.
    vfc_path = f'{qti_prefix}/root/etc/selinux/vendor_file_contexts'
    require(vfc_path in bodies, 'Missing vendor_file_contexts in base')
    bodies[vfc_path] = full_image.append_selinux_context_lines(bodies[vfc_path])

    # Update SYMLINKS and recompute INSTALL_FILES, INSTALLED_SHA256SUMS, SHA256SUMS.
    existing_symlinks = [line.strip() for line in bodies[f'{qti_prefix}/SYMLINKS'].decode().splitlines() if line.strip()]
    for line in sorted(v21_vk_symlinks):
        if line not in existing_symlinks:
            existing_symlinks.append(line)
    bodies[f'{qti_prefix}/SYMLINKS'] = ('\n'.join(existing_symlinks) + '\n').encode()

    root_prefix = f'{qti_prefix}/root/'
    install_rel_paths = sorted(
        n.removeprefix(root_prefix)
        for n in bodies
        if n.startswith(root_prefix) and not (records[n][1][1] & 0o170000 == 0o040000)
    )
    bodies[f'{qti_prefix}/INSTALL_FILES'] = ('\n'.join(install_rel_paths) + '\n').encode()
    installed_sums = [f'{sha256_bytes(bodies[root_prefix + rel])}  {rel}' for rel in install_rel_paths]
    bodies[f'{qti_prefix}/INSTALLED_SHA256SUMS'] = ('\n'.join(installed_sums) + '\n').encode()

    non_root_sums = []
    for raw_line in bodies[f'{qti_prefix}/SHA256SUMS'].decode().splitlines():
        line = raw_line.strip()
        if not line:
            continue
        _, rel = line.split(None, 1)
        rel = rel.lstrip('*')
        if not rel.startswith('root/'):
            target = f'{qti_prefix}/{rel}'
            require(target in bodies, f'Missing non-root file in sq2-qti-system: {target}')
            non_root_sums.append(f'{sha256_bytes(bodies[target])}  {rel}')
    all_sums = [f'{sha256_bytes(bodies[root_prefix + rel])}  root/{rel}' for rel in install_rel_paths] + sorted(non_root_sums)
    bodies[f'{qti_prefix}/SHA256SUMS'] = ('\n'.join(all_sums) + '\n').encode()

    # Add missing parent dir records and encode member 0 + verbatim trailing credential member.
    for name in list(bodies):
        for parent in Path(name).parents:
            key = parent.as_posix()
            if key != '.' and key not in records:
                records[key] = full_image.new_record(0o040755)
                bodies[key] = b''

    trailer = full_image.new_record(0)
    raw = b''.join(encode(n, bodies[n], records[n]) for n in sorted(bodies, key=lambda n: (n.count('/'), n)))
    raw += encode('TRAILER!!!', b'', trailer)
    raw += b'\0' * (-len(raw) % 512)
    out_bytes = gzip.compress(raw, mtime=0) + credential_member

    output_dir.mkdir(parents=True, exist_ok=True)
    out_initramfs = output_dir / 'initramfs.cpio.gz'
    out_initramfs.write_bytes(out_bytes)

    # Host post-build audit 1: exact cpio entry delta and marker-free full decompressed scan.
    full_decompressed = gzip.decompress(out_bytes)
    require(
        b'com.futuremark.dmandroid.application' not in full_decompressed,
        'Legacy 3DMark sysmem workaround marker leaked into output initramfs',
    )
    after_records, after_bodies = full_image.records_from_gzip(out_initramfs)

    expected_added = {
        'etc/googlebook-security/lib64/hw/vulkan.turnip.so',
        'etc/googlebook-security/etc/sq2-vulkan-policy.conf',
        'etc/sq2-qti-system/root/lib64/hw/vulkan.qti.so',
        'etc/sq2-qti-system/root/lib64/vulkan.qti-native.so',
        'etc/sq2-qti-system/root/lib64/libquest2-native-vulkan-memory.so',
        'etc/sq2-qti-system/root/lib64/libq2v77glnext.so',
        'etc/sq2-qti-system/root/lib64/libq2v77qgl.so',
        'etc/sq2-qti-system/root/lib64/qgl_config.txt',
    }
    allowed_modified = {
        'etc/googlebook-security/lib64/hw/vulkan.freedreno.so',
        'etc/googlebook-security/lib64/libdrm.so',
        'etc/sq2-qti-system/root/lib64/sq2-qti-display/libdrm.so',
        'etc/sq2-qti-system/root/etc/selinux/vendor_file_contexts',
        'etc/sq2-qti-system/INSTALL_FILES',
        'etc/sq2-qti-system/SYMLINKS',
        'etc/sq2-qti-system/INSTALLED_SHA256SUMS',
        'etc/sq2-qti-system/SHA256SUMS',
    }
    added = set(after_bodies) - set(before_bodies)
    removed = set(before_bodies) - set(after_bodies)
    modified = {k for k in set(before_bodies) & set(after_bodies) if after_bodies[k] != before_bodies[k] or after_records[k][1][1] != before_records[k][1][1]}
    require(added == expected_added, f'Unexpected added keys: {sorted(added ^ expected_added)}')
    require(not removed, f'Unexpected removed keys: {sorted(removed)}')
    require(modified <= allowed_modified, f'Unexpected modified keys: {sorted(modified - allowed_modified)}')

    readelf_bin = build_hybrid_vulkan_router.find_android_cc(None).parent / 'llvm-readelf'
    staging_report = simulate_boot_staging_dry_run(after_bodies, after_records, readelf_bin)

    out_sha = sha256_bytes(out_bytes)
    manifest = {
        'mode': 'hybrid-mesa-qti-dual-vulkan',
        'hardware_validated': False,
        'target_device': '192.168.5.110 (unreachable during build; live boot pending)',
        'inputs': {
            'base_initramfs': {'path': str(base_initramfs), 'sha256': base_sha, 'size_bytes': len(base_bytes)},
            'v21_zip': {'path': str(v21_zip), 'member': V21_INITRAMFS_MEMBER, 'initramfs_sha256': V21_INITRAMFS_SHA256},
            'clean_turnip': {'path': str(clean_turnip_path), 'sha256': sha256_bytes(clean_turnip_bytes), 'size_bytes': len(clean_turnip_bytes)},
            'clean_libdrm': {'path': str(clean_libdrm_path), 'sha256': sha256_bytes(clean_libdrm_bytes), 'size_bytes': len(clean_libdrm_bytes)},
            'router_elf': {'sha256': sha256_bytes(router_bytes), 'size_bytes': len(router_bytes)},
            'policy_conf': {'path': str(policy_conf), 'sha256': sha256_bytes(policy_bytes)},
            'qti_native_vulkan_sha256': qti_vulkan_hashes,
        },
        'output': {
            'initramfs': str(out_initramfs),
            'sha256': out_sha,
            'size_bytes': len(out_bytes),
            'added_entries': sorted(added),
            'modified_entries': sorted(modified),
            'removed_entries': [],
            'trailing_ssh_member_preserved': True,
        },
        'host_audits': {
            'legacy_3dmark_marker_absent': True,
            'staging_dry_run': staging_report,
        },
    }
    (output_dir / 'MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')

    rollback = f"""#!/bin/sh
# Non-destructive reference rollback procedure for 192.168.8.124 (not auto-executed).
# Verify the deployed EFI partition mount (/media/esp/EFI/SQ2Hyb/initramfs.cpio.gz) before running manually:
set -eu
echo "Restore baseline SQ2Hyb backlight-patched initramfs ({base_sha}):"
echo "  ssh root@192.168.8.124 'dd of=/media/esp/EFI/SQ2Hyb/initramfs.cpio.gz.rb bs=4194304 && sync && mv /media/esp/EFI/SQ2Hyb/initramfs.cpio.gz.rb /media/esp/EFI/SQ2Hyb/initramfs.cpio.gz && sync && sha256sum /media/esp/EFI/SQ2Hyb/initramfs.cpio.gz' < {base_initramfs}"
echo "  ssh root@192.168.8.124 reboot"
"""
    (output_dir / 'ROLLBACK.sh').write_text(rollback, encoding='utf-8')

    verification = f"""SQ2Hyb Dual-Vulkan Host Candidate Verification
============================================
hardware_validated: false (192.168.5.110 unreachable during host build)

Output initramfs : {out_initramfs}
Output size      : {len(out_bytes)} bytes
Output SHA256    : {out_sha}
Base initramfs   : {base_initramfs} ({base_sha})
Router ELF       : {sha256_bytes(router_bytes)} ({len(router_bytes)} bytes)
Clean Turnip     : {sha256_bytes(clean_turnip_bytes)} ({len(clean_turnip_bytes)} bytes)
Legacy 3DMark    : ABSENT in full decompressed cpio stream
Staging dry-run  : PASS ({staging_report['shell']}, {staging_report['vendor_lib_count']} staged libs/symlinks, 0 unresolved DT_NEEDED)
SSH addon member : PRESERVED verbatim as final gzip member
"""
    (output_dir / 'VERIFICATION.txt').write_text(verification, encoding='utf-8')
    print(f'SQ2HYB_DUAL_VULKAN_PACK_OK {out_initramfs} ({len(out_bytes)} bytes, sha256={out_sha})')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-initramfs', type=Path, default=DEFAULT_BASE_INITRAMFS)
    parser.add_argument('--v21-zip', type=Path, default=DEFAULT_V21_ZIP)
    parser.add_argument('--turnip-dir', type=Path, default=DEFAULT_TURNIP_DIR)
    parser.add_argument('--policy-conf', type=Path, default=DEFAULT_POLICY_CONF)
    parser.add_argument('--output-dir', type=Path, default=DEFAULT_OUTPUT_DIR)
    args = parser.parse_args()
    build_candidate(
        args.base_initramfs,
        args.v21_zip,
        args.turnip_dir,
        args.policy_conf,
        args.output_dir,
    )


if __name__ == '__main__':
    main()
