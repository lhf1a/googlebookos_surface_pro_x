#!/usr/bin/env python3
"""Six independently referenced VP8/VP9 frames; strip IVF container for V4L2."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--codec',choices=['vp8','vp9'],required=True)
    ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args();a.out.mkdir(exist_ok=False)
    clip=a.out/'clip.ivf';ref=a.out/'ref.nv12'
    def run(cmd):subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    run(['ffmpeg','-nostdin','-v','error','-f','lavfi','-i','testsrc2=size=320x240:rate=30',
         '-frames:v','6','-c:v','libvpx-vp9' if a.codec=='vp9' else 'libvpx',
         '-pix_fmt','yuv420p','-threads','1','-g','6','-lag-in-frames','0',
         '-auto-alt-ref','0','-b:v','500k','-f','ivf',str(clip)])
    run(['ffmpeg','-nostdin','-v','error','-threads','1','-i',str(clip),
         '-pix_fmt','nv12','-f','rawvideo',str(ref)])
    container=clip.read_bytes();raw=ref.read_bytes()
    if container[:4]!=b'DKIF' or struct.unpack_from('<HH',container,4)!=(0,32):raise ValueError('IVF header')
    expected=b'VP90' if a.codec=='vp9' else b'VP80'
    if container[8:12]!=expected or struct.unpack_from('<HH',container,12)!=(320,240):raise ValueError('IVF format')
    data=bytearray();bounds=[0];at=32;timestamps=[]
    while at<len(container):
        if len(container)-at<12:raise ValueError('short packet header')
        size,pts=struct.unpack_from('<IQ',container,at);at+=12
        if not size or size>len(container)-at:raise ValueError('short packet')
        data.extend(container[at:at+size]);at+=size;bounds.append(len(data));timestamps.append(pts)
    if len(bounds)!=7 or timestamps!=list(range(6)) or len(raw)!=115200*6:raise ValueError('frame count/order')
    crcs=[(zlib.crc32(raw[n*115200:n*115200+76800]),zlib.crc32(raw[n*115200+76800:(n+1)*115200])) for n in range(6)]
    lines=['/* Generated raw VPx frame packets, no IVF headers. */','#define SQ2_VEC_W 320',
           '#define SQ2_VEC_H 240','#define SQ2_VEC_FRAMES 6',
           f'#define SQ2_VEC_CODEC V4L2_PIX_FMT_{a.codec.upper()}',
           f'#define SQ2_VEC_SHA256 "{hashlib.sha256(data).hexdigest()}"']
    for name,values in [('au',bounds),('crc_y',[c[0] for c in crcs]),('crc_uv',[c[1] for c in crcs])]:
        lines.append(f'static const u32 sq2_vec_{name}[] = {{'+','.join(str(x)+'U' for x in values)+'};')
    lines.append('static const u8 sq2_vec_stream[] = {')
    for i in range(0,len(data),16):lines.append(','.join(f'0x{x:02x}' for x in data[i:i+16])+',')
    lines.append('};');(a.out/'sq2-vpx-vector.h').write_text('\n'.join(lines)+'\n')
    (a.out/'vector.json').write_text(json.dumps(dict(codec=a.codec,width=320,height=240,frames=6,
        access_units=bounds,stream_sha256=hashlib.sha256(data).hexdigest(),
        reference_sha256=hashlib.sha256(raw).hexdigest(),crcs=crcs),indent=2)+'\n')

if __name__=='__main__':main()
