#!/usr/bin/env python3
"""Pack SQ2 C2 hardware encoder, patched venus-enc/DT, and 0x10303 allocator5 into a 6.18 initramfs."""
import argparse
import gzip
import hashlib
import importlib.util
from pathlib import Path, PurePosixPath
import subprocess

ROOT = Path(__file__).resolve().parents[1]
_pack_spec = importlib.util.spec_from_file_location('pack_ssam', ROOT / 'scripts/pack-sq2-lts-audio-ssam.py')
pack = importlib.util.module_from_spec(_pack_spec)
_pack_spec.loader.exec_module(pack)

_xml_spec = importlib.util.spec_from_file_location('prep_c2_xml', ROOT / 'scripts/prepare-sq2-c2-enc-xml.py')
prep_c2_xml = importlib.util.module_from_spec(_xml_spec)
_xml_spec.loader.exec_module(prep_c2_xml)

sha = lambda b: hashlib.sha256(b).hexdigest()

DEFAULT_C2_XML = (
    b'<?xml version="1.0" encoding="utf-8"?>\n'
    b'<MediaCodecs>\n'
    b'    <Encoders>\n'
    b'        <MediaCodec name="c2.sq2.avc.encoder" type="video/avc" rank="100">\n'
    b'            <Limit name="size" min="96x96" max="8192x8192" />\n'
    b'            <Limit name="alignment" value="2x2" />\n'
    b'            <Limit name="block-size" value="16x16" />\n'
    b'            <Limit name="frame-rate" range="1-480" />\n'
    b'            <Limit name="bitrate" range="32000-160000000" />\n'
    b'            <Limit name="concurrent-instances" max="16" />\n'
    b'            <Feature name="bitrate-modes" value="CBR" />\n'
    b'        </MediaCodec>\n'
    b'    </Encoders>\n'
    b'</MediaCodecs>\n'
)


def _rewrite_sums(raw_text, updates):
    lines = []
    seen = set()
    for line in raw_text.decode('utf-8').splitlines():
        parts = line.split(None, 1)
        if len(parts) == 2 and parts[1] in updates:
            lines.append(f'{sha(updates[parts[1]])}  {parts[1]}')
            seen.add(parts[1])
        elif line.strip():
            lines.append(line)
    for name in sorted(updates):
        if name not in seen:
            lines.append(f'{sha(updates[name])}  {name}')
    return ('\n'.join(lines) + '\n').encode('utf-8')


