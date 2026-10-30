import pathlib, subprocess, tempfile, json, sys, xml.etree.ElementTree as ET
root=pathlib.Path(__file__).resolve().parents[1]
header=pathlib.Path(sys.argv[1]) if len(sys.argv)>1 else root/'probe/video/sq2-codecs.h'
s=header.read_text()
with tempfile.TemporaryDirectory() as tmp:
 d=pathlib.Path(tmp);(d/'sq2-codecs.h').write_text(s)
 match='sq2CodecMatches(i,n)' if 'inline bool sq2CodecMatches' in s else '!std::strcmp(i.name,n)'
 names=[e.find('Alias').attrib['name'] for e in ET.parse(root/'configs/sq2-media-codecs-c2.xml').findall('./Decoders/MediaCodec')]
 code='#include <cstring>\n#include <cstdio>\n#include "sq2-codecs.h"\nint main(){ const char* aliases[]={' + ','.join(json.dumps(n) for n in names) + '}; int failures=0; for(unsigned k=0;k<4;k++){const auto& i=sq2Codecs[k]; const char* n=aliases[k]; bool ok='+match+'; printf("%s=%d\\n",n,ok); failures+=!ok; n=i.name; if(!('+match+')) failures++; for(unsigned j=0;j<4;j++){if(j==k)continue;n=aliases[j];if('+match+')failures++;} n="OMX.qcom.video.decoder.avc.secure";if('+match+')failures++;}return failures?1:0;}'
 (d/'test.cpp').write_text(code)
 subprocess.run(['aarch64-linux-gnu-g++','-std=c++17','-O2','-static',str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 sys.exit(subprocess.run(['qemu-aarch64',str(d/'test')]).returncode)
