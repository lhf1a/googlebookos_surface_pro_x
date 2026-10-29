#!/usr/bin/env python3
"""Generate a six-frame HEVC Main vector and independent NV12 CRC reference.

No B frames in this first native-session probe. This is not a 4K60 benchmark.
"""
import argparse
import hashlib
import json
import subprocess
import zlib
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--bit-depth',type=int,choices=(8,10),default=8)
    args = ap.parse_args()
    args.out.mkdir(exist_ok=False)
    ten=args.bit_depth==10
    sample_bytes=2 if ten else 1
    frame_bytes=115200*sample_bytes; y_bytes=76800*sample_bytes
    profile='main10' if ten else 'main'
    output_format='p010le' if ten else 'nv12'
    clip, ref = args.out / 'clip.hevc', args.out / ('ref.p010' if ten else 'ref.nv12')
    source='testsrc2=size=320x240:rate=60'
    if ten:
        source+=",format=yuv420p10le,geq=lum='mod(X*7+Y*3+N*19,1024)':cb='mod(X*5+Y*11+N*13,1024)':cr='mod(X*13+Y*7+N*17,1024)'"
    def run(cmd):
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    run(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'lavfi', '-i',
         source, '-frames:v', '6', '-c:v', 'libx265',
         '-pix_fmt', 'yuv420p10le' if ten else 'yuv420p', '-profile:v', profile, '-threads', '1',
         '-x265-params', 'aud=1:repeat-headers=1:bframes=0:keyint=6:pools=none:frame-threads=1',
         '-f', 'hevc', str(clip)])
    run(['ffmpeg', '-nostdin', '-v', 'error', '-threads', '1', '-i', str(clip),
         '-pix_fmt', output_format, '-f', 'rawvideo', str(ref)])
    data, raw = clip.read_bytes(), ref.read_bytes()
    info=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(clip)],text=True))['streams'][0]
    if info['profile']!=('Main 10' if ten else 'Main') or info['pix_fmt']!=('yuv420p10le' if ten else 'yuv420p'):
        raise ValueError('encoded profile/depth mismatch')
    if ten:
        samples=[int.from_bytes(raw[i:i+2],'little') for i in range(0,len(raw),2)]
        if any(v&63 for v in samples) or not any((v>>6)&3 for v in samples):
            raise ValueError('invalid P010 packing or missing 10-bit detail')
    starts = []
    i = 0
    while i + 4 < len(data):
        if data[i:i+3] == b'\0\0\1':
            start = i-1 if i and data[i-1] == 0 else i
            if ((data[i+3] >> 1) & 63) == 35:
                starts.append(start)
            i += 3
        else:
            i += 1
    if len(starts) != 6 or starts[0] != 0 or len(raw) != frame_bytes*6:
        raise ValueError('unexpected access units or decoded reference size')
    bounds = starts + [len(data)]
    crcs = [(zlib.crc32(raw[n*frame_bytes:n*frame_bytes+y_bytes]),
             zlib.crc32(raw[n*frame_bytes+y_bytes:(n+1)*frame_bytes])) for n in range(6)]
    lines = [f'/* Generated HEVC {profile}, six frames, no B frames. */',
             f'#define SQ2_VEC_BYTES_PER_SAMPLE {sample_bytes}',
             '#define SQ2_VEC_W 320', '#define SQ2_VEC_H 240', '#define SQ2_VEC_FRAMES 6',
             f'#define SQ2_VEC_SHA256 "{hashlib.sha256(data).hexdigest()}"']
    for name, values in [('au', bounds), ('crc_y', [c[0] for c in crcs]),
                         ('crc_uv', [c[1] for c in crcs])]:
        lines.append(f'static const u32 sq2_vec_{name}[] = {{' + ','.join(str(x)+'U' for x in values) + '};')
    lines.append('static const u8 sq2_vec_stream[] = {')
    for i in range(0, len(data), 16):
        lines.append(','.join(f'0x{x:02x}' for x in data[i:i+16]) + ',')
    lines.append('};')
    (args.out / 'sq2-hevc-vector.h').write_text('\n'.join(lines)+'\n')
    (args.out / 'vector.json').write_text(json.dumps(dict(codec='hevc', profile=info['profile'],bit_depth=args.bit_depth,reference_format=output_format,
        width=320, height=240, fps=60, frames=6, bframes=0, access_units=bounds,
        stream_sha256=hashlib.sha256(data).hexdigest(),
        reference_sha256=hashlib.sha256(raw).hexdigest(), crcs=crcs), indent=2)+'\n')


if __name__ == '__main__':
    main()
