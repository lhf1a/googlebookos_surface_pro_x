#!/usr/bin/env python3
"""Keep hardware video decode working in source-built and full-Mesa images.

Two independent regressions are handled here:

1. ``prepare-full-image-sources.py`` previously copied ``probe/video/*.h`` over
   ``external/venus/*.h`` after applying the system-PM and capability-export
   patches, overwriting two single-line hooks:
   - ``venus_sq2_pm_publish(core);`` in ``venus-sq2-core.h`` (leaving
     ``sq2_pm_status`` permanently ``unpublished``), and
   - ``#include "venus-sq2-avc-export.h"`` in ``venus-sq2-system-pm.h``
     (omitting ``sq2_{avc,hevc,vp8,vp9,mpeg2,mpeg1}_caps`` from ``venus-core``).
   ``ensure_venus_source_hooks`` restores both hooks idempotently in the copied
   external Venus module source tree before compilation so all ``.ko`` modules
   come genuinely from source compilation.

2. The full-Mesa variant removes ``etc/sq2-qti-system``.  The accepted OMX
   service is the native-surface build: it links QTI ``libgrallocutils.so``
   (for ``gralloc::GetBufferLayout(private_handle_t*, ...)``) and answers
   ``RequiresSwRenderer = 0``, which makes ACodec pass minigbm ANW buffers that
   QTI-gralloc validation rejects.  In ``mesa`` mode ``mesa_omx`` copies QTI
   ``libgrallocutils.so`` solely into the OMX service's private library dir
   (``etc/sq2-hidl/lib64/``, never ``/vendor/lib64/``) and patches the audited
   ``OMX_IndexParamVideoAndroidRequiresSwRenderer`` constant in
   ``quest2-omx-service`` from ``0`` to ``1`` so Venus hardware decoding outputs
   via ByteBuffer + Android ``SoftwareRenderer`` (+ ``sq2-p010-renderer``) into
   minigbm buffers composited by Mesa/SurfaceFlinger.

Host-side transformation only; no device access.
"""
import argparse
import hashlib
from pathlib import Path
import re

VENUS_MODULES = ('venus-core.ko', 'venus-dec.ko', 'venus-enc.ko')
VENUS_CORE_REQUIRED_STRINGS = (
    b'__param_str_sq2_avc_caps\0',
    b'__param_str_sq2_hevc_caps\0',
    b'__param_str_sq2_vp8_caps\0',
    b'__param_str_sq2_vp9_caps\0',
    b'__param_str_sq2_mpeg2_caps\0',
)

OMX_SERVICE = 'etc/sq2-hidl/quest2-omx-service'
OMX_SUMS = 'etc/sq2-hidl/SHA256SUMS'
OMX_GRALLOC_UTILS_SOURCE = 'etc/sq2-qti-system/root/lib64/libgrallocutils.so'
OMX_GRALLOC_UTILS = 'etc/sq2-hidl/lib64/libgrallocutils.so'
GRALLOC_LAYOUT_SYMBOL = b'_ZN7gralloc15GetBufferLayoutEP16private_handle_tPjS2_S2_\0'

# Quest2Node::getParameter(OMX_IndexParamVideoAndroidRequiresSwRenderer = 0x6f600011):
#   vaddr 0x51174 (file 0x4d174): mov w9,#0x11; movk w9,#0x6f60,lsl #16
#   vaddr 0x5126c (file 0x4d26c):
#     stur wzr,[x29,#-228]; add x0,sp,#0x330; str x0,[sp,#144];
#     bl hidl_vec(); ldr x0,[sp,#144]
#   becomes:
#     mov w8,#1; stur w8,[x29,#-228]; add x0,sp,#0x330;
#     bl hidl_vec(); add x0,sp,#0x330
# The bl stays at the exact same address; [sp,#144] has no other reader.
OMX_INDEX_CHECK_OFFSET = 0x4d174
OMX_INDEX_CHECK_BYTES = bytes.fromhex('29028052' '09ecad72')
OMX_SWRENDER_OFFSET = 0x4d26c
OMX_SWRENDER_BEFORE = bytes.fromhex('bfc311b8' 'e0c30c91' 'e04b00f9' '7a080094' 'e04b40f9')
OMX_SWRENDER_AFTER = bytes.fromhex('28008052' 'a8c311b8' 'e0c30c91' '7a080094' 'e0c30c91')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def aarch64_elf(data, label):
    require(len(data) >= 64 and data[:6] == b'\x7fELF\x02\x01' and
            int.from_bytes(data[18:20], 'little') == 183,
            'Expected AArch64 ELF64 little-endian binary: ' + label)