def pack_initramfs(base_bytes, hfi_overlay_ko, venus_enc_ko, c2_enc_service, allocator5_service):
    """Return updated initramfs.cpio.gz bytes preserving tail credentials and OMX decoders."""
    before = pack.cpio.entries(gzip.decompress(base_bytes))
    tail = pack.credential_tail(base_bytes)

    changes = {}

    def put(name, data, mode=0o100644):
        for parent in reversed(PurePosixPath(name).parents):
            d = str(parent)
            if d != '.' and d not in before and d not in changes:
                changes[d] = (0o040755, b'')
        changes[name] = (mode, data)

    # 1. Kernel Venus encoder & DT overlay modules
    put('lib/modules/6.18.60-sq2-android1/extra/sq2_hfi_overlay.ko', hfi_overlay_ko, 0o100644)
    put('lib/modules/6.18.60-sq2-android1/kernel/drivers/media/platform/qcom/venus/venus-enc.ko',
        venus_enc_ko, 0o100644)
    put('etc/sq2-video-native/sq2_hfi_overlay.ko', hfi_overlay_ko, 0o100644)
    put('etc/sq2-video-native/venus-enc.ko', venus_enc_ko, 0o100644)
    if 'etc/sq2-video-native/SHA256SUMS' in before:
        put('etc/sq2-video-native/SHA256SUMS', _rewrite_sums(
            before['etc/sq2-video-native/SHA256SUMS'][1],
            {'sq2_hfi_overlay.ko': hfi_overlay_ko, 'venus-enc.ko': venus_enc_ko},
        ), 0o100644)

    # 2. Updated allocator5 allowing 0x10303 SurfaceFlinger VIDEO_ENCODER RGBA allocation
    alloc_path = 'etc/sq2-qti-system/root/bin/hw/vendor.qti.hardware.display.allocator-service'
    assert alloc_path in before, f'Missing {alloc_path} in initramfs'
    put(alloc_path, allocator5_service, 0o100755)

    # 3. C2 hardware encoder binary + XML include (without touching OMX decoders)
    put('etc/sq2-c2-enc/sq2-c2-enc-service', c2_enc_service, 0o100755)
    put('etc/sq2-c2-enc/media_codecs_sq2_c2_enc.xml', DEFAULT_C2_XML, 0o100644)

    hamoa_path = 'etc/sq2-hidl/media_codecs_hamoa.xml'
    assert hamoa_path in before, f'Missing {hamoa_path} in initramfs'
    hamoa_updated = prep_c2_xml.configure_enc_codecs(before[hamoa_path][1])
    put(hamoa_path, hamoa_updated, 0o100644)
    if 'etc/sq2-hidl/SHA256SUMS' in before:
        put('etc/sq2-hidl/SHA256SUMS', _rewrite_sums(
            before['etc/sq2-hidl/SHA256SUMS'][1],
            {'media_codecs_hamoa.xml': hamoa_updated},
        ), 0o100644)

    # 4. Boot-time staging BEFORE Android init launches allocator / media.hwcodec / SurfaceFlinger
    hidl_stage_path = 'bin/quest2-hidl-stage'
    hidl_stage = before[hidl_stage_path][1].decode('utf-8')
    c2_anchor = 'echo QUEST2_HIDL_STAGED'
    c2_stage_block = (
        'cp /etc/sq2-c2-enc/sq2-c2-enc-service /newroot/vendor/bin/hw/vendor.qti.media.c2@1.0-service\n'
        'chmod 755 /newroot/vendor/bin/hw/vendor.qti.media.c2@1.0-service\n'
        'cp /etc/sq2-c2-enc/media_codecs_sq2_c2_enc.xml /newroot/vendor/etc/media_codecs_sq2_c2_enc.xml\n'
        'cat > /newroot/vendor/etc/init/sq2-c2-enc-labels.rc <<\'RC\'\n'
        'on post-fs\n'
        '    restorecon /vendor/bin/hw/vendor.qti.media.c2@1.0-service\n'
        '    restorecon /vendor/bin/hw/vendor.qti.hardware.display.allocator-service\n'
        '    restorecon /vendor/etc/media_codecs_sq2_c2_enc.xml\n'
        'RC\n'
        'echo QUEST2_HIDL_STAGED'
    )
    if '/etc/sq2-c2-enc/sq2-c2-enc-service' not in hidl_stage:
        assert hidl_stage.count(c2_anchor) == 1, 'Unexpected quest2-hidl-stage anchor'
        hidl_stage = hidl_stage.replace(c2_anchor, c2_stage_block, 1)
    put(hidl_stage_path, hidl_stage.encode('utf-8'), 0o100755)

    # 5. Kernel module startup: load venus-enc alongside venus-dec
    lab_path = 'bin/video-decoder-lab-start'
    lab_sh = before[lab_path][1].decode('utf-8')
    if 'venus_enc' not in lab_sh:
        lab_sh = lab_sh.replace(
            'for module in sq2_iris1_identify sq2_hfi_overlay venus_core venus_dec sq2_iris1_phase; do',
            'for module in sq2_iris1_identify sq2_hfi_overlay venus_core venus_dec venus_enc sq2_iris1_phase; do',
            1,
        )
        lab_sh = lab_sh.replace(
            'modprobe venus-dec\n',
            'modprobe venus-dec\nmodprobe venus-enc\n',
            1,
        )
    put(lab_path, lab_sh.encode('utf-8'), 0o100755)

    # 6. Framework boot: probe real encoder XML, restart C2 HAL, then restart media
    boot_path = 'bin/video-runtime-framework-boot'
    boot_sh = before[boot_path][1].decode('utf-8')
    if 'media_codecs_sq2_c2_enc.xml.next' not in boot_sh:
        anchor = 'setprop ctl.start sq2-omx\nANDROID'
        assert boot_sh.count(anchor) == 1, 'Unexpected video-runtime-framework-boot anchor'
        enc_cmds = (
            '/vendor/bin/hw/vendor.qti.media.c2@1.0-service --print-capabilities-xml > /vendor/etc/media_codecs_sq2_c2_enc.xml.next\n'
            'mv /vendor/etc/media_codecs_sq2_c2_enc.xml.next /vendor/etc/media_codecs_sq2_c2_enc.xml\n'
            'restorecon /vendor/etc/media_codecs_sq2_c2_enc.xml || true\n'
            'setprop ctl.restart vendor-qti-media-c2-hal-1-0\n'
            'setprop ctl.start sq2-omx\nANDROID'
        )
        boot_sh = boot_sh.replace(anchor, enc_cmds, 1)
        restart_anchor = 'timeout 5 nsenter -t 1 -m -r -- /system/bin/setprop ctl.restart media'
        if restart_anchor in boot_sh:
            wait_c2 = (
                'for n in $(seq 1 30); do\n'
                ' [ "$(timeout 3 nsenter -t 1 -m -r -- /system/bin/getprop init.svc.vendor-qti-media-c2-hal-1-0)" != running ] || break\n'
                ' sleep 1\n'
                'done\n'
                + restart_anchor
            )
            boot_sh = boot_sh.replace(restart_anchor, wait_c2, 1)
    put(boot_path, boot_sh.encode('utf-8'), 0o100755)

    for script_path in (hidl_stage_path, lab_path, boot_path):
        subprocess.run(['sh', '-n'], input=changes[script_path][1], check=True)

    raw = (
        b''.join(pack.cpio.entry(n, b, m) for n, (m, b) in changes.items())
        + pack.cpio.entry('TRAILER!!!', b'', 0)
    )
    raw += b'\0' * (-len(raw) % 512)
    result = base_bytes + gzip.compress(raw, mtime=0) + tail
    after = pack.cpio.entries(gzip.decompress(result))
    expected = {**before, **changes}
    assert after == expected, 'Packed initramfs verification mismatch'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', required=True, type=Path)
    parser.add_argument('--hfi-overlay', required=True, type=Path)
    parser.add_argument('--venus-enc', required=True, type=Path)
    parser.add_argument('--c2-enc-service', required=True, type=Path)
    parser.add_argument('--allocator5', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    out_bytes = pack_initramfs(
        args.base.read_bytes(),
        args.hfi_overlay.read_bytes(),
        args.venus_enc.read_bytes(),
        args.c2_enc_service.read_bytes(),
        args.allocator5.read_bytes(),
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(out_bytes)
    print(f'PACKED_BOOT_INITRAMFS={args.output} bytes={len(out_bytes)} sha256={sha(out_bytes)}')


if __name__ == '__main__':
    main()
