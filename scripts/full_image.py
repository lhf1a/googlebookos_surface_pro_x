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
import tempfile

from kernel_baseline import ROOT, policy
from disk_image import module, regular, require
from single_user_payload import ANCHOR, HOOK

DEFAULT_PRIVATE = ROOT / 'private/build-inputs'
DEFAULT_KERNEL_CONFIG = ROOT / 'configs/kernel-6.18-hybrid.config'
DEFAULT_A6XX_PATCHES = ROOT / 'patches/drm-618-a6xx-upstream'
DEFAULT_A6XX_MMIO_FIX_PATCHES = ROOT / 'patches/drm-618-a6xx-mmio-fix'
DEFAULT_DPU_CRTC_KICKOFF_PATCHES = ROOT / 'patches/drm-618-dpu-crtc-kickoff'
DEFAULT_MSM_ASYNC_CLOSE_PATCHES = ROOT / 'patches/drm-618-msm-async-close'
# In-tree kernel/edp-618 s2idle patches applied unconditionally in source mode.
# Out-of-tree Type-C module patches (spx-qmp-lifecycle.patch and
# spx-dwc3-ffs-ep0-dequeue.patch) are applied by scripts/build-spx-typec.py onto
# isolated scratch copies of extracted/kernel files and must not pre-patch them.
SPX_S2IDLE_PATCHES = (
    ROOT / 'patches/spx-dp-s2idle.patch',
    ROOT / 'patches/spx-dwc3-s2idle.patch',
)

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
}
HYBRID_REQUIRED_QTI_VULKAN_FILES = (
    'lib64/vulkan.qti-native.so',
    'lib64/libquest2-native-vulkan-memory.so',
    'lib64/libq2v77glnext.so',
    'lib64/libq2v77qgl.so',
    'lib64/qgl_config.txt',
    'lib64/libgsl.so',
    'lib64/libadreno_utils.so',
    'lib64/libquest2-system-ion.so',
)
HYBRID_REQUIRED_QTI_VULKAN_SYMLINKS = {
    'lib64/egl/libq2v77glnext.so',
    'lib64/egl/libq2v77qgl.so',
}
HYBRID_SELINUX_CONTEXT_LINES = (
    r'/vendor/lib64/hw/vulkan\.freedreno\.so u:object_r:same_process_hal_file:s0',
    r'/vendor/lib64/hw/vulkan\.turnip\.so u:object_r:same_process_hal_file:s0',
    r'/vendor/lib64/hw/vulkan\.qti\.so u:object_r:same_process_hal_file:s0',
    r'/vendor/lib64/hw/gralloc\.sq2_mesa\.so u:object_r:same_process_hal_file:s0',
    r'/vendor/lib64/libsq2_grallocutils_vendor\.so u:object_r:same_process_hal_file:s0',
    r'/vendor/etc/sq2-vulkan-policy\.conf u:object_r:vendor_configs_file:s0',
)
LEGACY_TURNIP_SYSMEM_MARKERS = (
    b'com.futuremark.dmandroid.application',
    b'SQ2 A680.1: forcing sysmem rendering',
)
DEFAULT_CLEAN_TURNIP_PATH = ROOT / 'out/sq2-turnip/runtime/vulkan.freedreno.so'
DEFAULT_CLEAN_TURNIP_LIBDRM_PATH = ROOT / 'out/sq2-turnip/runtime/libdrm.so'
DEFAULT_MESA_PERF_DIR = ROOT / 'out/sq2-mesa-perf/runtime'
DEFAULT_SDM_PERF_PATH = ROOT / 'out/sq2-sdm-perf/libsdmcore.so'
DEFAULT_VULKAN_POLICY_PATH = ROOT / 'configs/sq2-vulkan-policy.conf'
HYBRID_REQUIRED_PROPS = ('ro.hardware.egl=freedreno', 'ro.hardware.vulkan=freedreno',
                         'vendor.gralloc.disable_ubwc=0', 'ro.surface_flinger.force_hole_punch=true')
HYBRID_REMOVE_PROPS = {'ro.vendor.hwcomposer.mode=client', 'vendor.minigbm.debug=nocompression'}
HYBRID_REPLACE_PROPS = {
    'debug.sf.disable_client_composition_cache=1': 'debug.sf.disable_client_composition_cache=0',
}
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


def append_selinux_context_lines(contexts_bytes, lines=HYBRID_SELINUX_CONTEXT_LINES):
    """Append missing SELinux file_contexts lines idempotently."""
    text = contexts_bytes.decode('utf-8')
    existing = {line.strip() for line in text.splitlines() if line.strip()}
    out = list(text.splitlines())
    for line in lines:
        if line not in existing:
            out.append(line)
            existing.add(line)
    return ('\n'.join(out) + '\n').encode('utf-8')


def resolve_clean_turnip_bytes(bodies, turnip_bytes=None):
    """Return unpatched hardware-GMEM Turnip bytes and reject 3DMark sysmem markers."""
    vk_key = 'etc/googlebook-security/lib64/hw/vulkan.freedreno.so'
    require(vk_key in bodies, 'Mesa Vulkan payload missing')
    if turnip_bytes is None:
        existing = bodies[vk_key]
        if any(m in existing for m in LEGACY_TURNIP_SYSMEM_MARKERS) and DEFAULT_CLEAN_TURNIP_PATH.is_file():
            turnip_bytes = DEFAULT_CLEAN_TURNIP_PATH.read_bytes()
            libdrm_key = 'etc/googlebook-security/lib64/libdrm.so'
            if libdrm_key in bodies and DEFAULT_CLEAN_TURNIP_LIBDRM_PATH.is_file():
                bodies[libdrm_key] = DEFAULT_CLEAN_TURNIP_LIBDRM_PATH.read_bytes()
        else:
            turnip_bytes = existing
    for marker in LEGACY_TURNIP_SYSMEM_MARKERS:
        require(marker not in turnip_bytes, 'Legacy 3DMark sysmem workaround marker found in Turnip binary')
    return turnip_bytes