def software_renderer_service(service):
    """Return the RequiresSwRenderer=1 variant of the native OMX service."""
    aarch64_elf(service, OMX_SERVICE)
    idx_end = OMX_INDEX_CHECK_OFFSET + len(OMX_INDEX_CHECK_BYTES)
    end = OMX_SWRENDER_OFFSET + len(OMX_SWRENDER_BEFORE)
    require(len(service) >= end and
            service[OMX_INDEX_CHECK_OFFSET:idx_end] == OMX_INDEX_CHECK_BYTES,
            'OMX RequiresSwRenderer index anchor mismatch')
    require(service[OMX_SWRENDER_OFFSET:end] == OMX_SWRENDER_BEFORE,
            'Unexpected OMX RequiresSwRenderer patch site')
    return service[:OMX_SWRENDER_OFFSET] + OMX_SWRENDER_AFTER + service[end:]


def _rewrite_sums(text, replacements):
    lines = []
    decoded = text.decode().splitlines()
    for line in decoded:
        match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
        if match and match.group(2) in replacements:
            line = sha(replacements[match.group(2)]) + '  ' + match.group(2)
        lines.append(line)
    present = {m.group(1) for m in (re.fullmatch(r'[0-9a-f]{64}  (.+)', l)
                                    for l in decoded) if m}
    for name in sorted(set(replacements) - present):
        lines.append(sha(replacements[name]) + '  ' + name)
    return ('\n'.join(lines) + '\n').encode()


def mesa_omx(records, bodies, new_record):
    """Switch the OMX service to ByteBuffer/SoftwareRenderer for minigbm buffers.

    Must run before the closed QTI payload is removed (it supplies the QTI
    gralloc utility library that ``quest2-omx-service`` links against).
    """
    require(OMX_SERVICE in bodies and OMX_SUMS in bodies, 'OMX HIDL payload missing')
    utils = bodies.get(OMX_GRALLOC_UTILS_SOURCE)
    require(utils is not None, 'QTI gralloc utility library for OMX missing')
    aarch64_elf(utils, OMX_GRALLOC_UTILS_SOURCE)
    require(GRALLOC_LAYOUT_SYMBOL in utils,
            'QTI gralloc utility library lacks GetBufferLayout export')
    service = software_renderer_service(bodies[OMX_SERVICE])
    bodies[OMX_SERVICE] = service
    bodies[OMX_GRALLOC_UTILS] = utils
    records[OMX_GRALLOC_UTILS] = new_record(0o100644)
    bodies[OMX_SUMS] = _rewrite_sums(bodies[OMX_SUMS], {
        'quest2-omx-service': service, 'lib64/libgrallocutils.so': utils})
    return {'omx_output': 'bytebuffer-software-renderer',
            'omx_service_sha256': sha(service),
            'omx_private_libgrallocutils_sha256': sha(utils),
            'hardware_validated': False}


