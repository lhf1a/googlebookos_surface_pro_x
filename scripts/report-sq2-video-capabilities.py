#!/usr/bin/env python3
"""Verify private H264 frame evidence and separate firmware/Android capabilities.

Offline only. Does not register codecs or promote declared limits to tested ones.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

MIMES = {'H264': 'video/avc', 'HEVC': 'video/hevc', 'MPEG2': 'video/mpeg2',
         'VP8': 'video/x-vnd.on2.vp8', 'VP9': 'video/x-vnd.on2.vp9'}


def verify_frames(log, vector):
    matches = list(re.finditer(
        r'SQ2_HFI_DEC_FRAME frame=(\d+) crc_y=([0-9a-f]{8})/([0-9a-f]{8}) '
        r'crc_uv=([0-9a-f]{8})/([0-9a-f]{8}).*? w=(\d+) h=(\d+)', log))
    found = {}
    for m in matches:
        frame = int(m[1])
        if frame in found or frame >= vector['frames']:
            raise ValueError('duplicate/out-of-range frame')
        expected = vector['frames_detail'][frame]
        if (m[2] != expected['crc_y'] or m[3] != expected['crc_y'] or
                m[4] != expected['crc_uv'] or m[5] != expected['crc_uv'] or
                int(m[6]) != vector['width'] or int(m[7]) != vector['height']):
            raise ValueError('frame CRC/dimensions disagree with independent reference')
        found[frame] = {'index': frame, 'crc_y': m[2], 'crc_uv': m[4]}
    if sorted(found) != list(range(vector['frames'])):
        raise ValueError('missing frame evidence')
    result = re.search(r'SQ2_HFI_DEC_RESULT ret=0 verified=(\d+)/(\d+) mismatched=0 '
                       r'ebd=\d+ ebd_err=0 .*?fbd_err=0 eos=1', log)
    if not result or tuple(map(int, result.groups())) != (vector['frames'], vector['frames']):
        raise ValueError('decode summary absent or inconsistent')
    for marker in ['SQ2_HFI_DEC_TEARDOWN ret=0 session_open=0',
                   'phase=firmware_shutdown_end ret=0 owner=0',
                   'SQ2_HFI_DMA freed first_iova_unmapped=1 ret=0',
                   'phase=post_reset_end ret=0 hfi_ret=0',
                   'resources_released ret=0', 'KMSG_END reason=signal']:
        if marker not in log:
            raise ValueError('missing shutdown/evidence marker: '+marker)
    return [found[i] for i in sorted(found)]


def android_decoders(text):
    result = {}
    parts = re.split(r"\nMedia type '([^']+)':\n", '\n'+text)
    for i in range(1, len(parts), 2):
        mime, block = parts[i:i+2]
        for match in re.finditer(r'  Decoder "([^"]+)" supports(.*?)(?=\n  Decoder|\Z)', block, re.S):
            hardware = re.search(r'hw-accelerated:\s*([01])', match[2])
            if hardware:
                result.setdefault(mime, []).append({'name': match[1],
                    'hardware_accelerated': hardware[1] == '1'})
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--caps', type=Path, required=True)
    ap.add_argument('--kmsg', type=Path, required=True)
    ap.add_argument('--vector', type=Path, required=True)
    ap.add_argument('--android', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    caps = json.loads(a.caps.read_text())
    if not (caps['complete'] and caps['error'] == 0 and
            caps['num_properties'] == caps['records'] and
            caps['body_words'] == caps['consumed_body_words']):
        raise ValueError('capability parse incomplete/inconsistent')
    vector = json.loads(a.vector.read_text())
    log = a.kmsg.read_text()
    if f"vector={vector['stream_sha256']} " not in log:
        raise ValueError('test vector identity mismatch')
    frames = verify_frames(log, vector)
    android = android_decoders(a.android.read_text(encoding='utf-8-sig'))
    codec = next(r for r in caps['all_records'] if r['property'] == 'codec_supported')
    rows = []
    for name in codec['decoders']:
        declared = []
        for group in caps['groups']:
            if name in group['codecs'] and 'decoder' in group['domains']:
                declared.extend(group.get('data', []))
        entries = android.get(MIMES.get(name), [])
        rows.append({'codec': name, 'mime': MIMES.get(name),
            'firmware_declared': declared,
            'pixel_verification': {'status': 'verified-private-HFI' if name == 'H264' else 'not-tested',
                'scope': 'Constrained Baseline level1.3, 320x240, 6 I/P frames, linear NV12 OUTPUT2'
                         if name == 'H264' else None},
            'android_components': entries,
            'android_hardware_component_present': any(e['hardware_accelerated'] for e in entries),
            'alignment': 'API/backend missing; not resolved by XML declarations'})
    report = {'status': 'private-H264-pixels-verified-Android-hardware-not-integrated',
        'sources_sha256': {name: hashlib.sha256(path.read_bytes()).hexdigest() for name,path in
            [('caps',a.caps),('kmsg',a.kmsg),('vector',a.vector),('android',a.android)]},
        'verified_frames': frames, 'decoder_alignment': rows,
        'encoder_declarations_outside_decode_goal': codec.get('encoders', []),
        'software_only_not_in_firmware_declaration': [mime for mime in android
            if mime.startswith('video/') and mime not in [MIMES.get(n) for n in codec['decoders']]],
        'limits': ['firmware width/height/frame-rate maxima are joint constraints, not every combination',
                   'no 4K/8K, HEVC, VP8/VP9, MPEG1/2, 10-bit or secure decode proof',
                   'no V4L2, Android hardware MediaCodec, application playback or performance proof',
                   'same-boot firmware restart remains unresolved; one firmware start per OS boot',
                   'do not advertise AV1 hardware or untested limits by editing XML']}
    with a.output.open('x') as file:
        json.dump(report, file, indent=2)
        file.write('\n')
    print(f"VIDEO_CAPABILITY_ALIGNMENT_OK verified_frames={len(frames)} decoder_rows={len(rows)}")
    for row in rows:
        print(row['codec'], row['pixel_verification']['status'],
              'AndroidHW='+str(row['android_hardware_component_present']))


if __name__ == '__main__':
    main()
