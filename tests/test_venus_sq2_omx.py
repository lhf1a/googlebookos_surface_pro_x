"""Test OMX kernel adapter metadata and real-acknowledgement ownership order."""
from pathlib import Path
import subprocess
import importlib.util
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
typedef uint64_t u64;
#include <stdbool.h>
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
enum venus_dec_state {
 VENUS_DEC_STATE_STOPPED = 3,
 VENUS_DEC_STATE_DRAIN = 5,
 VENUS_DEC_STATE_DECODING = 6,
 VENUS_DEC_STATE_DRC = 7,
};
struct venus_ts_metadata {int used; u64 ts_ns;};
struct venus_inst {struct core *core;int lock,ctx_q_lock,state,session_error;u32 sq2_omx_flush_flags;int sq2_omx_flush_output_pending;void *m2m_ctx;unsigned int streamon_cap;enum venus_dec_state codec_state;int drain_active;struct venus_ts_metadata tss[4];};
static int inject_sequence,inject_session_error,inject_core_error,fail_call,inject_drc;
static int sq2_omx_flush_output(struct venus_inst *);
static int error,acknowledged,calls,returned,last_type,in_count,out_count,cleared,dpb_queued,dpb_error;
static struct vb2_v4l2_buffer in_buf,out_buf;
static void mutex_lock(int *p){assert(!*p);*p=1;}
static void mutex_unlock(int *p){assert(*p);*p=0;}
static int test_bit(int n,unsigned long *p){return (*p>>n)&1;}
static int venus_helper_queue_dpb_bufs(struct venus_inst *i){assert(i->lock);if(dpb_error)return dpb_error;dpb_queued++;return 0;}
static int hfi_session_flush(struct venus_inst *i,u32 type,int block){
 calls++;last_type=type;
 if(!block){assert(i->lock);return error;}
 assert(!i->lock && i->ctx_q_lock);
 if(inject_sequence){
  inject_sequence=0;
  mutex_lock(&i->lock);
  if(inject_drc){inject_drc=0;i->codec_state=VENUS_DEC_STATE_DRC;}
  assert(!sq2_omx_flush_output(i));
  mutex_unlock(&i->lock);
 }
 if(inject_session_error)i->session_error=1;
 if(inject_core_error)i->core->sys_error=1;
 if(error || calls==fail_call)return error?error:-EIO;
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
 // A sequence callback runs during the blocking wait instead of stalling IRQ.
 i.state=INST_START;int count=calls;inject_sequence=1;
 assert(!sq2_omx_flush(&i,2) && calls==count+1 && !i.ctx_q_lock);
 // Input-only flush still requires its own real output acknowledgement for DRC.
 count=calls;inject_sequence=1;
 assert(!sq2_omx_flush(&i,1) && calls==count+2 && last_type==HFI_FLUSH_OUTPUT);
 assert(!i.sq2_omx_flush_flags && !i.sq2_omx_flush_output_pending);
 // A failed extra DRC output reply cannot acknowledge input completion.
 count=calls;inject_sequence=1;fail_call=count+2;int n=returned;
 assert(sq2_omx_flush(&i,1)==-EIO && calls==count+2 && returned==n);
 fail_call=0;
 // Outside an OMX wait the original nonblocking DRC flush still runs.
 mutex_lock(&i.lock);count=calls;
 assert(!sq2_omx_flush_output(&i) && calls==count+1 && last_type==HFI_FLUSH_OUTPUT);
 mutex_unlock(&i.lock);
 // Errors arriving while unlocked must not return software-owned buffers or re-queue DPBs.
 in_count=out_count=1;int old_returned=returned;int old_dpb=dpb_queued;inject_session_error=1;
 assert(sq2_omx_flush(&i,3)==-EIO && returned==old_returned && dpb_queued==old_dpb);
 inject_session_error=0;i.session_error=0;inject_core_error=1;
 assert(sq2_omx_flush(&i,3)==-EIO && returned==old_returned && dpb_queued==old_dpb);
 inject_core_error=0;core.sys_error=0;inject_sequence=1;error=-ETIMEDOUT;count=calls;
 assert(sq2_omx_flush(&i,1)==-ETIMEDOUT && calls==count+1 && returned==old_returned && dpb_queued==old_dpb);
 error=0;
 // Active capture decoding re-queues flushed DPBs, clears tss, and resets DRAIN/STOPPED.
 i.streamon_cap=1;i.codec_state=VENUS_DEC_STATE_DRAIN;i.drain_active=1;i.tss[0].used=1;
 old_dpb=dpb_queued;
 assert(!sq2_omx_flush(&i,3) && dpb_queued==old_dpb+1 && i.codec_state==VENUS_DEC_STATE_DECODING && !i.drain_active && !i.tss[0].used);
 i.codec_state=VENUS_DEC_STATE_STOPPED;
 assert(!sq2_omx_flush(&i,3) && dpb_queued==old_dpb+2 && i.codec_state==VENUS_DEC_STATE_DECODING);
 // Input-only flush without DRC does not re-queue DPBs or clear tss.
 i.tss[0].used=1;
 assert(!sq2_omx_flush(&i,1) && dpb_queued==old_dpb+2 && i.tss[0].used);
 // Mid-DRC teardown must not re-queue old DPBs.
 inject_sequence=1;inject_drc=1;
 assert(!sq2_omx_flush(&i,1) && dpb_queued==old_dpb+2 && i.codec_state==VENUS_DEC_STATE_DRC);
 // Propagate DPB queue error while still releasing software-queued buffers.
 i.codec_state=VENUS_DEC_STATE_DECODING;in_count=out_count=1;old_returned=returned;dpb_error=-ENOMEM;
 assert(sq2_omx_flush(&i,3)==-ENOMEM && returned==old_returned+2);
 dpb_error=0;
 assert(!i.lock && !i.ctx_q_lock && !i.sq2_omx_flush_flags);
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

    def test_flush_lock_source_adapter(self):
        spec = importlib.util.spec_from_file_location('omx_patch', ROOT/'scripts/patch-sq2-venus-omx.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p/'core.h').write_text('struct venus_inst {\n\tstruct mutex ctx_q_lock;\n};\n')
            original = '\t\tret = hfi_session_flush(inst, HFI_FLUSH_OUTPUT, false);\n'
            (p/'vdec.c').write_text(original)
            patch.apply_flush_lock(p, ROOT)
            self.assertEqual((p/'core.h').read_text().count('u32 sq2_omx_flush_flags;'), 1)
            self.assertIn('IS_SQ2_HFI(inst->core) ? sq2_omx_flush_output(inst)', (p/'vdec.c').read_text())
            self.assertIn('hfi_session_flush(inst, HFI_FLUSH_OUTPUT, false)', (p/'vdec.c').read_text())
            self.assertEqual((p/'venus-sq2-omx.h').read_bytes(), (ROOT/'probe/video/venus-sq2-omx.h').read_bytes())
            before = {f.name:f.read_bytes() for f in p.iterdir()}
            with self.assertRaises(ValueError):
                patch.apply_flush_lock(p, ROOT)
            self.assertEqual(before, {f.name:f.read_bytes() for f in p.iterdir()})
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p/'core.h').write_text('struct venus_inst {\n\tstruct mutex ctx_q_lock;\n};\n')
            (p/'vdec.c').write_text('unknown decoder\n')
            before = (p/'core.h').read_bytes()
            with self.assertRaises(ValueError):
                patch.apply_flush_lock(p, ROOT)
            self.assertEqual(before, (p/'core.h').read_bytes())

if __name__=='__main__':
    unittest.main()