def ensure_venus_source_hooks(directory):
    """Idempotently restore the PM-publish and capability-export header hooks."""
    directory = Path(directory)
    core_h = directory / 'venus-sq2-core.h'
    pm_h = directory / 'venus-sq2-system-pm.h'
    export_h = directory / 'venus-sq2-avc-export.h'
    require(core_h.is_file() and pm_h.is_file() and export_h.is_file(),
            'Incomplete SQ2 Venus source directory: ' + str(directory))
    changed = []
    core_text = core_h.read_text()
    publish_done = ' if (ret) goto fail;\n venus_sq2_pm_publish(core);\n return 0;\nfail:'
    publish_raw = ' if (ret) goto fail;\n return 0;\nfail:'
    if publish_done not in core_text:
        require(core_text.count(publish_raw) == 1,
                'Unexpected venus_sq2_probe publish anchor in venus-sq2-core.h')
        core_h.write_text(core_text.replace(publish_raw, publish_done, 1))
        changed.append('venus-sq2-core.h')
    pm_text = pm_h.read_text()
    include_line = '#include "venus-sq2-avc-export.h"'
    stage_anchor = 'static const char *sq2_pm_stage'
    if include_line not in pm_text:
        require(pm_text.count(stage_anchor) == 1,
                'Unexpected sq2_pm_stage anchor in venus-sq2-system-pm.h')
        pm_h.write_text(pm_text.replace(stage_anchor, include_line + '\n' + stage_anchor, 1))
        changed.append('venus-sq2-system-pm.h')
    omx_h = directory / 'venus-sq2-omx.h'
    repo_omx_h = Path(__file__).resolve().parents[1] / 'probe/video/venus-sq2-omx.h'
    if omx_h.is_file():
        require(repo_omx_h.is_file(), 'Missing repo probe/video/venus-sq2-omx.h')
        want_omx = repo_omx_h.read_bytes()
        if omx_h.read_bytes() != want_omx:
            omx_h.write_bytes(want_omx)
            changed.append('venus-sq2-omx.h')
    return {'directory': str(directory), 'updated_headers': changed}


def verify_native_omx(bodies):
    """Verify quest2-omx-service retains RequiresSwRenderer=0 native ANW surface mode."""
    require(OMX_SERVICE in bodies, 'OMX HIDL payload missing: ' + OMX_SERVICE)
    service = bodies[OMX_SERVICE]
    aarch64_elf(service, OMX_SERVICE)
    idx_end = OMX_INDEX_CHECK_OFFSET + len(OMX_INDEX_CHECK_BYTES)
    end = OMX_SWRENDER_OFFSET + len(OMX_SWRENDER_BEFORE)
    require(len(service) >= end and
            service[OMX_INDEX_CHECK_OFFSET:idx_end] == OMX_INDEX_CHECK_BYTES,
            'OMX RequiresSwRenderer index anchor mismatch')
    require(service[OMX_SWRENDER_OFFSET:end] == OMX_SWRENDER_BEFORE,
            'OMX service is not in RequiresSwRenderer=0 native ANW mode')
    return {'omx_output': 'native-anw-surface',
            'omx_service_sha256': sha(service),
            'requires_sw_renderer': 0}


def validate_venus_core_module(body):
    """Verify a built venus-core.ko exports all SQ2 HFI capability parameters."""
    aarch64_elf(body, 'venus-core.ko')
    missing = [s.decode().rstrip('\0') for s in VENUS_CORE_REQUIRED_STRINGS if s not in body]
    require(not missing,
            'Compiled venus-core.ko lacks SQ2 capability exports: ' + ', '.join(missing))


def validate_venus_dec_module(body):
    """Verify built venus-dec.ko is valid AArch64 ELF and references DPB queueing."""
    aarch64_elf(body, 'venus-dec.ko')
    require(b'venus_helper_queue_dpb_bufs' in body,
            'Compiled venus-dec.ko lacks venus_helper_queue_dpb_bufs reference')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ensure-venus-hooks', type=Path, required=True)
    args = parser.parse_args()
    ensure_venus_source_hooks(args.ensure_venus_hooks)


if __name__ == '__main__':
    main()