def hybrid_runtime(
    records,
    bodies,
    gralloc_adapter_bytes=None,
    layout_payload=None,
    backlight_payload=None,
    vulkan_router_bytes=None,
    turnip_bytes=None,
    vulkan_policy_bytes=None,
    qti_submit_payload=None,
    mesa_runtime_dir=None,
    sdmcore_bytes=None,
):
    """Combine Mesa Freedreno/Dual-Vulkan (Turnip + QTI) with QTI gralloc/mapper5/HWC/SDM + OMX.

    Host-side transformation only: retains Mesa GLES in etc/googlebook-security,
    installs unpatched hardware-GMEM Turnip at vulkan.turnip.so, installs the
    per-app Dual-Vulkan router at vulkan.freedreno.so + sq2-vulkan-policy.conf,
    retains QTI native Vulkan closure while relocating QTI's HAL wrapper to
    vulkan.qti.so, strips closed Adreno GLES, installs gralloc.sq2_mesa.so as
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
    for req_vk_rel in HYBRID_REQUIRED_QTI_VULKAN_FILES:
        req_vk_path = f'{qti_prefix}/root/{req_vk_rel}'
        require(req_vk_path in bodies, 'QTI native Vulkan dependency missing: ' + req_vk_path)

    qti_vk_hal_src = f'{qti_prefix}/root/lib64/hw/vulkan.freedreno.so'
    qti_vk_hal_dst = f'{qti_prefix}/root/lib64/hw/vulkan.qti.so'
    require(
        qti_vk_hal_src in bodies or qti_vk_hal_dst in bodies,
        'QTI Vulkan policy HAL missing: ' + qti_vk_hal_src,
    )

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

    mesa_perf_map = (
        ('libEGL_freedreno.so', 'etc/googlebook-security/lib64/egl/libEGL_freedreno.so'),
        ('libGLESv1_CM_freedreno.so', 'etc/googlebook-security/lib64/egl/libGLESv1_CM_freedreno.so'),
        ('libGLESv2_freedreno.so', 'etc/googlebook-security/lib64/egl/libGLESv2_freedreno.so'),
        ('libgallium_dri.so', 'etc/googlebook-security/lib64/libgallium_dri.so'),
        ('libdrm.so', 'etc/googlebook-security/lib64/libdrm.so'),
    )
    mesa_dir = Path(mesa_runtime_dir) if mesa_runtime_dir is not None else DEFAULT_MESA_PERF_DIR
    use_mesa_perf = mesa_runtime_dir is not None or (
        bodies['etc/googlebook-security/lib64/libgallium_dri.so'].startswith(b'\x7fELF')
        and all((mesa_dir / src_name).is_file() for src_name, _ in mesa_perf_map)
        and (mesa_dir / 'vulkan.turnip.so').is_file()
    )
    if use_mesa_perf:
        for src_name, dst_key in mesa_perf_map:
            src_path = mesa_dir / src_name
            require(src_path.is_file(), f'Mesa perf runtime missing: {src_path}')
            bodies[dst_key] = src_path.read_bytes()
        if turnip_bytes is None:
            turnip_path = mesa_dir / 'vulkan.turnip.so'
            require(turnip_path.is_file(), f'Mesa perf Turnip missing: {turnip_path}')
            turnip_bytes = turnip_path.read_bytes()

    clean_turnip = resolve_clean_turnip_bytes(bodies, turnip_bytes=turnip_bytes)
    if vulkan_router_bytes is None:
        from build_hybrid_vulkan_router import build_bytes as build_router_bytes
        vulkan_router_bytes = build_router_bytes()
    if vulkan_policy_bytes is None:
        vulkan_policy_bytes = DEFAULT_VULKAN_POLICY_PATH.read_bytes().replace(b'\r\n', b'\n')

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

    # Relocate QTI's Vulkan policy HAL out of vulkan.freedreno.so so it does not overwrite our router.
    if qti_vk_hal_src in bodies:
        qti_vk_bytes = bodies.pop(qti_vk_hal_src)
        records.pop(qti_vk_hal_src, None)
        bodies[qti_vk_hal_dst] = qti_vk_bytes
        records[qti_vk_hal_dst] = new_record(0o100644)
    require(qti_vk_hal_src not in bodies, 'QTI Vulkan HAL must not remain at vulkan.freedreno.so')

    # Install Dual-Vulkan files in etc/googlebook-security (router + Turnip + policy conf).
    records.setdefault('etc/googlebook-security/etc', new_record(0o040755))
    bodies.setdefault('etc/googlebook-security/etc', b'')
    bodies['etc/googlebook-security/lib64/hw/vulkan.turnip.so'] = clean_turnip
    records['etc/googlebook-security/lib64/hw/vulkan.turnip.so'] = new_record(0o100644)
    bodies['etc/googlebook-security/lib64/hw/vulkan.freedreno.so'] = vulkan_router_bytes
    records['etc/googlebook-security/lib64/hw/vulkan.freedreno.so'] = new_record(0o100644)
    bodies['etc/googlebook-security/etc/sq2-vulkan-policy.conf'] = vulkan_policy_bytes
    records['etc/googlebook-security/etc/sq2-vulkan-policy.conf'] = new_record(0o100644)

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
    sdmcore_key = qti_prefix + '/root/lib64/sq2-qti-display/libsdmcore.so'
    use_sdm_perf = False
    if sdmcore_bytes is not None:
        bodies[sdmcore_key] = sdmcore_bytes
        use_sdm_perf = True
    elif (
        backlight_payload is None
        and bodies[sdmcore_key].startswith(b'\x7fELF')
        and DEFAULT_SDM_PERF_PATH.is_file()
    ):
        bodies[sdmcore_key] = DEFAULT_SDM_PERF_PATH.read_bytes()
        use_sdm_perf = True
    if backlight_payload is None:
        from build_sdm_backlight_shim import build_payload as build_backlight_payload
        backlight_payload = build_backlight_payload(bodies[sdmcore_key])

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

    for rel, data in (qti_submit_payload or {}).items():
        path = f'{qti_prefix}/root/{rel}'
        bodies[path] = data
        records.setdefault(path, new_record(0o100644))

    vfc_path = f'{qti_prefix}/root/etc/selinux/vendor_file_contexts'
    if vfc_path in bodies:
        bodies[vfc_path] = append_selinux_context_lines(bodies[vfc_path])

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
    seen_vk_symlinks = set()
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
        if src in HYBRID_REQUIRED_QTI_VULKAN_SYMLINKS:
            seen_vk_symlinks.add(src)
        new_symlinks.append(f'{src} {dst}')
    require(saw_default_gralloc, 'Expected lib64/hw/gralloc.default.so in sq2-qti-system SYMLINKS')
    require(
        seen_vk_symlinks == HYBRID_REQUIRED_QTI_VULKAN_SYMLINKS,
        'Missing required QTI Vulkan symlinks in sq2-qti-system SYMLINKS',
    )
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

    # Remove minigbm/client-only overrides and enable SF client composition caching.
    filtered_props = []
    for line in props_text.splitlines(keepends=True):
        stripped = line.strip()
        if stripped in HYBRID_REMOVE_PROPS:
            continue
        if stripped in HYBRID_REPLACE_PROPS:
            eol = '\n' if line.endswith('\n') else ''
            filtered_props.append(HYBRID_REPLACE_PROPS[stripped] + eol)
            continue
        filtered_props.append(line)
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
            'vulkan_mode': 'dual-turnip-qti',
            'removed_members': removed,
            'removed_count': len(removed),
            'retained_mesa_members': len(retained_mesa),
            'qti_installed_count': len(install_rel_paths),
            'gralloc_adapter_sha256': _sha256(gralloc_adapter_bytes),
            'vulkan_router_sha256': _sha256(vulkan_router_bytes),
            'turnip_sha256': _sha256(clean_turnip),
            'mesa_perf_applied': use_mesa_perf,
            'sdm_perf_applied': use_sdm_perf,
            'marker': 'etc/sq2-graphics-mode',
            'video': omx_report,
            'hardware_validated': False}


def mesa_runtime(records, bodies, turnip_bytes=None):
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
    bodies['etc/googlebook-security/lib64/hw/vulkan.freedreno.so'] = resolve_clean_turnip_bytes(
        bodies, turnip_bytes=turnip_bytes
    )
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


def _apply_patch_idempotent(patch_path, target_dir, run):
    """Apply patch_path to target_dir with --fuzz=0, skipping only if cleanly applied."""
    patch_path = Path(patch_path).resolve()
    target_dir = Path(target_dir)
    forward = subprocess.run(
        ['patch', '-p1', '--dry-run', '--batch', '--forward', '--fuzz=0', '-i', str(patch_path)],
        cwd=target_dir,
        capture_output=True,
        text=True,
    )
    if forward.returncode == 0:
        run(['patch', '-p1', '--batch', '--forward', '--fuzz=0', '-i', str(patch_path)],
            cwd=target_dir)
        return True
    reverse = subprocess.run(
        ['patch', '-p1', '--dry-run', '--force', '--reverse', '--fuzz=0', '-i', str(patch_path)],
        cwd=target_dir,
        capture_output=True,
        text=True,
    )
    if reverse.returncode == 0:
        return False
    run(['patch', '-p1', '--dry-run', '--batch', '--forward', '--fuzz=0', '-i', str(patch_path)],
        cwd=target_dir)
    raise ValueError('Patch failed to apply cleanly with --fuzz=0: ' + str(patch_path))


def apply_spx_s2idle_patches(kernel_source, run, external_source=None,
                             patches=SPX_S2IDLE_PATCHES):
    """Apply Surface Pro X DP/DWC3 s2idle patches to kernel and external/edp-618."""
    kernel_source = Path(kernel_source)
    patch_files = [Path(p) for p in patches]
    require(patch_files, 'No SPX s2idle patches specified')
    edp_tree = Path(external_source) / 'edp-618' if external_source is not None else None
    if edp_tree is not None:
        require((edp_tree / 'drivers/gpu/drm/msm').is_dir(),
                'External MSM override directory is missing: ' + str(edp_tree))
    applied = []
    for patch in patch_files:
        require(patch.is_file(), 'SPX s2idle patch is missing: ' + str(patch))
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
        changed = _apply_patch_idempotent(patch, kernel_source, run)
        hashes = {}
        for name in touched:
            path = kernel_source / name
            require(path.is_file(), 'Patched file missing: ' + name)
            hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        entry = {
            'path': str(patch),
            'sha256': hashlib.sha256(patch.read_bytes()).hexdigest(),
            'applied': changed,
            'files': hashes,
        }
        if edp_tree is not None and msm_blocks:
            if len(msm_blocks) == len(blocks):
                ext_changed = _apply_patch_idempotent(patch, edp_tree, run)
            else:
                with tempfile.TemporaryDirectory() as tmpdir:
                    ext_patch = Path(tmpdir) / ('edp-618-' + patch.name)
                    ext_patch.write_text(header + ''.join(block for _, block in msm_blocks))
                    ext_changed = _apply_patch_idempotent(ext_patch, edp_tree, run)
            ext_hashes = {}
            for name in msm_touched:
                ext_path = edp_tree / name
                require(ext_path.is_file(), 'Patched external MSM file missing: ' + name)
                ext_hashes[name] = hashlib.sha256(ext_path.read_bytes()).hexdigest()
                if name in pre_matched_msm:
                    require(ext_hashes[name] == hashes[name],
                            'External MSM patch result diverged from kernel: ' + name)
            entry['external_applied'] = ext_changed
            entry['external_files'] = ext_hashes
        applied.append(entry)
    return {
        'patch_count': len(applied),
        'patches': applied,
        'external_msm_synced': edp_tree is not None,
    }


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


KSU_MODULES_HOOK = b'    sq2-ksu-modules-boot > /logs/sq2-ksu-modules-boot.txt 2>&1 &\n'
IRQ_BALANCE_HOOK = b'    spx-irq-balance-boot > /logs/spx-irq-balance.txt 2>&1 &\n'
POWER_TUNE_HOOK = b'    spx-power-tune-boot > /logs/spx-power-tune.txt 2>&1 &\n'


def install_irq_balance(records, bodies):
    """Install portable one-shot GICv3 peripheral IRQ affinity helper into initramfs."""
    init = bodies['init']
    require(init.count(ANCHOR) == 1 and init.count(IRQ_BALANCE_HOOK) <= 1,
            'Unknown IRQ balance boot handoff')
    if IRQ_BALANCE_HOOK not in init:
        init = init.replace(ANCHOR, IRQ_BALANCE_HOOK + ANCHOR)
    bodies['init'] = init
    helper = (ROOT / 'probe/test2/spx-irq-balance-boot').read_bytes().replace(b'\r\n', b'\n')
    subprocess.run(['sh', '-n'], input=helper, check=True, capture_output=True)
    bodies['bin/spx-irq-balance-boot'] = helper
    records['bin/spx-irq-balance-boot'] = new_record(0o100755)


def install_power_tune(records, bodies):
    """Install portable one-shot NVMe APST and idle host-only USB3 runtime PM helper."""
    init = bodies['init']
    require(init.count(ANCHOR) == 1 and init.count(POWER_TUNE_HOOK) <= 1,
            'Unknown power tune boot handoff')
    if POWER_TUNE_HOOK not in init:
        init = init.replace(ANCHOR, POWER_TUNE_HOOK + ANCHOR)
    bodies['init'] = init
    helper = (ROOT / 'probe/test2/spx-power-tune-boot').read_bytes().replace(b'\r\n', b'\n')
    subprocess.run(['sh', '-n'], input=helper, check=True, capture_output=True)
    bodies['bin/spx-power-tune-boot'] = helper
    records['bin/spx-power-tune-boot'] = new_record(0o100755)


_EVENT_WAIT_CACHE = {}
EVENT_WATCHER_HELPERS = (
    'cover-input-watch',
    'bluetooth-input-watch',
    'usb-input-watch',
    'usb-network-watch',
    'lab-input-posture',
    'touch-resume-watch',
)


def build_event_wait(output=None):
    """Cross-compile static AArch64 probe/test2/spx-event-wait.c deterministically."""
    src = ROOT / 'probe/test2/spx-event-wait.c'
    cc = os.environ.get('SPX_EVENT_WAIT_CROSS_CC', os.environ.get('SPX_TYPEC_CROSS_CC', 'aarch64-linux-gnu-gcc'))
    src_hash = hashlib.sha256(src.read_bytes()).hexdigest()
    cache_key = (cc, src_hash)
    if output is None and cache_key in _EVENT_WAIT_CACHE:
        return _EVENT_WAIT_CACHE[cache_key]

    def _compile(out_path):
        out_path = Path(out_path)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        env = dict(os.environ, SOURCE_DATE_EPOCH='0')
        subprocess.run([
            cc, '-std=c11', '-static', '-Os', '-Wall', '-Wextra', '-Werror',
            '-Wl,--build-id=none', '-frandom-seed=spx-event-wait',
            f'-ffile-prefix-map={ROOT}=/src',
            str(src), '-o', str(out_path),
        ], env=env, check=True)
        return out_path.read_bytes()

    if output is not None:
        return _compile(output)
    with tempfile.TemporaryDirectory(prefix='spx-event-wait-') as td:
        built = _compile(Path(td) / 'spx-event-wait')
    _EVENT_WAIT_CACHE[cache_key] = built
    return built


def install_powerhint(records, bodies):
    """Install configs/powerhint.sq2.json into the vendor overlay powerhint path."""
    raw = (ROOT / 'configs/powerhint.sq2.json').read_bytes().replace(b'\r\n', b'\n')
    parsed = json.loads(raw.decode('utf-8'))
    require(isinstance(parsed.get('Nodes'), list) and parsed['Nodes'], 'powerhint.sq2.json missing Nodes')
    require(isinstance(parsed.get('Actions'), list) and parsed['Actions'], 'powerhint.sq2.json missing Actions')
    records.setdefault('etc/googlebook-security', new_record(0o040755))
    bodies.setdefault('etc/googlebook-security', b'')
    records.setdefault('etc/googlebook-security/etc', new_record(0o040755))
    bodies.setdefault('etc/googlebook-security/etc', b'')
    bodies['etc/googlebook-security/etc/powerhint.json'] = raw
    records['etc/googlebook-security/etc/powerhint.json'] = new_record(0o100644)


def install_event_watchers(records, bodies, event_wait_bytes=None):
    """Install event-driven spx-event-wait binary and updated watcher scripts."""
    binary = event_wait_bytes if event_wait_bytes is not None else build_event_wait()
    require(binary[:4] == b'\x7fELF', 'spx-event-wait must be an ELF binary')
    bodies['bin/spx-event-wait'] = binary
    records['bin/spx-event-wait'] = new_record(0o100755)
    for name in EVENT_WATCHER_HELPERS:
        helper = (ROOT / 'probe/test2' / name).read_bytes().replace(b'\r\n', b'\n')
        subprocess.run(['sh', '-n'], input=helper, check=True, capture_output=True)
        bodies['bin/' + name] = helper
        records['bin/' + name] = new_record(0o100755)


def install_ksu_userspace(records, bodies, ksu_userspace):
    """Replace bundled etc/sq2-ksu/{ksud,manager-verified.apk} and ksu-framework-boot for upgraded KSU kernels."""
    if not ksu_userspace:
        return
    helper = (ROOT / 'probe/test2/ksu-framework-boot').read_bytes().replace(b'\r\n', b'\n')
    subprocess.run(['sh', '-n'], input=helper, check=True, capture_output=True)
    bodies['bin/ksu-framework-boot'] = helper
    records['bin/ksu-framework-boot'] = new_record(0o100755)
    bodies['etc/sq2-ksu/ksud'] = ksu_userspace['ksud_bytes']
    records['etc/sq2-ksu/ksud'] = new_record(0o100755)
    bodies['etc/sq2-ksu/manager-verified.apk'] = ksu_userspace['apk_bytes']
    records['etc/sq2-ksu/manager-verified.apk'] = new_record(0o100644)
    bodies['etc/sq2-ksu/expected-version'] = f"{int(ksu_userspace['version_code'])}\n".encode('ascii')
    records['etc/sq2-ksu/expected-version'] = new_record(0o100644)


def install_ksu_modules(records, bodies, ksu_modules):
    """Bundle validated KernelSU module zips into initramfs and wire one-time installer hook."""
    if not ksu_modules:
        return []
    init = bodies['init']
    require(init.count(ANCHOR) == 1 and init.count(KSU_MODULES_HOOK) <= 1,
            'Unknown KSU modules boot handoff')
    if KSU_MODULES_HOOK not in init:
        init = init.replace(ANCHOR, KSU_MODULES_HOOK + ANCHOR)
    bodies['init'] = init
    helper = (ROOT / 'probe/test2/sq2-ksu-modules-boot').read_bytes().replace(b'\r\n', b'\n')
    subprocess.run(['sh', '-n'], input=helper, check=True, capture_output=True)
    bodies['bin/sq2-ksu-modules-boot'] = helper
    records['bin/sq2-ksu-modules-boot'] = new_record(0o100755)
    installed = []
    for name, body in sorted(ksu_modules.items()):
        require(body and name.endswith('.zip') and '/' not in name,
                'Invalid KSU module archive name: ' + name)
        rel = 'etc/sq2-ksu-modules/' + name
        bodies[rel] = body
        records[rel] = new_record(0o100644)
        installed.append(rel)
    return installed


def install_userdata_growth(records, bodies, compiled_resize=None):
    import tempfile
    if compiled_resize is not None:
        bodies['bin/resize-userdata-partition'] = compiled_resize
    else:
      with tempfile.TemporaryDirectory(prefix='spx-grow-build-') as tmp:
        binary=Path(tmp)/'resize-userdata-partition'
        subprocess.run(['aarch64-linux-gnu-gcc','-static','-Os','-Wall','-Wextra',
                        str(ROOT/'probe/storage/resize-userdata-partition.c'),'-o',str(binary)],check=True)
        bodies['bin/resize-userdata-partition']=binary.read_bytes()
    for name in ('grow-image-userdata','surface-swap-boot'):
        bodies['bin/'+name]=(ROOT/'probe/test2'/name).read_bytes().replace(b'\r\n',b'\n')
    for name in ('resize-userdata-partition','grow-image-userdata','surface-swap-boot'):
        records['bin/'+name]=new_record(0o100755)

def assemble_runtime(private, output, compiled=None, graphics='hybrid', ksu_modules=None, camera=None, sensors=None, anonymous=False,
                     runtime_replacements=None, graphics_inputs=None, suspend=None, compiled_helpers=None, typec=None, gnss=None, face=None, ksu_userspace=None,
                     ota_version=None, source_commit=None, super_sha256='', vendor_build_prop=None):
    runtime = private/'full-image-runtime'
    records = json.loads((runtime/'records.json').read_text())
    bodies = {name: b'' if rec[1][1] & 0o170000 == 0o040000 else (runtime/'files'/name).read_bytes()
              for name,rec in records.items()}
    if vendor_build_prop is not None:
        from spx_super_delta import adapt_vendor_build_prop
        bodies['etc/googlebook-security/build.prop'] = adapt_vendor_build_prop(vendor_build_prop)
        records.setdefault('etc/googlebook-security/build.prop', new_record(0o100644))
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
                if typec and name.startswith('etc/spx-typec/'):
                    continue  # Replaced by modules rebuilt against this Image below.
                if sensors and name.startswith('etc/spx-sensors/modules/'):
                    continue  # Replaced by freshly built sensor modules below.
                if camera and name.startswith('etc/spx-camera/modules/'):
                    continue  # Replaced below by modules built against this kernel.
                require(Path(name).name in by_name, 'Required runtime module not built: '+name)
                bodies[name] = by_name[Path(name).name].read_bytes()
        for path in modules.rglob('*'):
            if not path.is_file() or path.is_symlink():
                continue
            name = path.relative_to(stage).as_posix()
            bodies[name] = path.read_bytes()
            records[name] = new_record(0o100644)
    for name, body in (runtime_replacements or {}).items():
        require(name in bodies, 'Unknown runtime replacement: '+name)
        bodies[name] = body
    # Apply after component reuse too; keep input metadata and credentials
    # separate, and never inherit the old 4096-frame playback cadence.
    module('primary_audio', 'prepare-sq2-primary-audio.py').install(bodies)
    graphics_report = None
    if graphics == 'mesa':
        graphics_report = mesa_runtime(records, bodies)
    elif graphics == 'hybrid':
        graphics_report = hybrid_runtime(records, bodies, **(graphics_inputs or {}))
    elif graphics is not None:
        raise ValueError('Unknown graphics mode (only hybrid is supported): ' + repr(graphics))
    init = bodies['init']
    require(init.count(ANCHOR) == 1, 'Unknown Android handoff')
    init = re.sub(rb'(?m)^(\s*el2-terminal-autostart)[ \t]+[0-9]+(?=[ \t]*(?:>|&|\n|$))',rb'\1',init)
    if HOOK not in init:
        init = init.replace(ANCHOR,HOOK+ANCHOR)
    bodies['init'] = init
    bodies['bin/apply-single-user'] = (ROOT/'probe/test2/apply-single-user').read_bytes().replace(b'\r\n',b'\n')
    records['bin/apply-single-user'] = new_record(0o100755)
    bodies['bin/apply-device-info'] = (ROOT/'probe/test2/apply-device-info').read_bytes().replace(b'\r\n', b'\n')
    records['bin/apply-device-info'] = new_record(0o100755)
    install_irq_balance(records, bodies)
    install_power_tune(records, bodies)
    install_powerhint(records, bodies)
    install_event_watchers(records, bodies, (compiled_helpers or {}).get('bin/spx-event-wait'))
    if 'bin/lte-framework-boot' in bodies:
        lte_boot = (ROOT / 'probe/lte/lte-framework-boot').read_bytes().replace(b'\r\n', b'\n')
        subprocess.run(['sh', '-n'], input=lte_boot, check=True, capture_output=True)
        bodies['bin/lte-framework-boot'] = lte_boot
    if 'bin/power-input-boot' in bodies:
        power_input_boot = (ROOT / 'probe/test2/power-input-boot').read_bytes().replace(b'\r\n', b'\n')
        subprocess.run(['sh', '-n'], input=power_input_boot, check=True, capture_output=True)
        bodies['bin/power-input-boot'] = power_input_boot
    # Take the current committed helper, while the accepted cumulative IPTSd
    # binary remains the fixed private runtime input (not the old touch cache).
    bodies['bin/touch-calibration-boot'] = (ROOT/'probe/test2/touch-calibration-boot').read_bytes().replace(b'\r\n',b'\n')
    records.setdefault('bin/touch-calibration-boot', new_record(0o100755))
    # Rebuild the default wizard instead of retaining a stale private-runtime APK.
    wizard = 'etc/sq2-touch-calibration/wizard.apk'
    compiled_helpers = compiled_helpers or {}
    if wizard in compiled_helpers:
        mode, bodies[wizard] = 0o100644, compiled_helpers[wizard]
    else:
        touch_files, _ = module('full_image_touch', 'build-touch-calibration.py').build(ROOT)
        mode, bodies[wizard] = touch_files[wizard]
    records[wizard] = new_record(mode)
    wifi_hal = 'etc/googlebook-security/lib64/libsq2-wifi-hal.so'
    if wifi_hal in compiled_helpers:
        bodies[wifi_hal] = compiled_helpers[wifi_hal]
        records[wifi_hal] = new_record(0o100644)
    elif wifi_hal in bodies and wifi_hal not in (runtime_replacements or {}):
        connectivity_bin = ROOT / 'out/connectivity/libsq2-wifi-hal.so'
        connectivity_sources = [
            ROOT / 'probe/test2/sq2-wifi-hal.cpp',
            ROOT / 'probe/test2/sq2-wifi-driver.c',
            ROOT / 'probe/test2/sq2-hci-bridge.c',
            ROOT / 'scripts/build-sq2-connectivity.sh',
        ]
        source_mtime = max(p.stat().st_mtime for p in connectivity_sources)
        headers_ready = (ROOT / 'out/connectivity/source/headers/wifi_hal.h').is_file()
        is_fresh = connectivity_bin.is_file() and connectivity_bin.stat().st_mtime >= source_mtime
        if not is_fresh and headers_ready:
            ndk_clang = next(
                (
                    cand for cand in (
                        Path('/mnt/spx-workspace/toolchains/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang'),
                        Path('/root/.cache/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang'),
                    ) if cand.is_file()
                ),
                None,
            )
            if ndk_clang is not None:
                subprocess.run(['bash', str(ROOT / 'scripts/build-sq2-connectivity.sh')], check=True)
                is_fresh = connectivity_bin.is_file() and connectivity_bin.stat().st_mtime >= source_mtime
        if connectivity_bin.is_file():
            require(is_fresh, 'Stale out/connectivity/libsq2-wifi-hal.so; rebuild via scripts/build-sq2-connectivity.sh')
            bodies[wifi_hal] = connectivity_bin.read_bytes()
            records[wifi_hal] = new_record(0o100644)
    if graphics == 'hybrid':
        from full_image_advanced_control import install as install_advanced_control
        # Recompile this small APK and install its bridge daemon on every assembly, including component reuse.
        install_advanced_control(records, bodies, new_record)
    from full_image_hygiene_runtime import (
        EC_STUB_TARGET,
        SYSUI_OVERLAY_REL,
        install as install_hygiene_runtime,
    )
    install_hygiene_runtime(
        records,
        bodies,
        new_record,
        private=private,
        sysui_overlay=compiled_helpers.get('etc/sq2-qti-system/root/' + SYSUI_OVERLAY_REL),
        ec_stub=compiled_helpers.get(EC_STUB_TARGET),
    )
    from full_image_radio import (
        RADIO_BIN_TARGET,
        RADIO_OVERLAY_TARGET,
        install as install_radio_runtime,
    )
    install_radio_runtime(
        records,
        bodies,
        new_record,
        private=private,
        radio_bin=compiled_helpers.get(RADIO_BIN_TARGET),
        radio_overlay=compiled_helpers.get(RADIO_OVERLAY_TARGET),
    )
    # Universal builds do not rewrite reference hashes or gate boot on content
    # hashes. Preserve imported manifests as metadata; no startup consumer uses
    # them for validation after this policy is applied.
    from full_image_video import install_p010_stage
    install_p010_stage(records, bodies, new_record)
    from runtime_boot_policy import apply
    apply(bodies)
    from full_image_home import apply as apply_home
    apply_home(records, bodies, new_record)
    install_default_wallpaper(private, records, bodies)
    if ksu_modules is None and graphics == 'hybrid':
        priv_df = private / 'ksu-modules' / 'device_faker.zip'
        if priv_df.is_file():
            from build_device_faker_fork import validate_device_faker_zip
            df_bytes = priv_df.read_bytes()
            validate_device_faker_zip(df_bytes)
            ksu_modules = {'device_faker.zip': df_bytes}
    if ksu_userspace:
        install_ksu_userspace(records, bodies, ksu_userspace)
    if ksu_modules:
        install_ksu_modules(records, bodies, ksu_modules)
    install_userdata_growth(records, bodies, compiled_helpers.get('bin/resize-userdata-partition'))
    if camera is not None:
        from full_image_camera import install
        install(records, bodies, camera, new_record)
    if sensors is not None:
        from full_image_sensors import install
        install(records, bodies, sensors, new_record)
    if suspend is not None:
        from full_image_suspend import install
        install(records, bodies, suspend, new_record)
    if typec is not None:
        from full_image_typec import install
        install(records, bodies, typec, new_record)
    if gnss is not None:
        from full_image_gnss import install as install_gnss
        install_gnss(records, bodies, gnss, new_record)
    if face is not None:
        from full_image_face import install as install_face
        install_face(records, bodies, face, new_record)
    if anonymous:
        from full_image_anonymous import apply as anonymize
        privacy = anonymize(records, bodies, new_record)
        (output.parent/'PRIVACY.json').write_text(json.dumps(privacy, indent=2)+'\n')
    from full_image_rescue import install as install_rescue
    install_rescue(records, bodies, new_record, compiled_wifi_tui=compiled_helpers.get('bin/spx-wifi-tui'))
    from full_image_ota import install as install_ota
    install_ota(
        records,
        bodies,
        new_record,
        compiled_ota_tool=compiled_helpers.get('bin/spx-ota-tool'),
        version=ota_version,
        source_commit=source_commit,
        super_sha256=super_sha256,
    )
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
    output.write_bytes(archive(sorted(bodies,key=lambda n:(n.count('/'),n)))+(b'' if anonymous else archive(sorted(identity))))
    return graphics_report


def build(args, compile_kernel):
    reuse = None
    replacements, graphics_inputs, compiled_helpers = {}, {}, {}
    if getattr(args, 'reuse_components', None):
        require(args.kernel_mode == 'source' and not args.kernel_patches,
                'Component reuse requires source mode and no new --kernel-patches')
        from full_image_reuse import load as load_reuse
        reuse, replacements, graphics_inputs, reuse_receipt = load_reuse(args.reuse_components)
        from full_image_reuse import prepared_runtime
        graphics_extra, compiled_helpers = prepared_runtime(reuse_receipt.get('prepared_runtime'))
        graphics_inputs.update(graphics_extra)
        for attr, role in (('camera_inputs','camera'),('sensor_inputs','sensors')):
            existing = getattr(args, attr, None)
            require(existing is None or existing.resolve() == reuse[role], 'Conflicting reused '+role+' inputs')
            setattr(args, attr, reuse[role])
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
    from full_image_boot_source import check_inputs
    boot_gnu_efi = check_inputs(getattr(args, 'boot_gnu_efi', None) or private/'kernel-6.18/gnu-efi')
    # Fail before the expensive kernel build if the reusable Android input is
    # missing; the image never silently falls back to the long donor wakelock.
    mica_ota_base_dir = getattr(args, 'mica_ota_base', None)
    mica_ota_meta = None
    mica_desktop_services_jar = None
    mica_services_jar = None
    mica_vendor_build_prop = None
    mica_target_slices_dir = None
    mica_base_slices_dir = None
    if mica_ota_base_dir is not None:
        mica_ota_base_dir = Path(mica_ota_base_dir).resolve()
        mica_manifest_path = mica_ota_base_dir / 'MICA-OTA-BASE.json'
        regular(mica_manifest_path)
        mica_ota_meta = json.loads(mica_manifest_path.read_text(encoding='utf-8'))
        mica_target_slices_dir = Path(mica_ota_meta['target_slices_dir']).resolve()
        mica_base_slices_dir = Path(mica_ota_meta['base_slices_dir']).resolve()
        android_inputs_dir = Path(mica_ota_meta['android_inputs_dir']).resolve()
        mica_desktop_services_jar = regular(android_inputs_dir / 'desktop-services.jar')
        mica_services_jar = regular(android_inputs_dir / 'services.jar')
        mica_vendor_build_prop = regular(android_inputs_dir / 'vendor_build.prop').read_bytes()
    else:
        regular(private/'suspend/desktop-services.jar')
        regular(private/'typec/services.jar')
    source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    template=regular(args.template or private/'template.img')
    require(args.jobs>0,'--jobs must be positive')
    require(not args.output.exists(),'Full build output must be fresh')
    args.output.mkdir(parents=True)
    if reuse:
        (args.output/'REUSED-COMPONENTS.json').write_text(json.dumps(reuse_receipt,indent=2)+'\n')
    records=[]
    def run(command, cwd=None):
        label='step-'+str(len(records))
        with (args.output/(label+'.stdout')).open('w') as out,(args.output/(label+'.stderr')).open('w') as err:
            result=subprocess.run(list(map(str,command)),stdout=out,stderr=err,cwd=cwd)
        records.append({'command':list(map(str,command)),'exit':result.returncode,
                        'stdout':label+'.stdout','stderr':label+'.stderr'})
        (args.output/'BUILD-COMMANDS.json').write_text(json.dumps(records,indent=2)+'\n')
        require(result.returncode==0,'Build failed: '+str(args.output/(label+'.stderr')))
    require(args.graphics == 'hybrid', 'Only hybrid graphics mode is supported: ' + repr(args.graphics))
    camera = None
    if not getattr(args, 'without_camera', False):
        from full_image_camera import load
        camera = load(getattr(args, 'camera_inputs', None) or private/'camera',
                      source_build=args.kernel_mode == 'source')
    sensors = None
    if not getattr(args, 'without_sensors', False):
        from full_image_sensors import load
        sensors = load(getattr(args, 'sensor_inputs', None) or private/'sensors')
    gnss = None
    if not getattr(args, 'without_gnss', False):
        from full_image_gnss import HAL_TARGET as GNSS_HAL_TARGET, load as load_gnss
        gnss = load_gnss(
            gnss_hal_path=getattr(args, 'gnss_hal', None),
            compiled_hal=compiled_helpers.get(GNSS_HAL_TARGET),
        )
    face = None
    if not getattr(args, 'without_face', False):
        from full_image_face import CLI_TARGET as FACE_CLI_TARGET, HAL_TARGET as FACE_HAL_TARGET, load as load_face
        compiled_face = None
        if FACE_HAL_TARGET in compiled_helpers:
            compiled_face = {
                'hal': compiled_helpers[FACE_HAL_TARGET],
                'cli': compiled_helpers.get(FACE_CLI_TARGET),
            }
        face = load_face(
            face_hal_path=getattr(args, 'face_hal', None),
            compiled_payload=compiled_face,
        )
    compiled=reuse['kernel'] if reuse else None
    kernel_build_dir = None
    kernel_cross = None
    ksu_next_info = None
    patch_report = None
    if args.kernel_mode=='source' and not reuse:
        source=private/'kernel-6.18/source'
        recipe=json.loads((source/'SOURCE.json').read_text())
        require(recipe.get('current_release_patches') is True,'Source inputs are not prepared for current release')
        compiled=args.output/'kernel-export'
        build_dir=args.build_dir or args.output/'kernel-build'
        kernel_build_dir = build_dir
        extracted=build_dir.parent/(build_dir.name+'-sources')
        require(not extracted.exists(),'Source extraction directory must be fresh: '+str(extracted))
        extracted.mkdir(parents=True)
        with tarfile.open(source/recipe['archive']) as archive:
            archive.extractall(extracted,filter='data')
        if not getattr(args, 'skip_ksu_upgrade', False):
            from build_ksu_next import apply_to_kernel_source, resolve_latest_ksu_next
            ksu_next_info = resolve_latest_ksu_next(
                explicit_src=getattr(args, 'ksu_next_src', None),
            )
            apply_to_kernel_source(
                extracted / 'kernel',
                ksu_next_info['src_dir'],
                tag=ksu_next_info['tag'],
                version_code=ksu_next_info['version_code'],
            )
        for patch in sorted((ROOT/'patches/spx-portable-boot').glob('*.patch')):
            run(['patch','--batch','--forward','--fuzz=0','-p1','-i',patch],
                cwd=extracted/'kernel')
        # The preserved archive predates the accepted QTI HWC kernel contract.
        # A source build must carry those adaptations, not replace the runtime
        # with an older MSM merely because its release string matches.
        from full_image_display import prepare as prepare_display
        display = prepare_display(extracted/'external')
        (args.output/'DISPLAY-SOURCE.json').write_text(json.dumps(display,indent=2)+'\n')
        s2idle_patches = apply_spx_s2idle_patches(
            extracted/'kernel', run, external_source=extracted/'external')
        (args.output/'SPX-S2IDLE-PATCHES.json').write_text(json.dumps(s2idle_patches,indent=2)+'\n')
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
                extra_patches = (DEFAULT_A6XX_MMIO_FIX_PATCHES,
                                 DEFAULT_DPU_CRTC_KICKOFF_PATCHES,
                                 ROOT / "patches/drm-618-msm-submit",
                                 ROOT / "patches/drm-618-msm-vmbind-resv",
                                 DEFAULT_MSM_ASYNC_CLOSE_PATCHES)
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
        kernel_cross = cross
        run([sys.executable,ROOT/'scripts/build.py','compile','--source',extracted/'kernel',
             '--external-sources',extracted/'external','--config',DEFAULT_KERNEL_CONFIG,
             '--build-dir',build_dir,'--output',compiled,'--cross-compile',cross,
             '--jobs',str(args.jobs),'--build-only'])
    if camera is not None and compiled and not reuse:
        from full_image_camera import rebuild_modules
        rebuild_modules(camera, extracted/'kernel', build_dir, args.output, cross, run)
    if sensors is not None and compiled and not reuse:
        from full_image_sensors import rebuild_modules
        rebuild_modules(sensors, extracted/'kernel', build_dir, args.output, cross, run)
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
    if camera is not None:
        from full_image_camera import prepare_dtb
        camera_dt = bundle/'android-el2.dtb'
        camera_dt.write_bytes(prepare_dtb(camera_dt.read_bytes()))
    if sensors is not None:
        from full_image_sensors import prepare_dtb
        sensor_dt = bundle/'android-el2.dtb'
        sensor_dt.write_bytes(prepare_dtb(sensor_dt.read_bytes(), sensors))
        (bundle/'qebspilaa64.efi').write_bytes(sensors['qebspilaa64.efi'])
    ksu_modules = {}
    ksu_modules_meta = None
    device_faker_meta = None
    if args.graphics == 'hybrid':
        from build_ksu_modules import resolve_default_ksu_modules
        ksu_modules, ksu_modules_meta = resolve_default_ksu_modules(
            private=private,
            skip_zygisk_next=bool(getattr(args, 'skip_zygisk_next', False)),
            zygisk_next_zip=getattr(args, 'zygisk_next_zip', None),
            skip_game_opt=bool(getattr(args, 'skip_game_opt', False)),
            game_opt_zip=getattr(args, 'game_opt_zip', None),
            skip_device_faker=bool(getattr(args, 'skip_device_faker', False)),
            device_faker_zip=getattr(args, 'device_faker_zip', None),
            device_faker_src=getattr(args, 'device_faker_src', None),
            kernel_dir=kernel_build_dir,
            cross_compile=kernel_cross,
        )
        device_faker_meta = ksu_modules_meta.get('device_faker_fork')
    # Both prebuilt and source default builds must contain the accepted sleep
    # fix. Source mode selects this build's module, never a same-release donor.
    from full_image_suspend import load as load_suspend
    if mica_desktop_services_jar is not None:
        suspend = load_suspend(private/'suspend', bundle/'Image', compiled, desktop_services_jar=mica_desktop_services_jar)
    else:
        suspend = load_suspend(private/'suspend', bundle/'Image', compiled)
    from full_image_typec import load as load_typec, prepare_dtb as typec_dtb
    if mica_services_jar is not None:
        typec = load_typec(private/'typec', bundle/'Image', args.output/'typec', compiled, services_jar=mica_services_jar)
    else:
        typec = load_typec(private/'typec', bundle/'Image', args.output/'typec', compiled)
    dt=bundle/'android-el2.dtb'
    dt.write_bytes(typec_dtb(dt.read_bytes()))
    dt.write_bytes(module('spx_backlight_dt', 'prepare-spx-backlight-dtb.py').prepare(dt.read_bytes()))
    dt.write_bytes(module('spx_spmi_pdc_dt', 'prepare-spx-spmi-pdc-dtb.py').prepare(dt.read_bytes()))
    dt.write_bytes(module('spx_cpu_eas_dt', 'prepare-spx-cpu-eas-dtb.py').prepare(dt.read_bytes()))
    dt.write_bytes(module('spx_gpu_bw_dt', 'prepare-spx-gpu-bw-dtb.py').prepare(dt.read_bytes()))
    lid_dt = module('spx_lid_dt', 'prepare-spx-lid-dtb.py')
    hall_lid_enabled = bool(
        (args.kernel_mode == 'source' or getattr(args, 'enable_hall_lid', False))
        and not getattr(args, 'without_hall_lid', False)
    )
    if not lid_dt.has_hall_lid_node(dt.read_bytes()):
        dt.write_bytes(
            lid_dt.prepare(
                dt.read_bytes(),
                enable=hall_lid_enabled,
                kernel_source=(extracted/'kernel' if compiled and not reuse else None),
            )
        )
    if args.graphics == 'hybrid' and args.kernel_mode == 'source':
        from full_image_qti_submit import build_payload
        graphics_inputs['qti_submit_payload'] = build_payload(
            private, args.output/'qti-submit-runtime',
            None if reuse else extracted/'kernel/include/uapi')
    graphics_report = assemble_runtime(
        private,
        bundle/'initramfs.cpio.gz',
        compiled,
        graphics=args.graphics,
        ksu_modules=ksu_modules,
        ksu_userspace=ksu_next_info,
        camera=camera,
        sensors=sensors,
        suspend=suspend,
        typec=typec,
        gnss=gnss,
        face=face,
        anonymous=True,
        runtime_replacements=replacements,
        graphics_inputs=graphics_inputs,
        compiled_helpers=compiled_helpers,
        ota_version=getattr(args, 'ota_version', None),
        source_commit=source_commit,
        vendor_build_prop=mica_vendor_build_prop,
    )
    esp_only = bool(getattr(args, 'esp_only', False))
    from disk_image import build as disk_build
    disk_args=argparse.Namespace(output=args.output/'disk',template=template,recovery_zip=None,
        resources=private,private_root=private,prepared_bundle=bundle,
        single_user_initramfs=bundle/'initramfs.cpio.gz',
        expected_single_user_sha256=None,
        size_gib=getattr(args, 'size_gib', None),
        esp_mib=getattr(args, 'esp_mib', None),
        super_mib=getattr(args, 'super_mib', None),
        super_slices_dir=mica_target_slices_dir,
        minimal_partitions=bool(getattr(args, 'minimal_partitions', True)),
        jobs=args.jobs,build_only=False,resume_build=False,kernel_mode=args.kernel_mode,
        graphics_argument=graphics_token,esp_only=esp_only,anonymous=True,
        boot_gnu_efi=boot_gnu_efi)
    if sensors is not None:
        from full_image_sensors import FIRMWARE
        disk_args.additional_firmware={FIRMWARE.removeprefix('firmware/'): sensors[FIRMWARE]}
    report=disk_build(disk_args)
    ota_report = None
    if not getattr(args, 'skip_ota', False):
        from build_ota_package import build_ota_package
        requested_ota_mode = getattr(args, 'ota_mode', 'auto')
        effective_ota_mode = (
            'super-delta'
            if (mica_ota_meta is not None and requested_ota_mode in ('auto', 'super-delta'))
            else requested_ota_mode
        )
        ota_report = build_ota_package(
            output_dir=args.output / 'ota',
            build_output=args.output,
            previous_manifest=getattr(args, 'ota_baseline', None),
            previous_super_sha256=getattr(args, 'ota_previous_super_sha256', None),
            mode=effective_ota_mode,
            super_unchanged_hint=(None if mica_ota_meta is not None else True),
            version=getattr(args, 'ota_version', None),
            source_commit=source_commit,
            base_slices_dir=(mica_base_slices_dir if effective_ota_mode == 'super-delta' else None),
            target_slices_dir=(mica_target_slices_dir if effective_ota_mode == 'super-delta' else None),
        )
    report.update(source_compiled=bool(compiled),private_root=str(private),
        mica_ota_base=(
            {
                'base_build': mica_ota_meta['base_build'],
                'target_build': mica_ota_meta['target_build'],
                'target_build_id': mica_ota_meta['target_build_id'],
                'security_patch': mica_ota_meta['security_patch'],
                'vendor_trimmed': mica_ota_meta['vendor_trimmed'],
                'compacted_super_bytes': mica_ota_meta['compacted_super_bytes'],
            }
            if mica_ota_meta is not None else None
        ),
        source_commit=source_commit,
        build_time_parent_initramfs_used=False,postbuild_tests_performed=not esp_only,
        private_identity_present=False, first_setup_preserved=False, first_setup_skipped=True, target_models=["sq1", "sq2"],
        graphics=args.graphics,graphics_boot_option=graphics_token,
        graphics_report=graphics_report,
        ota=ota_report,
        suspend_enabled=True,
        hall_lid_enabled=hall_lid_enabled,
        typec_enabled=True, lower_usb_default='auto',
        suspend_module_source=('matched-incremental-rebuild' if reuse else 'fresh-compiled') if compiled else 'image-paired-prebuilt',
        ksu_next=({k: str(v) if isinstance(v, Path) else v for k, v in ksu_next_info.items() if not k.endswith('_bytes')} if ksu_next_info else None),
        ksu_modules=ksu_modules_meta,
        device_faker_fork=device_faker_meta,
        sensors_enabled=sensors is not None,
        sensor_modules_rebuilt=bool(sensors is not None and compiled and not reuse),
        sensor_hal_sha256=_sha256(sensors["sensors.spx.so"]) if sensors else None,
        camera_enabled=camera is not None,
        camera_provider_sha256=_sha256(camera['provider']) if camera else None,
        camera_modules_rebuilt=bool(camera is not None and compiled and not reuse),
        gnss_enabled=gnss is not None,
        gnss_aidl_version=(4 if gnss is not None else None),
        gnss_hal_sha256=_sha256(gnss) if gnss is not None else None,
        face_enabled=face is not None,
        face_aidl_version=(4 if face is not None else None),
        face_hal_sha256=_sha256(face['hal']) if face is not None else None,
        components_reused=bool(reuse),
        kernel_compiled_this_run=bool(compiled and not reuse),
        esp_only=esp_only,
        kernel_patches=patch_report['patch_count'] if patch_report else 0,
        kernel_patches_external_msm_synced=(patch_report['external_msm_synced'] if patch_report else None),
        kernel_patches_note=('prebuilt accepted Image: no patches applied' if args.kernel_mode=='prebuilt'
                             else None),
        hardware_validated=False)
    (args.output/'FULL-IMAGE.json').write_text(json.dumps(report,indent=2)+'\n')
    return report
