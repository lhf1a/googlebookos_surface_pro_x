"""Exercise the actual Annex-B copy helpers; no OMX/device simulation."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class CsdParts(unittest.TestCase):
    def test_initial_parameters_and_exact_au_parts(self):
        source=(ROOT/"probe/video/sq2-mediacodec-frames.c").read_text()
        methods=source[source.index("static size_t startcode("):source.index("static int save_rgb10_image(")]
        nals=[bytes([0,0,0,1,0x67,0xaa]),bytes([0,0,1,0x68,0xbb]),
              bytes([0,0,1,0x06,0xcc]),bytes([0,0,1,0x65,0xdd]),
              bytes([0,0,0,1,0x67,0xee]),bytes([0,0,1,0x68,0xff]),
              bytes([0,0,1,0x65,0x11]),bytes([0,0,1,0x41,0x22])]
        data=b"".join(nals);offsets=[0,sum(map(len,nals[:4])),sum(map(len,nals[:7])),len(data)]
        pre="""#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef struct {const void *sps,*pps;size_t sps_len,pps_len;} AMediaFormat;
static void AMediaFormat_setBuffer(AMediaFormat *f,const char *name,const void *data,size_t len){
 if(!strcmp(name,"csd-0")){f->sps=data;f->sps_len=len;}else{f->pps=data;f->pps_len=len;}
}
"""
        pre+="static const uint8_t sq2_vec_stream[]={"+",".join(map(str,data))+"};\n"
        pre+="static const unsigned sq2_vec_au[]={"+",".join(map(str,offsets))+"};\n"
        checks=["AMediaFormat f={0};assert(add_csd(&f));",
                f"assert(f.sps==sq2_vec_stream&&f.sps_len=={len(nals[0])});",
                f"assert(f.pps==sq2_vec_stream+{len(nals[0])}&&f.pps_len=={len(nals[1])});"]
        for index,items in enumerate([nals[:4],nals[4:7],nals[7:]]):
            for configuration in [0,1]:
                expected=b"".join(n for n in items if ((n[4] if n[3]==1 and n[:4]==bytes([0,0,0,1]) else n[3])&31 in [7,8])==bool(configuration))
                checks+=["{uint8_t output[128]={0};",
                         f"size_t n=copy_au_part(output,{index},{configuration});assert(n=={len(expected)});"]
                if expected:checks+=["const uint8_t expected[]={"+",".join(map(str,expected))+"};assert(!memcmp(output,expected,sizeof(expected)));"]
                checks+=["}"]
        code=pre+methods+"\nint main(void){"+ "".join(checks)+"return 0;}"
        with tempfile.TemporaryDirectory() as directory:
            d=Path(directory);(d/"check.c").write_text(code)
            p=subprocess.run(["gcc","-std=gnu11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",
                "-fno-pie","-no-pie",str(d/"check.c"),"-o",str(d/"check")],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stderr)
            p=subprocess.run([str(d/"check")],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)

    def test_hevc_csd_and_picture_parts(self):
        source=(ROOT/"probe/video/sq2-mediacodec-frames.c").read_text()
        methods=source[source.index("static size_t startcode("):source.index("static int save_rgb10_image(")]
        def nal(kind, payload, short=False):
            return (b"\x00\x00\x01" if short else b"\x00\x00\x00\x01")+bytes([kind<<1,1,payload])
        params=[nal(32,0xaa),nal(33,0xbb,True),nal(34,0xcc)]
        sei=nal(39,0xdd,True);picture=nal(19,0xee);next_pic=nal(1,0xff,True)
        cases=[("complete",params+[sei,picture],True),
               ("missing_vps",params[1:]+[sei,picture],False),
               ("missing_sps",[params[0],params[2],picture],False),
               ("missing_pps",params[:2]+[picture],False),
               ("truncated_pps",params[:2]+[b"\x00\x00\x01\x44"],False)]
        for name,first,valid in cases:
            with self.subTest(name=name),tempfile.TemporaryDirectory() as directory:
                # Parameters in the following AU must not repair missing first-AU CSD.
                second=params+[next_pic];data=b"".join(first+second)
                first_size=sum(map(len,first))
                code="""#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define SQ2_HEVC_VECTOR 1
typedef struct {uint8_t data[256];size_t size;unsigned calls;} AMediaFormat;
static void AMediaFormat_setBuffer(AMediaFormat *f,const char *name,const void *data,size_t len){
 assert(!strcmp(name,"csd-0"));assert(len<=sizeof(f->data));memcpy(f->data,data,len);f->size=len;++f->calls;
}
"""
                code+="static const uint8_t sq2_vec_stream[]={"+",".join(map(str,data))+"};\n"
                code+=f"static const unsigned sq2_vec_au[]={{0,{first_size},{len(data)}}};\n"
                code+=methods+"\nint main(void){AMediaFormat f={0};"
                code+=f"assert(add_csd(&f)=={int(valid)});assert(f.calls=={int(valid)});"
                if valid:
                    expected=b"".join(params)
                    code+="const uint8_t expected[]={"+",".join(map(str,expected))+"};"
                    code+="assert(f.size==sizeof(expected)&&!memcmp(f.data,expected,sizeof(expected)));"
                for index,items in enumerate([first,second]):
                    for config in (0,1):
                        expected=b"".join(n for n in items if (32<=((n[3] if n[2]==1 else n[4])>>1)&63<=34)==bool(config))
                        if name=="truncated_pps" and index==0:expected=b""
                        code+="{uint8_t output[256]={0};"
                        code+=f"assert(copy_au_part(output,{index},{config})=={len(expected)});"
                        if expected:
                            code+="const uint8_t expected[]={"+",".join(map(str,expected))+"};"
                            code+="assert(!memcmp(output,expected,sizeof(expected)));"
                        code+="}"
                code+="return 0;}"
                d=Path(directory);(d/"check.c").write_text(code)
                built=subprocess.run(["gcc","-std=gnu11","-Wall","-Wextra","-Werror",
                    "-fsanitize=address,undefined","-fno-pie","-no-pie",str(d/"check.c"),"-o",str(d/"check")],capture_output=True,text=True)
                self.assertEqual(built.returncode,0,built.stderr)
                checked=subprocess.run([str(d/"check")],capture_output=True,text=True)
                self.assertEqual(checked.returncode,0,checked.stdout+checked.stderr)
