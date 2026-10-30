"""Compile the actual vdec_buf_done with mocked delivery; preserve error cases."""
from pathlib import Path
import subprocess,sys,tempfile
src=(Path(sys.argv[1])/'vdec.c').read_text()
body=src[src.index('static void vdec_buf_done('):src.index('static void vdec_event_change(')]
fixed='!IS_SQ2_HFI(inst->core)' in body
if '--strict' in sys.argv: assert fixed
prefix=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <linux/videodev2.h>
typedef uint32_t u32;typedef uint64_t u64;
#define IS_SQ2_HFI(c) ((c)->sq2)
#define HFI_BUFFER_INPUT 1
#define HFI_BUFFER_OUTPUT 2
#define HFI_BUFFERFLAG_READONLY 1
#define HFI_BUFFERFLAG_DATACORRUPT 2
#define HFI_BUFFERFLAG_DROP_FRAME 4
#define NSEC_PER_USEC 1000
#define VENUS_DEC_STATE_DRAIN 1
#define VENUS_DEC_STATE_STOPPED 2
enum vb2_buffer_state {VB2_BUF_STATE_DONE,VB2_BUF_STATE_ERROR};
struct vb2_buffer {struct {unsigned data_offset;} planes[1];u64 timestamp;unsigned payload;};
struct vb2_v4l2_buffer {unsigned flags,field,sequence;struct vb2_buffer vb2_buf;};
struct core {bool sq2;};
struct venus_inst {struct core *core;unsigned sequence_cap,sequence_out;int fh,codec_state;bool drain_active;};
static struct vb2_v4l2_buffer buffer;
static int events,refs,done,missing,dpb;
static enum vb2_buffer_state state;
static void vdec_pm_touch(struct venus_inst *i){(void)i;}
static struct vb2_v4l2_buffer *venus_helper_find_buf(struct venus_inst *i,unsigned t,u32 tag){(void)i;(void)t;(void)tag;return missing?0:&buffer;}
static void venus_helper_change_dpb_owner(struct venus_inst *i,struct vb2_v4l2_buffer *v,unsigned t,unsigned b,u32 tag){(void)i;(void)v;(void)t;(void)b;(void)tag;dpb++;}
static void vb2_set_plane_payload(struct vb2_buffer *v,unsigned p,unsigned n){assert(p==0);v->payload=n;}
static void v4l2_event_queue_fh(int *fh,const struct v4l2_event *e){(void)fh;assert(e->type==V4L2_EVENT_EOS);events++;}
static void venus_helper_get_ts_metadata(struct venus_inst *i,u64 ts,struct vb2_v4l2_buffer *b){(void)i;(void)ts;(void)b;}
static void venus_helper_acquire_buf_ref(struct vb2_v4l2_buffer *b){(void)b;refs++;}
static void v4l2_m2m_buf_done(struct vb2_v4l2_buffer *b,enum vb2_buffer_state s){(void)b;done++;state=s;}
'''
suffix=r'''
int main(void){unsigned cases=0;
 for(unsigned sq=0;sq<2;sq++)for(unsigned input=0;input<2;input++)for(unsigned bytes=0;bytes<2;bytes++)for(unsigned last=0;last<2;last++)for(unsigned hf=0;hf<8;hf++){
 struct core c={.sq2=sq};struct venus_inst i={.core=&c,.codec_state=VENUS_DEC_STATE_DRAIN,.drain_active=true};
 buffer=(struct vb2_v4l2_buffer){0};events=refs=done=dpb=missing=0;
 vdec_buf_done(&i,input?HFI_BUFFER_INPUT:HFI_BUFFER_OUTPUT,7,bytes?42:0,0,last?V4L2_BUF_FLAG_LAST:0,hf,123);
 bool expected=(!input&&!bytes&&!(FIXED&&sq&&last))||(hf&(HFI_BUFFERFLAG_DATACORRUPT|HFI_BUFFERFLAG_DROP_FRAME));
 assert(done==1 && state==(expected?VB2_BUF_STATE_ERROR:VB2_BUF_STATE_DONE));assert(events==(!input&&last));assert(refs==!!(hf&HFI_BUFFERFLAG_READONLY));
 if(!input&&last)assert(!i.drain_active&&i.codec_state==VENUS_DEC_STATE_STOPPED);
 if(hf&HFI_BUFFERFLAG_DROP_FRAME)assert(buffer.vb2_buf.payload==0&&buffer.vb2_buf.timestamp==0);
 if(sq&&!input&&!bytes&&last&&!hf)printf("EMPTY_LAST state=%s eos_events=%d stopped=%d\n",state==VB2_BUF_STATE_DONE?"DONE":"ERROR",events,i.codec_state==VENUS_DEC_STATE_STOPPED);
 cases++;
 }
 struct core c={.sq2=true};struct venus_inst i={.core=&c};missing=1;dpb=done=0;vdec_buf_done(&i,HFI_BUFFER_OUTPUT,7,0,0,V4L2_BUF_FLAG_LAST,0,0);assert(dpb==1&&!done);
 printf("BUF_DONE_PASS cases=%u missing-owner=1 corruption/drop preserved\n",cases);return 0;}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(prefix+'\n#define FIXED '+str(int(fixed))+'\n'+body+suffix)
 subprocess.run(['gcc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
