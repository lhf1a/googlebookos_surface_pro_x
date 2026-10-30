"""Actual generated stop/flush functions, mocked synchronous HFI delivery."""
from pathlib import Path
import subprocess
import sys
import tempfile

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
typedef unsigned int u32;
#define fallthrough __attribute__((fallthrough))
#define READ_ONCE(x) (x)
#define IS_SQ2_HFI(c) ((c)->sq2)
#define V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE 1
#define V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE 2
#define VB2_BUF_STATE_ERROR 1
#define HFI_FLUSH_ALL 1
#define HFI_FLUSH_INPUT 2
#define HFI_FLUSH_OUTPUT 3
enum {VENUS_DEC_STATE_DEINIT,VENUS_DEC_STATE_INIT,VENUS_DEC_STATE_CAPTURE_SETUP,
 VENUS_DEC_STATE_DECODING,VENUS_DEC_STATE_DRAIN,VENUS_DEC_STATE_STOPPED,
 VENUS_DEC_STATE_SEEK,VENUS_DEC_STATE_DRC};
struct venus_inst;
struct hfi_ops {int (*session_flush)(struct venus_inst *,u32);};
struct inst_ops {void (*flush_done)(struct venus_inst *);};
struct venus_core {struct hfi_ops *ops;bool sq2;unsigned long sys_error;};
struct completion {bool done;};
struct venus_inst {struct venus_core *core;struct inst_ops *ops;int lock,codec_state,error;
 bool drain_active,session_error,sq2_closing,streamon_out,streamon_cap;struct completion done;};
struct vb2_queue {int type;struct venus_inst *inst;};
struct hfi_msg_session_flush_done_pkt {int error_type;};
static void complete(struct completion *p){p->done=true;}
static void reinit_completion(struct completion *p){p->done=false;}
static bool test_bit(int b,unsigned long *p){return (*p>>b)&1;}
static void mutex_lock(int *p){assert(!*p);*p=1;}
static void mutex_unlock(int *p){assert(*p);*p=0;}
static void *vb2_get_drv_priv(struct vb2_queue *q){return q->inst;}
static int wait_session_msg(struct venus_inst *i)
{return !i->done.done?-ETIMEDOUT:(i->error?-EIO:0);}
static int callbacks,sends,returned,cancelled,dpb_freed,mode;
static void flush_callback(struct venus_inst *i){(void)i;callbacks++;}
static void vdec_cancel_dst_buffers(struct venus_inst *i){assert(i->lock);cancelled++;}
static void venus_helper_free_dpb_bufs(struct venus_inst *i){assert(i->lock);dpb_freed++;}
static void vdec_pm_get_put(struct venus_inst *i){(void)i;}
static void venus_helper_buffers_done(struct venus_inst *i,int t,int s)
{assert(i->lock);(void)t;(void)s;returned++;}
/* FUNCTIONS */
static int send_flush(struct venus_inst *i,u32 type)
{
 assert(i->lock && type>=HFI_FLUSH_ALL && type<=HFI_FLUSH_OUTPUT);sends++;
 if(mode==3)return -EINVAL;
 if(mode==2)return 0; /* missing reply */
 struct hfi_msg_session_flush_done_pkt p={mode==1?7:0};
 hfi_session_flush_done(i->core,i,&p);return 0;
}
int main(int argc,char **argv)
{
 bool strict=argc>1;(void)argv;int cases=0;
 struct hfi_ops ops={send_flush};struct inst_ops iops={flush_callback};
 for(int sq2=0;sq2<2;sq2++)for(int closing=0;closing<2;closing++)
 for(int type=1;type<3;type++)for(int state=0;state<8;state++)
 for(mode=0;mode<5;mode++)for(int prior=0;prior<2;prior++) {
  struct venus_core c={.ops=&ops,.sq2=sq2,.sys_error=mode==4};
  struct venus_inst i={.core=&c,.ops=&iops,.codec_state=state,.session_error=prior,
   .sq2_closing=closing,.streamon_out=true,.streamon_cap=true};struct vb2_queue q={type,&i};
  callbacks=sends=returned=cancelled=dpb_freed=0;
  vdec_stop_streaming(&q);assert(!i.lock&&returned==1);
  if(sq2&&closing)assert(!callbacks); /* completion still delivered */
  if(sends&&(mode==0||mode==1))assert(i.done.done);
  bool failed=(sends&&mode!=0)||c.sys_error;
  if(strict&&sq2)assert(i.session_error==(prior||failed));
  if(!sq2)assert(!i.session_error);
  if(sq2&&closing&&type==1&&state==VENUS_DEC_STATE_DECODING&&mode==2&&!prior)
   printf("capture close timeout: session_error=%d replies=%d callbacks=%d returned=%d\n",i.session_error,i.done.done,callbacks,returned);
  cases++;
 }
 printf("STOP_SEQUENCE: %d cases; strict=%d; actual stop/capture/output/HFI flush/reply, mock delivery; no reconf wait in these functions\n",cases,strict);
 return 0;
}
'''

def main():
    directory = Path(sys.argv[1])
    vdec = (directory / 'vdec.c').read_text()
    hfi = (directory / 'hfi.c').read_text()
    msgs = (directory / 'hfi_msgs.c').read_text()
    functions = []
    for text, start, end in (
        (hfi, 'int hfi_session_flush(', 'EXPORT_SYMBOL_GPL(hfi_session_flush);'),
        (msgs, 'static void hfi_session_flush_done(', 'static void hfi_session_etb_done('),
        (vdec, 'static int vdec_stop_capture(', 'static void vdec_session_release('),
    ):
        functions.append(text[text.index(start):text.index(end)])
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / 'stop.c'; exe = Path(tmp) / 'stop'
        c.write_text(HARNESS.replace('/* FUNCTIONS */', '\n'.join(functions)))
        subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                        '-fno-pie', '-no-pie', str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)] + (['strict'] if '--strict' in sys.argv else []), check=True)

if __name__ == '__main__':
    main()
