"""Compile actual staged selection helpers against recorded firmware formats."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def function(source,name):
    match=re.search(r'(?:static )?(?:u32|bool|int)\s+'+name+r'\s*\(',source)
    assert match,name
    begin=source.index('{',match.start());depth=1;end=begin+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[match.start():end]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('stage',type=Path);a=ap.parse_args()
    helpers=(a.stage/'helpers.c').read_text();header=(a.stage/'hfi_helper.h').read_text()
    vdec=(a.stage/'vdec.c').read_text()
    formats=re.findall(r'\.pixfmt = (V4L2_PIX_FMT_\w+)',vdec.split('vdec_formats[] = {',1)[1].split('\n};',1)[0])
    assert formats[0]=='V4L2_PIX_FMT_NV12' and formats[8]=='V4L2_PIX_FMT_H264'
    assert formats[-1]=='V4L2_PIX_FMT_P010'
    constants='\n'.join(line for line in header.splitlines() if re.match(r'#define HFI_COLOR_FORMAT_\w+\s+0x[0-9a-fA-F]+\s*$',line))
    prelude='''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <linux/videodev2.h>
typedef uint32_t u32;
#define HFI_BUFFER_OUTPUT 2
#define HFI_BUFFER_OUTPUT2 3
#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))
#define SZ_4K 4096
struct hfi_plat_caps {unsigned num_fmts;struct {u32 buftype,fmt;} fmts[32];};
struct venus_core {bool sq2;struct hfi_plat_caps caps;};
struct venus_inst {struct venus_core *core;u32 hfi_codec,session_type;};
#define IS_SQ2_HFI(c) ((c)->sq2)
static struct hfi_plat_caps *venus_caps_by_codec(struct venus_core *c,u32 codec,u32 type){(void)codec;(void)type;return &c->caps;}
'''
    bodies='\n'.join(function(helpers,n) for n in ('to_hfi_raw_fmt','find_fmt_from_caps','venus_helper_get_out_fmts','get_framesize_raw_p010'))
    cases=[]
    for decoder in json.loads((ROOT/'configs/sq2-firmware-decode-capabilities.json').read_text())['decoders']:
        init=[]
        for buffer,items in decoder['output_formats'].items():
            for item in items:
                init.append('{HFI_BUFFER_'+buffer.upper()+',HFI_COLOR_FORMAT_'+item['format']+'}')
        if not init:continue
        supported=decoder['codec'] in ('HEVC','VP9')
        cases.append('''{
 struct venus_core core={.sq2=true,.caps={.num_fmts=%d,.fmts={%s}}};
 struct venus_inst inst={.core=&core};u32 out=123,out2=456;
 int ret=venus_helper_get_out_fmts(&inst,V4L2_PIX_FMT_P010,&out,&out2,true);
 assert(ret==%s);
 %s
 /* Missing either DPB or display format must refuse, never fall back to 8bit. */
 for(unsigned i=0;i<core.caps.num_fmts;i++){
  u32 saved=core.caps.fmts[i].fmt;
  if(saved!=HFI_COLOR_FORMAT_P010 && saved!=HFI_COLOR_FORMAT_YUV420_TP10_UBWC)continue;
  core.caps.fmts[i].fmt=0;
  if(core.caps.fmts[i].buftype==HFI_BUFFER_OUTPUT && saved==HFI_COLOR_FORMAT_YUV420_TP10_UBWC ||
     core.caps.fmts[i].buftype==HFI_BUFFER_OUTPUT2 && saved==HFI_COLOR_FORMAT_P010)
   assert(venus_helper_get_out_fmts(&inst,V4L2_PIX_FMT_P010,&out,&out2,true)==-EINVAL && !out && !out2);
  core.caps.fmts[i].fmt=saved;
 }
 assert(!venus_helper_get_out_fmts(&inst,V4L2_PIX_FMT_NV12,&out,&out2,true));
 assert(out==HFI_COLOR_FORMAT_NV12_UBWC && out2==HFI_COLOR_FORMAT_NV12);
 printf("FORMAT_SELECTION_PASS %s\\n");
}'''%(len(init),','.join(init),'0' if supported else '-EINVAL',
       'assert(out==HFI_COLOR_FORMAT_YUV420_TP10_UBWC && out2==HFI_COLOR_FORMAT_P010);' if supported else 'assert(!out && !out2);',decoder['codec']))
    code=prelude+constants+'\n'+bodies+'\nint main(void){\n'+'\n'.join(cases)+'''
 assert(get_framesize_raw_p010(320,240)==294912);
 assert(get_framesize_raw_p010(3840,2160)==25067520);
 puts("P010_LAYOUT_DEFAULT_FORMATS_PASS");return 0;}
'''
    with tempfile.TemporaryDirectory() as temp:
        p=Path(temp);(p/'test.c').write_text(code)
        subprocess.run(['gcc','-Wall','-Wextra','-Wno-parentheses','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',str(p/'test.c'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)

if __name__=='__main__':main()
