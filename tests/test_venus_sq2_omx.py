"""Test OMX kernel adapter metadata and real-acknowledgement ownership order."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
#define V4L2_EVENT_SRC_CH_RESOLUTION 1
#define INST_START 5
#define HFI_BUFFERFLAG_CODECCONFIG 0x80U
#define HFI_FLUSH_ALL 4
#define HFI_FLUSH_INPUT 1
#define HFI_FLUSH_OUTPUT 2
#define VB2_BUF_STATE_DONE 1
#define IS_SQ2_HFI(core) ((core)->sq2)
struct v4l2_event {union {unsigned char data[64];}u;};
struct hfi_event_data {u32 sq2_sequence_fields,height,width,bit_depth,pic_struct,colour_space;};
struct vb2_buffer {int bytes;};
struct vb2_v4l2_buffer {struct vb2_buffer vb2_buf;u32 flags;};
struct core {int sq2;unsigned long sys_error;};
struct venus_inst {struct core *core;int lock,state,session_error;void *m2m_ctx;};
static int error,acknowledged,calls,returned,last_type,in_count,out_count,cleared;
static struct vb2_v4l2_buffer in_buf,out_buf;
static void mutex_lock(int *p){assert(!*p);*p=1;}
static void mutex_unlock(int *p){assert(*p);*p=0;}
static int test_bit(int n,unsigned long *p){return (*p>>n)&1;}
static int hfi_session_flush(struct venus_inst *i,u32 type,int block){
 assert(i->lock && block);calls++;last_type=type;
 if(error)return error;
 acknowledged=1;return 0;
}
static struct vb2_v4l2_buffer *v4l2_m2m_src_buf_remove(void *p){
 (void)p;assert(acknowledged);if(in_count){in_count--;return &in_buf;}return 0;
}
static struct vb2_v4l2_buffer *v4l2_m2m_dst_buf_remove(void *p){
 (void)p;assert(acknowledged);if(out_count){out_count--;return &out_buf;}return 0;
}
static void vb2_set_plane_payload(struct vb2_buffer *b,int plane,int bytes){assert(!plane);b->bytes=bytes;}
static void v4l2_m2m_buf_done(struct vb2_v4l2_buffer *b,int state){
 assert(acknowledged && !b->vb2_buf.bytes && !b->flags && state==VB2_BUF_STATE_DONE);returned++;
}
static void *v4l2_m2m_get_dst_vq(void *p){return p;}
static void vb2_clear_last_buffer_dequeued(void *p){(void)p;assert(acknowledged);cleared++;}
#define true 1
#include "HEADER"
#include "FLAGS_HEADER"
int main(void){
 assert(sq2_omx_input_flags(0x01000000U)==0x80U);
 assert(sq2_omx_input_flags(0x20000U)==0);
 assert(sq2_omx_input_flags(0)==0);
 struct v4l2_event event={0};
 struct hfi_event_data h={15,240,320,0x20002,1,9};
 sq2_omx_sequence(&event,&h);u32 p[8];memcpy(p,event.u.data,sizeof(p));
 assert(p[0]==1 && p[1]==0x51324d31 && p[2]==15 && p[3]==240 && p[4]==320 && p[5]==0x20002 && p[6]==1 && p[7]==9);
 h.sq2_sequence_fields=1;sq2_omx_sequence(&event,&h);memcpy(p,event.u.data,sizeof(p));assert(p[2]==1);
 struct core core={1,0};struct venus_inst i={.core=&core,.state=INST_START};
 in_count=out_count=1;error=-ETIMEDOUT;
 assert(sq2_omx_flush(&i,3)==-ETIMEDOUT && !acknowledged && !returned && !cleared && !i.lock);
 error=0;assert(!sq2_omx_flush(&i,3) && returned==2 && last_type==HFI_FLUSH_ALL);
 int before=calls;assert(sq2_omx_flush(&i,0)==-EINVAL && calls==before);
 core.sys_error=1;assert(sq2_omx_flush(&i,1)==-EIO && calls==before);
 core.sys_error=0;i.state=2;assert(sq2_omx_flush(&i,1)==-EIO && calls==before);
 i.state=INST_START;in_count=1;out_count=1;
 assert(!sq2_omx_flush(&i,1) && last_type==HFI_FLUSH_INPUT && !in_count && out_count==1);
 assert(!sq2_omx_flush(&i,2) && last_type==HFI_FLUSH_OUTPUT && !out_count);
 return 0;
}
"""

class VenusOmx(unittest.TestCase):
    def test_metadata_and_flush_ownership(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)
            (p/'test.c').write_text(HARNESS.replace('FLAGS_HEADER',str(ROOT/'probe/video/venus-sq2-omx-flags.h')).replace('HEADER',str(ROOT/'probe/video/venus-sq2-omx.h')))
            subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                            '-fno-pie','-no-pie',str(p/'test.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True,timeout=10)

if __name__=='__main__':
    unittest.main()
