"""Original-template full image, standalone private inputs, paired source kernel.

Import is a one-time conversion of private runtime dependencies, not a build-time
parent-package dependency. Full builds never deploy or open block devices.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile

from kernel_baseline import ROOT, policy
from disk_image import module, regular, require
from single_user_payload import ANCHOR, HOOK

DEFAULT_PRIVATE = ROOT / 'private/build-inputs'
DEFAULT_A6XX_PATCHES = ROOT / 'patches/drm-618-a6xx-upstream'
DEFAULT_A6XX_MMIO_FIX_PATCHES = ROOT / 'patches/drm-618-a6xx-mmio-fix'

# Full Mesa chain: keep the Mesa/minigbm/drm-HWC vendor overlay, remove the
# closed Qualcomm graphics payloads and their pre-Android stage helpers.
CLOSED_GRAPHICS_DIRS = ('etc/sq2-qti-system', 'etc/sq2-graphics-full', 'etc/sq2-vulkan-closed')
CLOSED_GRAPHICS_HELPERS = ('bin/qti-system-display-stage', 'bin/qualcomm-full-start',
                           'bin/closed-vulkan-start')
HYBRID_CLOSED_DIRS = ('etc/sq2-graphics-full', 'etc/sq2-vulkan-closed')
HYBRID_CLOSED_HELPERS = ('bin/qualcomm-full-start', 'bin/closed-vulkan-start')
HYBRID_CLOSED_QTI_ROOT_FILES = (
    'lib64/egl/eglSubDriverAndroid.so',
    'lib64/egl/libEGL_adreno.so',
    'lib64/egl/libGLESv1_CM_adreno.so',
    'lib64/egl/libGLESv2_adreno.so',
    'lib64/egl/qgl_config.txt',
    'lib64/hw/vulkan.freedreno.so',
    'lib64/libq2v77glnext.so',
    'lib64/libq2v77qgl.so',
    'lib64/libquest2-native-vulkan-memory.so',
    'lib64/qgl_config.txt',
    'lib64/vulkan.qti-native.so',
    'lib64/sq2-qti-display/eglSubDriverAndroid.so',
    'lib64/sq2-qti-display/libEGL_adreno.so',
    'lib64/sq2-qti-display/libGLESv2_adreno.so',
    'lib64/sq2-qti-display/libq2v77glnext.so',
    'lib64/sq2-qti-display/libq2v77qgl.so',
)
HYBRID_CLOSED_QTI_SYMLINKS = {
    'lib64/egl/libEGL_freedreno.so',
    'lib64/egl/libGLESv1_CM_freedreno.so',
    'lib64/egl/libGLESv2_freedreno.so',
    'lib64/egl/libq2v77glnext.so',
    'lib64/egl/libq2v77qgl.so',
}
HYBRID_REQUIRED_PROPS = ('ro.hardware.egl=freedreno', 'ro.hardware.vulkan=freedreno',
                         'vendor.gralloc.disable_ubwc=0', 'ro.surface_flinger.force_hole_punch=true')
HYBRID_REMOVE_PROPS = {'ro.vendor.hwcomposer.mode=client', 'vendor.minigbm.debug=nocompression'}
MESA_REQUIRED_PROPS = ('ro.hardware.egl=freedreno', 'ro.hardware.vulkan=freedreno',
                       'ro.vendor.hwcomposer.mode=client')
MESA_STAGE_LINES = (
    b'qualcomm-full-start > /logs/qualcomm-full.txt 2>&1 || fail qualcomm-full-graphics\n',
    b'qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n',
)
HYBRID_STAGE_LINE = b'qualcomm-full-start > /logs/qualcomm-full.txt 2>&1 || fail qualcomm-full-graphics\n'
HYBRID_KEEP_STAGE_LINE = b'qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n'
MESA_STATUS_HOOK = (
    b'if [ -f /etc/sq2-graphics-mode ]; then\n'
    b'    echo "SQ2_GRAPHICS_MODE: $(cat /etc/sq2-graphics-mode)" | tee -a /logs/status.txt\n'
    b'fi\n'
)


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def hybrid_runtime(
    records,
    bodies,
    gralloc_adapter_bytes=None,
    layout_payload=None,
    backlight_payload=None,
):
    """Combine Mesa Freedreno/Turnip 3D with QTI gralloc/mapper5/HWC/SDM + native OMX.

    Host-side transformation only: retains Mesa GLES/Turnip in etc/googlebook-security,
    strips closed Adreno GLES/Vulkan from etc/sq2-qti-system, installs the
    u_gralloc_cros_api -> gralloc.kona adapter (gralloc.sq2_mesa.so) as
    gralloc.default.so, aligns sq2-qti-display/libdrm.so with Mesa's libdrm,
    redirects missing QTI panelN-backlight sysfs nodes to pwm-backlight, and
    preserves RequiresSwRenderer=0 native ANW surface OMX decoding.
    """
    prop_name = 'etc/googlebook-security/build.prop'
    require(prop_name in bodies, 'Mesa overlay missing: ' + prop_name)
    props_text = bodies[prop_name].decode()
    for item in HYBRID_REQUIRED_PROPS:
        require(item in props_text, 'Hybrid overlay property missing: ' + item)
    for req_mesa in ('etc/googlebook-security/lib64/egl/libEGL_freedreno.so',
                     'etc/googlebook-security/lib64/egl/libGLESv1_CM_freedreno.so',
                     'etc/googlebook-security/lib64/egl/libGLESv2_freedreno.so',
                     'etc/googlebook-security/lib64/libgallium_dri.so',
                     'etc/googlebook-security/lib64/libdrm.so',
                     'etc/googlebook-security/lib64/hw/vulkan.freedreno.so'):
        require(req_mesa in bodies, 'Mesa/Turnip payload missing: ' + req_mesa)

    qti_prefix = 'etc/sq2-qti-system'
    for req_qti in (qti_prefix,
                    qti_prefix + '/INSTALL_FILES',
                    qti_prefix + '/SYMLINKS',
                    qti_prefix + '/SHA256SUMS',
                    qti_prefix + '/INSTALLED_SHA256SUMS',
                    qti_prefix + '/root/bin/hw/vendor.qti.hardware.display.composer-service',
                    qti_prefix + '/root/bin/hw/vendor.qti.hardware.display.allocator-service',
                    qti_prefix + '/root/lib64/hw/mapper.quest2.so',
                    qti_prefix + '/root/lib64/hw/gralloc.kona.so',
                    qti_prefix + '/root/lib64/libgrallocutils.so',
                    qti_prefix + '/root/lib64/libqdMetaData.so',
                    qti_prefix + '/root/lib64/sq2-qti-display/libdrm.so',
                    qti_prefix + '/root/lib64/sq2-qti-display/libsdmcore.so',
                    'bin/qti-system-display-stage'):
        require(req_qti in bodies, 'QTI display/gralloc payload missing: ' + req_qti)

    init = bodies.get('init', b'')
    require(init.count(HYBRID_STAGE_LINE) == 1,
            'Unknown closed-graphics stage in init: ' + HYBRID_STAGE_LINE.decode().strip())
    require(init.count(HYBRID_KEEP_STAGE_LINE) == 1,
            'Missing qti-system-display-stage in init')

    from full_image_video import verify_native_omx
    omx_report = verify_native_omx(bodies)

    if gralloc_adapter_bytes is None:
        from build_mesa_qti_gralloc import build_bytes
        gralloc_adapter_bytes = build_bytes()

    removed = []
    for directory in HYBRID_CLOSED_DIRS:
        require(directory in bodies, 'Closed graphics payload missing: ' + directory)
        for name in sorted(n for n in bodies if n == directory or n.startswith(directory + '/')):
            removed.append(name)
            bodies.pop(name, None)
            records.pop(name, None)
    for name in HYBRID_CLOSED_HELPERS:
        require(name in bodies, 'Closed graphics helper missing: ' + name)
        removed.append(name)
        bodies.pop(name, None)
        records.pop(name, None)

    for rel in HYBRID_CLOSED_QTI_ROOT_FILES:
        full_name = f'{qti_prefix}/root/{rel}'
        if full_name in bodies:
            removed.append(full_name)
            bodies.pop(full_name, None)
            records.pop(full_name, None)

    # Align composer's LD_LIBRARY_PATH libdrm.so with Mesa/Turnip's libdrm 2.4.133.
    bodies[qti_prefix + '/root/lib64/sq2-qti-display/libdrm.so'] = (
        bodies['etc/googlebook-security/lib64/libdrm.so']
    )

    # Install u_gralloc_cros_api -> gralloc.kona adapter as gralloc.sq2_mesa.so.
    adapter_rel = 'lib64/hw/gralloc.sq2_mesa.so'
    adapter_path = f'{qti_prefix}/root/{adapter_rel}'
    bodies[adapter_path] = gralloc_adapter_bytes
    records[adapter_path] = new_record(0o100644)

    # Pad real linear RGB allocations for Freedreno; never inflate imported sizes.
    if layout_payload is None:
        from build_mesa_qti_layout import build_payload
        layout_payload = build_payload(bodies[qti_prefix + '/root/lib64/libgrallocutils.so'])
    if backlight_payload is None:
        from build_sdm_backlight_shim import build_payload as build_backlight_payload
        backlight_payload = build_backlight_payload(
            bodies[qti_prefix + '/root/lib64/sq2-qti-display/libsdmcore.so']
        )

    overlay_specs = (
        (
            'Hybrid layout payload',
            layout_payload,
            {
                'lib64/libgrallocutils.so',
                'lib64/sq2-qti-display/libgrallocutils.so',
                'lib64/libsq2_grallocutils_vendor.so',
            },
        ),
        (
            'Hybrid backlight payload',
            backlight_payload,
            {
                'lib64/sq2-qti-display/libsdmcore.so',
                'lib64/sq2-qti-display/libsq2_sdm_backlight.so',
            },
        ),
    )
    for label, payload_map, expected_keys in overlay_specs:
        require(set(payload_map) == expected_keys, f'Incomplete {label}')
        for rel, payload in payload_map.items():
            path = f'{qti_prefix}/root/{rel}'
            bodies[path] = payload
            records[path] = new_record(0o100644)

    # Rewrite INSTALL_FILES, SYMLINKS, INSTALLED_SHA256SUMS, and SHA256SUMS deterministically.
    root_prefix = qti_prefix + '/root/'
    install_rel_paths = sorted(
        n.removeprefix(root_prefix)
        for n in bodies
        if n.startswith(root_prefix) and not (records[n][1][1] & 0o170000 == 0o040000)
    )
    bodies[qti_prefix + '/INSTALL_FILES'] = ('\n'.join(install_rel_paths) + '\n').encode()

    new_symlinks = []
    saw_default_gralloc = False
    for raw_line in bodies[qti_prefix + '/SYMLINKS'].decode().splitlines():
        line = raw_line.strip()
        if not line:
            continue
        src, dst = line.split(None, 1)
        if src in HYBRID_CLOSED_QTI_SYMLINKS:
            continue
        if src == 'lib64/hw/gralloc.default.so':
            dst = 'gralloc.sq2_mesa.so'
            saw_default_gralloc = True
        new_symlinks.append(f'{src} {dst}')
    require(saw_default_gralloc, 'Expected lib64/hw/gralloc.default.so in sq2-qti-system SYMLINKS')
    bodies[qti_prefix + '/SYMLINKS'] = ('\n'.join(new_symlinks) + '\n').encode()

    installed_sums = [f'{_sha256(bodies[root_prefix + rel])}  {rel}' for rel in install_rel_paths]
    bodies[qti_prefix + '/INSTALLED_SHA256SUMS'] = ('\n'.join(installed_sums) + '\n').encode()

    non_root_sums = []
    for raw_line in bodies[qti_prefix + '/SHA256SUMS'].decode().splitlines():
        line = raw_line.strip()
        if not line:
            continue
        _, rel = line.split(None, 1)
        rel = rel.lstrip('*')
        if not rel.startswith('root/'):
            target = f'{qti_prefix}/{rel}'
            require(target in bodies, 'Missing non-root file in sq2-qti-system: ' + target)
            non_root_sums.append(f'{_sha256(bodies[target])}  {rel}')
    all_sums = [f'{_sha256(bodies[root_prefix + rel])}  root/{rel}' for rel in install_rel_paths] + sorted(non_root_sums)
    bodies[qti_prefix + '/SHA256SUMS'] = ('\n'.join(all_sums) + '\n').encode()

    # Remove minigbm/client-only overrides while retaining freedreno EGL/Vulkan and hole punch.
    filtered_props = [
        line for line in props_text.splitlines(keepends=True)
        if line.strip() not in HYBRID_REMOVE_PROPS
    ]
    bodies[prop_name] = ''.join(filtered_props).encode()

    init = init.replace(HYBRID_STAGE_LINE, b'')
    require(MESA_STATUS_HOOK not in init, 'Hybrid status hook already present')
    anchor = b'if [ -d /etc/googlebook-security ]; then\n'
    require(init.count(anchor) == 1, 'Unknown security-overlay handoff')
    init = init.replace(anchor, MESA_STATUS_HOOK + anchor, 1)
    bodies['init'] = init
    bodies['etc/sq2-graphics-mode'] = b'hybrid-mesa-qti\n'
    records['etc/sq2-graphics-mode'] = new_record(0o100644)

    retained_mesa = sorted(n for n in bodies
                           if n.startswith('etc/googlebook-security/') and not n.endswith('/'))
    return {'graphics': 'hybrid',
            'removed_members': removed,
            'removed_count': len(removed),
            'retained_mesa_members': len(retained_mesa),
            'qti_installed_count': len(install_rel_paths),
            'gralloc_adapter_sha256': _sha256(gralloc_adapter_bytes),
            'marker': 'etc/sq2-graphics-mode',
            'video': omx_report,
            'hardware_validated': False}


def mesa_runtime(records, bodies):
    """Switch the runtime initramfs from the mixed/QTI stack to a full Mesa chain.

    Host-side transformation only: closed Qualcomm graphics payloads and their
    pre-Android stage helpers are removed, the Mesa/minigbm/drm-HWC overlay is
    required to stay intact, and init stops invoking the closed stages.
    It does not build Mesa, prove rendering or touch a device.
    """
    prop_name = 'etc/googlebook-security/build.prop'
    require(prop_name in bodies, 'Mesa overlay missing: ' + prop_name)
    props = bodies[prop_name].decode()
    for item in MESA_REQUIRED_PROPS:
        require(item in props, 'Mesa overlay property missing: ' + item)
    require('etc/googlebook-security/lib64/egl/libEGL_freedreno.so' in bodies, 'Mesa EGL payload missing')
    require('etc/googlebook-security/lib64/hw/vulkan.freedreno.so' in bodies, 'Mesa Vulkan payload missing')
    require('etc/googlebook-security/lib64/hw/mapper.minigbm.so' in bodies, 'minigbm mapper missing')
    init = bodies.get('init', b'')
    for line in MESA_STAGE_LINES:
        require(init.count(line) == 1,
                'Unknown closed-graphics stage in init: ' + line.decode().strip())
    # Keep Venus hardware decode usable without the QTI display/gralloc layer.
    from full_image_video import mesa_omx
    omx_report = mesa_omx(records, bodies, new_record)
    removed = []
    for directory in CLOSED_GRAPHICS_DIRS:
        require(directory in bodies, 'Closed graphics payload missing: ' + directory)
        for name in sorted(n for n in bodies if n == directory or n.startswith(directory + '/')):
            removed.append(name)
            bodies.pop(name, None)
            records.pop(name, None)
    for name in CLOSED_GRAPHICS_HELPERS:
        require(name in bodies, 'Closed graphics helper missing: ' + name)
        removed.append(name)
        bodies.pop(name, None)
        records.pop(name, None)
    for line in MESA_STAGE_LINES:
        init = init.replace(line, b'')
    require(MESA_STATUS_HOOK not in init, 'Mesa status hook already present')
    anchor = b'if [ -d /etc/googlebook-security ]; then\n'
    require(init.count(anchor) == 1, 'Unknown security-overlay handoff')
    init = init.replace(anchor, MESA_STATUS_HOOK + anchor, 1)
    bodies['init'] = init
    bodies['etc/sq2-graphics-mode'] = b'full-mesa\n'
    records['etc/sq2-graphics-mode'] = new_record(0o100644)
    retained = sorted(n for n in bodies
                      if n.startswith('etc/googlebook-security/') and not n.endswith('/'))
    require(retained, 'Mesa overlay lost all members')
    return {'graphics': 'mesa',
            'removed_members': removed,
            'removed_count': len(removed),
            'retained_mesa_members': len(retained),
            'init_closed_stages_removed': len(MESA_STAGE_LINES),
            'marker': 'etc/sq2-graphics-mode',
            'video': omx_report,
            'hardware_validated': False}


def rewrite_mesa_boot_selection(bundle):
    """Select Mesa in the EFI boot command line (equal-length token rewrite)."""
    options_path = bundle / 'kernel-options.txt'
    text = options_path.read_text()
    tokens = [t for t in text.split() if t.startswith('sq2.vulkan=')]
    require(tokens == ['sq2.vulkan=qcom'],
            'Unexpected baseline graphics option: ' + repr(tokens))
    launcher_path = bundle / 'el2launch.efi'
    launcher = launcher_path.read_bytes()
    old = 'sq2.vulkan=qcom'.encode('utf-16le')
    new = 'sq2.vulkan=mesa'.encode('utf-16le')
    require(len(old) == len(new) and launcher.count(old) == 1,
            'Unexpected EFI graphics constant')
    require(launcher.count(text.strip().encode('utf-16le') + b'\0\0') == 1,
            'EFI command line does not match kernel-options.txt')
    options_path.write_text(text.replace('sq2.vulkan=qcom', 'sq2.vulkan=mesa'))
    launcher_path.write_bytes(launcher.replace(old, new))
    updated = options_path.read_text()
    require(launcher_path.read_bytes().count(updated.strip().encode('utf-16le') + b'\0\0') == 1,
            'EFI command line was not updated')
    return 'sq2.vulkan=mesa'


EXTERNAL_UNSYNCED_PREFIXES = (
    'drivers/thermal/qcom/',
    'drivers/clk/qcom/videocc-sm8150',
    'drivers/media/platform/qcom/venus/',
    'drivers/hid/spi-hid/',
    'drivers/platform/surface/aggregator/',
)


def split_git_diffs(text):
    """Split a git patch into (header, [(path, diff_block), ...])."""
    starts = [(m.start(), m.group(1)) for m in re.finditer(r'^diff --git a/(\S+) b/\S+', text, re.M)]
    if not starts:
        return text, []
    header = text[:starts[0][0]]
    blocks = []
    for index, (pos, path) in enumerate(starts):
        end = starts[index + 1][0] if index + 1 < len(starts) else len(text)
        blocks.append((path, text[pos:end]))
    return header, blocks


def apply_kernel_patches(patch_dir, kernel_source, output, run, external_source=None,
                         extra_patch_dirs=()):
    """Apply every *.patch in order to pinned kernel source and synced external overrides."""
    patch_dir = Path(patch_dir)
    extra_dirs = [Path(d) for d in extra_patch_dirs]
    kernel_source = Path(kernel_source)
    output = Path(output)
    files = []
    for directory in (patch_dir, *extra_dirs):
        require(directory.is_dir(), 'Kernel patch directory is missing: ' + str(directory))
        dir_files = sorted(p for p in directory.iterdir() if p.suffix == '.patch')
        require(dir_files, 'No .patch files in ' + str(directory))
        files.extend(dir_files)
    edp_tree = Path(external_source) / 'edp-618' if external_source is not None else None
    if edp_tree is not None:
        require((edp_tree / 'drivers/gpu/drm/msm').is_dir(),
                'External MSM override directory is missing: ' + str(edp_tree))
    applied = []
    for patch in files:
        text = patch.read_text()
        header, blocks = split_git_diffs(text)
        touched = list(dict.fromkeys(name for name, _ in blocks))
        require(touched, 'Patch has no diff header: ' + patch.name)
        if edp_tree is not None:
            unsynced = [n for n in touched if n.startswith(EXTERNAL_UNSYNCED_PREFIXES)]
            require(not unsynced,
                    'Patch touches unsynced external module override(s): ' + ', '.join(unsynced))
        msm_blocks = [(name, block) for name, block in blocks
                      if name.startswith('drivers/gpu/drm/msm/')]
        msm_touched = list(dict.fromkeys(name for name, _ in msm_blocks))
        pre_matched_msm = set()
        if edp_tree is not None:
            for name in msm_touched:
                k_file, e_file = kernel_source / name, edp_tree / name
                if k_file.is_file() and e_file.is_file() and k_file.read_bytes() == e_file.read_bytes():
                    pre_matched_msm.add(name)
        run(['patch', '-p1', '--batch', '--forward', '--fuzz=0', '-i', str(patch.resolve())],
            cwd=kernel_source)
        hashes = {}
        for name in touched:
            path = kernel_source / name
            require(path.is_file(), 'Patched file missing: ' + name)
            hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        entry = {'path': str(patch),
                 'sha256': hashlib.sha256(patch.read_bytes()).hexdigest(),
                 'files': hashes}
        if edp_tree is not None and msm_blocks:
            if len(msm_blocks) == len(blocks):
                ext_patch = patch.resolve()
            else:
                ext_patch = (output / ('edp-618-' + patch.name)).resolve()
                ext_patch.write_text(header + ''.join(block for _, block in msm_blocks))
            run(['patch', '-p1', '--batch', '--forward', '--fuzz=0', '-i', str(ext_patch)],
                cwd=edp_tree)
            ext_hashes = {}
            for name in msm_touched:
                ext_path = edp_tree / name
                require(ext_path.is_file(), 'Patched external MSM file missing: ' + name)
                ext_hashes[name] = hashlib.sha256(ext_path.read_bytes()).hexdigest()
                if name in pre_matched_msm:
                    require(ext_hashes[name] == hashes[name],
                            'External MSM patch result diverged from kernel: ' + name)
            entry['external_files'] = ext_hashes
        applied.append(entry)
    report = {'directory': str(patch_dir), 'patch_count': len(applied), 'patches': applied,
              'external_msm_synced': edp_tree is not None,
              'note': 'Applied to the extracted pinned kernel source (and external/edp-618 '
                      'when present) before compilation; '
                      'not an upstream rebase and not hardware validated.',
              'hardware_validated': False}
    if extra_dirs:
        report['extra_directories'] = [str(d) for d in extra_dirs]
    (output / 'KERNEL-PATCHES.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def records_from_gzip(path):
    """Read effective newc records, preserving ownership/device metadata."""
    records, bodies = {}, {}
    with gzip.open(path, 'rb') as f:
        while True:
            first = f.read(1)
            if not first:
                break
            if first == b'\0':
                continue
            h = first + f.read(109)
            require(h[:6] in (b'070701', b'070702') and len(h) == 110, 'Invalid newc input')
            fields = [int(h[6+i*8:14+i*8], 16) for i in range(13)]
            size, length = fields[6], fields[11]
            require(0 < length < 65536, 'Invalid newc name')
            name = f.read(length)[:-1].decode().removeprefix('./')
            require(not name.startswith('/') and '..' not in Path(name).parts, 'Unsafe newc path')
            f.read(-(110+length) % 4)
            body = f.read(size)
            require(len(body) == size, 'Truncated newc input')
            f.read(-size % 4)
            if name == 'TRAILER!!!':
                continue
            require(fields[4] <= 1 or fields[1] & 0o170000 != 0o100000, 'Unresolved newc hardlink')
            records[name] = [h[:6].decode(), fields, '']
            bodies[name] = body
    return records, bodies


def import_private(resources, private):
    """Never replace existing dependencies. Secrets stay below ignored private/."""
    resources, private = resources.resolve(), private.resolve()
    source = resources / 'private/build-inputs/kernel-6.18'
    dest = private / 'kernel-6.18'
    runtime = private / 'full-image-runtime'
    require(not runtime.exists(), 'Private runtime already imported; select an explicit new input root to update')
    # Existing historical source archive is retained; new release subdirectories
    # must be fresh so an import cannot silently overwrite an accepted bundle.
    for name in ('EFI/SQ2Nat', 'firmware', 'bootloader'):
        require(not (dest/name).exists(), 'Refusing to overwrite private dependency: '+name)
    for name in ('EFI/SQ2Nat', 'firmware', 'bootloader'):
        shutil.copytree(source/name, dest/name)
    shutil.copyfile(resources/'RESOURCE-MANIFEST.json', private/'RESOURCE-MANIFEST.json')
    records, bodies = records_from_gzip(source/'EFI/SQ2Nat/initramfs.cpio.gz')
    runtime.mkdir(parents=True)
    for name, body in bodies.items():
        # Store even symlink bodies as ordinary blobs; metadata controls the
        # eventual archive. No host ownership, device nodes or secret links.
        target = runtime/'files'/name
        if records[name][1][1] & 0o170000 == 0o040000:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(body)
    (runtime/'records.json').write_text(json.dumps(records, indent=2)+'\n')
    # Preserve accepted board adaptations as explicit DTS input, not an opaque
    # parent DTB selected by a source-build mode.
    from board_dts import from_dtb
    (dest/'board.dts').write_text(from_dtb((source/'EFI/SQ2Nat/android-el2.dtb').read_bytes()))
    report = {'operation':'import-standalone-private-dependencies',
              'private_root':str(private),'runtime_entries':len(records),
              'source_baseline':policy()['baseline'],'source_commit':
              subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
              'credentials_present':True,'parent_needed_for_future_source_build':False}
    (runtime/'INPUTS.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


def new_record(mode):
    return ['070701',[0,mode,0,0,1,0,0,0,0,0,0,0,0],'']


def install_default_wallpaper(private, records, bodies):
    # Keep the original drawable names and Android wallpaper service. Only the
    # existing read-only resource APK is replaced; never touch /data wallpaper.
    wallpaper = private/'wallpaper/DefaultWallpaperOverlayMica.apk'
    if wallpaper.is_file():
        name = 'etc/DefaultWallpaperOverlayMica.apk'
        bodies[name] = wallpaper.read_bytes()
        records[name] = new_record(0o100644)
        hook = b'mount --bind /etc/DefaultWallpaperOverlayMica.apk /newroot/system_ext/overlay/DefaultWallpaperOverlayMica.apk || fail default-wallpaper\n'
        if hook not in bodies['init']:
            require(bodies['init'].count(ANCHOR) == 1, 'Unknown wallpaper handoff')
            bodies['init'] = bodies['init'].replace(ANCHOR, hook + ANCHOR)


def install_userdata_growth(records, bodies):
    import tempfile
    with tempfile.TemporaryDirectory(prefix='spx-grow-build-') as tmp:
        binary=Path(tmp)/'resize-userdata-partition'
        subprocess.run(['aarch64-linux-gnu-gcc','-static','-Os','-Wall','-Wextra',
                        str(ROOT/'probe/storage/resize-userdata-partition.c'),'-o',str(binary)],check=True)
        bodies['bin/resize-userdata-partition']=binary.read_bytes()
    for name in ('grow-image-userdata','surface-swap-boot'):
        bodies['bin/'+name]=(ROOT/'probe/test2'/name).read_bytes().replace(b'\r\n',b'\n')
    for name in ('resize-userdata-partition','grow-image-userdata','surface-swap-boot'):
        records['bin/'+name]=new_record(0o100755)

def assemble_runtime(private, output, compiled=None, graphics='baseline'):
    runtime = private/'full-image-runtime'
    records = json.loads((runtime/'records.json').read_text())
    bodies = {name: b'' if rec[1][1] & 0o170000 == 0o040000 else (runtime/'files'/name).read_bytes()
              for name,rec in records.items()}
    if compiled:
        release = policy()['kernel_release']
        stage = compiled/'modules-stage'
        modules = stage/'lib/modules'/release
        by_name = {}
        for path in modules.rglob('*.ko'):
            require(path.name not in by_name, 'Ambiguous freshly built module: '+path.name)
            by_name[path.name] = path
        # Replace ALL effective module copies, including private overlay copies,
        # never silently retain parent modules with the same version string.
        from full_image_video import validate_venus_core_module, validate_venus_dec_module
        require('venus-core.ko' in by_name, 'Required Venus core module not built')
        validate_venus_core_module(by_name['venus-core.ko'].read_bytes())
        require('venus-dec.ko' in by_name, 'Required Venus decoder module not built')
        validate_venus_dec_module(by_name['venus-dec.ko'].read_bytes())
        for name in list(bodies):
            if name.endswith('.ko'):
                require(Path(name).name in by_name, 'Required runtime module not built: '+name)
                bodies[name] = by_name[Path(name).name].read_bytes()
        for path in modules.rglob('*'):
            if not path.is_file() or path.is_symlink():
                continue
            name = path.relative_to(stage).as_posix()
            bodies[name] = path.read_bytes()
            records[name] = new_record(0o100644)
    graphics_report = None
    if graphics == 'mesa':
        graphics_report = mesa_runtime(records, bodies)
    elif graphics == 'hybrid':
        graphics_report = hybrid_runtime(records, bodies)
    elif graphics != 'baseline':
        raise ValueError('Unknown graphics mode: ' + repr(graphics))
    init = bodies['init']
    require(init.count(ANCHOR) == 1, 'Unknown Android handoff')
    init = re.sub(rb'(?m)^(\s*el2-terminal-autostart)[ \t]+[0-9]+(?=[ \t]*(?:>|&|\n|$))',rb'\1',init)
    if HOOK not in init:
        init = init.replace(ANCHOR,HOOK+ANCHOR)
    bodies['init'] = init
    bodies['bin/apply-single-user'] = (ROOT/'probe/test2/apply-single-user').read_bytes().replace(b'\r\n',b'\n')
    records['bin/apply-single-user'] = new_record(0o100755)
    # Take the current committed helper, while the accepted cumulative IPTSd
    # binary remains the fixed private runtime input (not the old touch cache).
    bodies['bin/touch-calibration-boot'] = (ROOT/'probe/test2/touch-calibration-boot').read_bytes().replace(b'\r\n',b'\n')
    records.setdefault('bin/touch-calibration-boot', new_record(0o100755))
    # Universal builds do not rewrite reference hashes or gate boot on content
    # hashes. Preserve imported manifests as metadata; no startup consumer uses
    # them for validation after this policy is applied.
    from runtime_boot_policy import apply
    apply(bodies)
    from full_image_home import apply as apply_home
    apply_home(records, bodies, new_record)
    install_default_wallpaper(private, records, bodies)
    install_userdata_growth(records, bodies)
    # Add parent directory records for every newly installed module/index.
    for name in list(bodies):
        for parent in Path(name).parents:
            key=parent.as_posix()
            if key!='.' and key not in records:
                records[key]=new_record(0o040755);bodies[key]=b''
    encode=module('full_image_encode','pack-sq2-release.py').encode
    trailer=new_record(0)
    identity={'bin/dropbear','bin/start-ssh','etc/shadow'}
    require(identity <= bodies.keys(),'Incomplete private SSH identity')
    def archive(names):
        raw=b''.join(encode(n,bodies[n],records[n]) for n in names)
        raw+=encode('TRAILER!!!',b'',trailer)
        return gzip.compress(raw+b'\0'*(-len(raw)%512),mtime=0)
    # Directories before children; separate final credential member is retained.
    output.write_bytes(archive(sorted(bodies,key=lambda n:(n.count('/'),n)))+archive(sorted(identity)))
    return graphics_report


def build(args, compile_kernel):
    # A prebuilt Image cannot carry new patches; never silently ignore them.
    if args.kernel_mode == 'prebuilt' and args.kernel_patches:
        raise ValueError('--kernel-patches requires --kernel-mode source')
    # Make every derived path absolute: make interprets CROSS_COMPILE and
    # O= relative to the kernel source directory, so a relative output path
    # would silently break the toolchain prefix.
    args.output = args.output.resolve()
    if args.build_dir is not None:
        args.build_dir = args.build_dir.resolve()
    private=args.private_root.resolve()
    source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    template=regular(args.template or private/'template.img')
    require(args.jobs>0,'--jobs must be positive')
    require(not args.output.exists(),'Full build output must be fresh')
    args.output.mkdir(parents=True)
    records=[]
    def run(command, cwd=None):
        label='step-'+str(len(records))
        with (args.output/(label+'.stdout')).open('w') as out,(args.output/(label+'.stderr')).open('w') as err:
            result=subprocess.run(list(map(str,command)),stdout=out,stderr=err,cwd=cwd)
        records.append({'command':list(map(str,command)),'exit':result.returncode,
                        'stdout':label+'.stdout','stderr':label+'.stderr'})
        (args.output/'BUILD-COMMANDS.json').write_text(json.dumps(records,indent=2)+'\n')
        require(result.returncode==0,'Build failed: '+str(args.output/(label+'.stderr')))
    compiled=None
    if args.kernel_mode=='source':
        source=private/'kernel-6.18/source'
        recipe=json.loads((source/'SOURCE.json').read_text())
        require(recipe.get('current_release_patches') is True,'Source inputs are not prepared for current release')
        compiled=args.output/'kernel-export'
        build_dir=args.build_dir or args.output/'kernel-build'
        extracted=build_dir.parent/(build_dir.name+'-sources')
        require(not extracted.exists(),'Source extraction directory must be fresh: '+str(extracted))
        extracted.mkdir(parents=True)
        with tarfile.open(source/recipe['archive']) as archive:
            archive.extractall(extracted,filter='data')
        # The preserved archive predates the accepted QTI HWC kernel contract.
        # A source build must carry those adaptations, not replace the runtime
        # with an older MSM merely because its release string matches.
        from full_image_display import prepare as prepare_display
        display = prepare_display(extracted/'external')
        (args.output/'DISPLAY-SOURCE.json').write_text(json.dumps(display,indent=2)+'\n')
        # The preserved source-only archive omitted Git's include-prefix
        # symlinks. Recreate the standard local DTC include namespace from
        # existing source directories, never from an old host absolute path.
        prefixes=extracted/'kernel/scripts/dtc/include-prefixes'
        prefixes.mkdir(exist_ok=True)
        include_targets={'dt-bindings':extracted/'kernel/include/dt-bindings'}
        include_targets.update({p.name:p/'boot/dts' for p in (extracted/'kernel/arch').iterdir()
                                if (p/'boot/dts').is_dir()})
        for name,target in include_targets.items():
            link=prefixes/name
            if not link.exists() and not link.is_symlink():
                link.symlink_to(os.path.relpath(target,prefixes),target_is_directory=True)
        kernel_patches = args.kernel_patches
        if kernel_patches is None and args.graphics in ('mesa', 'hybrid'):
            kernel_patches = DEFAULT_A6XX_PATCHES
        patch_report = None
        if kernel_patches:
            extra_patches = ()
            if Path(kernel_patches).resolve() == DEFAULT_A6XX_PATCHES.resolve():
                extra_patches = (DEFAULT_A6XX_MMIO_FIX_PATCHES,)
            patch_report = apply_kernel_patches(kernel_patches, extracted/'kernel', args.output, run,
                                                external_source=extracted/'external',
                                                extra_patch_dirs=extra_patches)
        cross=args.cross_compile
        if cross is None:
            toolchain=build_dir.parent/(build_dir.name+'-toolchain')
            require(not toolchain.exists(),'Toolchain extraction directory must be fresh')
            toolchain.mkdir(parents=True)
            with tarfile.open(private/'kernel-6.18/toolchain.tar.gz') as archive:
                archive.extractall(toolchain,filter='data')
            cross=str(toolchain/'usr/bin/aarch64-linux-gnu-')
        run([sys.executable,ROOT/'scripts/build.py','compile','--source',extracted/'kernel',
             '--external-sources',extracted/'external','--config',ROOT/'configs/kernel-6.18-baseline.config',
             '--build-dir',build_dir,'--output',compiled,'--cross-compile',cross,
             '--jobs',str(args.jobs),'--build-only'])
    bundle=args.output/'paired/EFI/SQ2Nat'
    bundle.mkdir(parents=True)
    original=private/'kernel-6.18/EFI/SQ2Nat'
    for name in ('start.efi','el2launch.efi','qebspilaa64.efi','tcblaunch.exe','kernel-options.txt'):
        shutil.copyfile(original/name,bundle/name)
    graphics_token = [t for t in (bundle/'kernel-options.txt').read_text().split()
                      if t.startswith('sq2.vulkan=')]
    require(graphics_token, 'Missing sq2.vulkan= boot option')
    graphics_token = graphics_token[0]
    if args.graphics in ('mesa', 'hybrid'):
        graphics_token = rewrite_mesa_boot_selection(bundle)
    if compiled:
        shutil.copyfile(compiled/'Image',bundle/'Image')
        run(['dtc','-q','-I','dts','-O','dtb','-o',bundle/'android-el2.dtb',private/'kernel-6.18/board.dts'])
    else:
        for name in ('Image','android-el2.dtb'):
            shutil.copyfile(original/name,bundle/name)
    graphics_report = assemble_runtime(private,bundle/'initramfs.cpio.gz',compiled,graphics=args.graphics)
    esp_only = bool(getattr(args, 'esp_only', False))
    from disk_image import build as disk_build
    disk_args=argparse.Namespace(output=args.output/'disk',template=template,recovery_zip=None,
        resources=private,private_root=private,prepared_bundle=bundle,
        single_user_initramfs=bundle/'initramfs.cpio.gz',
        expected_single_user_sha256=None,
        size_gib=None,jobs=args.jobs,build_only=True,resume_build=False,kernel_mode=args.kernel_mode,
        graphics_argument=graphics_token,esp_only=esp_only)
    report=disk_build(disk_args)
    report.update(source_compiled=bool(compiled),private_root=str(private),
        source_commit=source_commit,
        build_time_parent_initramfs_used=False,postbuild_tests_performed=False,
        graphics=args.graphics,graphics_boot_option=graphics_token,
        graphics_report=graphics_report,
        esp_only=esp_only,
        kernel_patches=patch_report['patch_count'] if patch_report else 0,
        kernel_patches_external_msm_synced=(patch_report['external_msm_synced'] if patch_report else None),
        kernel_patches_note=('prebuilt accepted Image: no patches applied' if args.kernel_mode=='prebuilt'
                             else None),
        hardware_validated=False)
    (args.output/'FULL-IMAGE.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
