#!/usr/bin/env python3
"""Generate six MPEG-2 ES access units and an independent NV12 reference."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zlib

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--size',choices=('320x240','320x256'),default='320x240')
    a=ap.parse_args();a.out.mkdir(exist_ok=False)
    width,height=map(int,a.size.split('x')); ybytes=width*height; framebytes=ybytes*3//2
    clip=a.out/'clip.m2v';ref=a.out/'ref.nv12'
    def run(cmd): subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    run(['ffmpeg','-nostdin','-v','error','-f','lavfi','-i',f'testsrc2=size={a.size}:rate=30',
         '-frames:v','6','-c:v','mpeg2video','-pix_fmt','yuv420p','-threads','1',
         '-g','6','-bf','0','-q:v','3','-f','mpeg2video',str(clip)])
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_packets',
        '-show_streams','-of','json',str(clip)],text=True))
    stream=probe['streams'][0]
    assert stream['codec_name']=='mpeg2video' and stream['width']==width and stream['height']==height
    assert stream['pix_fmt']=='yuv420p'
    data=clip.read_bytes();bounds=[0]
    seq=data.index(b'\x00\x00\x01\xb3')+4
    header=int.from_bytes(data[seq:seq+3],'big')
    if (header>>12,header&4095)!=(width,height):raise ValueError('sequence dimensions')
    for packet in probe['packets']:
        pos,size=int(packet['pos']),int(packet['size'])
        if pos!=bounds[-1] or size<=0 or pos+size>len(data):raise ValueError('packet boundary')
        if data[pos:pos+size].count(b'\x00\x00\x01\x00')!=1:raise ValueError('picture count')
        bounds.append(pos+size)
    if len(bounds)!=7 or bounds[-1]!=len(data):raise ValueError('packet count/coverage')
    run(['ffmpeg','-nostdin','-v','error','-threads','1','-i',str(clip),
         '-pix_fmt','nv12','-f','rawvideo',str(ref)])
    raw=ref.read_bytes()
    if len(raw)!=framebytes*6:raise ValueError('reference frame count')
    crcs=[(zlib.crc32(raw[n*framebytes:n*framebytes+ybytes]),
           zlib.crc32(raw[n*framebytes+ybytes:(n+1)*framebytes])) for n in range(6)]
    lines=['/* Generated MPEG-2 ES packets, sequence header retained. */',
           f'#define SQ2_VEC_W {width}',f'#define SQ2_VEC_H {height}','#define SQ2_VEC_FRAMES 6',
           '#define SQ2_VEC_CODEC V4L2_PIX_FMT_MPEG2',
           f'#define SQ2_VEC_SHA256 "{hashlib.sha256(data).hexdigest()}"']
    for name,values in [('au',bounds),('crc_y',[c[0] for c in crcs]),('crc_uv',[c[1] for c in crcs])]:
        lines.append(f'static const u32 sq2_vec_{name}[] = {{'+','.join(str(v)+'U' for v in values)+'};')
    lines.append('static const u8 sq2_vec_stream[] = {')
    for i in range(0,len(data),16):lines.append(','.join(f'0x{x:02x}' for x in data[i:i+16])+',')
    lines.append('};');(a.out/'sq2-mpeg2-vector.h').write_text('\n'.join(lines)+'\n')
    (a.out/'vector.json').write_text(json.dumps(dict(codec='mpeg2',width=width,height=height,
        frames=6,profile=stream.get('profile'),access_units=bounds,
        stream_sha256=hashlib.sha256(data).hexdigest(),reference_sha256=hashlib.sha256(raw).hexdigest(),
        crcs=crcs),indent=2)+'\n')

if __name__=='__main__':main()
