#!/usr/bin/env python3
"""Compile real Venus close/release/stop/END bodies with instrumented OS edges."""
from pathlib import Path
import argparse, json, subprocess

def extract(s, name):
 start=s.index(name); start=s.rfind('\n',0,start)+1
 brace=s.index('{',start); depth=1; pos=brace+1
 while depth:
  if s[pos]=='{': depth+=1
  elif s[pos]=='}': depth-=1
  pos+=1
 return s[start:pos]+'\n'

PREFIX = r"""
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define INST_UNINIT 0
#define INST_INIT 1
#define HFI_ERR_NONE 0
#define TIMEOUT 100
#define VENUS_DEC_STATE_DEINIT 0
#define V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE 1
#define V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE 2
#define VB2_BUF_STATE_ERROR 1
struct venus_inst;
struct ops { int (*session_end)(struct venus_inst *); };
struct sq2_venus_scope { bool ended; };
struct venus_core { void *dev; int lock,sq2_retire_lock; unsigned long sys_error; struct ops *ops; };
struct venus_inst { struct venus_core *core; void *m2m_ctx,*m2m_dev; int dpb_ids,ctx_q_lock,lock,fh,codec_state,registeredbufs,buf_count,state,sq2_end_done,sq2_end_error; bool closing,session_error,streamon_out,streamon_cap,sq2_end_sent,sq2_end_confirmed; struct sq2_venus_scope *sq2_scope; };
struct file { struct venus_inst *inst; };
struct vb2_queue { struct venus_inst *inst; int type; };
struct vb2_buffer { struct vb2_queue *vb2_queue; int type; };
struct vb2_v4l2_buffer { int x; };
struct venus_buffer { int reg_list; };
static int get_calls,got,put_count,failed,power_fail_at,end_error,end_calls,hfi_calls,hfi_after_failure,retire_calls,destroyed,unresolved_at_destroy,sys_error_at_destroy,freed,streaming,allocated,done_calls,abort_calls,provider_calls,barrier_calls,barrier_under_lock,unbarriered_retire;
static struct venus_inst *to_inst(struct file *f){return f->inst;}
static struct venus_inst *vb2_get_drv_priv(struct vb2_queue *q){return q->inst;}
static struct vb2_v4l2_buffer *to_vb2_v4l2_buffer(struct vb2_buffer *v){return (void*)v;}
static struct venus_buffer *to_venus_buffer(struct vb2_v4l2_buffer *v){return (void*)v;}
#define IS_SQ2_HFI(c) ((c)!=NULL)
#define dev_err(d,fmt,...) do{(void)(d);fprintf(stderr,fmt,__VA_ARGS__);}while(0)
static void mutex_lock(int *x){++*x;}
static void mutex_unlock(int *x){--*x;}
static void mutex_destroy(int *x){(void)x;}
static void set_bit(int n,unsigned long *x){*x|=1UL<<n;}
static bool test_bit(int n,unsigned long *x){return !!(*x&(1UL<<n));}
static void INIT_LIST_HEAD(int *x){*x=0;}
static bool list_empty(int *x){return !*x;}
static void list_del_init(int *x){*x=0;}
static void sq2_callback_close_begin(struct venus_inst *i){barrier_calls++;if(i->lock)barrier_under_lock++;i->closing=true;}
static int vdec_pm_get(struct venus_inst *i){(void)i;get_calls++;if(power_fail_at&&get_calls>=power_fail_at){failed=1;return-EIO;}got++;return 0;}
static int vdec_pm_put(struct venus_inst *i,bool b){(void)i;(void)b;put_count++;return 0;}
static int vdec_pm_get_put(struct venus_inst *i){int ret=vdec_pm_get(i);if(ret>=0)vdec_pm_put(i,false);return ret;}
static int hfi_command(struct venus_inst *i){(void)i;hfi_calls++;if(failed)hfi_after_failure++;return 0;}
static int send_end(struct venus_inst *i){end_calls++;return hfi_command(i);}
static void reinit_completion(int *x){*x=0;}
static int wait_for_completion_timeout(int *x,int t){(void)t;if(end_error==-ETIMEDOUT)return 0;*x=1;return 1;}
static void sq2_venus_scope_end(struct venus_inst *i){if(i->sq2_scope)i->sq2_scope->ended=true;}
static int hfi_session_stop(struct venus_inst *i){return hfi_command(i);}
static int hfi_session_unload_res(struct venus_inst *i){return hfi_command(i);}
static int venus_helper_unregister_bufs(struct venus_inst *i){return hfi_command(i);}
static int venus_helper_intbufs_free(struct venus_inst *i){return hfi_command(i);}
static int hfi_session_abort(struct venus_inst *i){abort_calls++;return hfi_command(i);}
static void venus_helper_free_dpb_bufs(struct venus_inst *i){(void)i;}
static void venus_pm_load_scale(struct venus_inst *i){(void)i;provider_calls++;}
static void venus_pm_release_core(struct venus_inst *i){(void)i;provider_calls++;}
static void venus_helper_buffers_done(struct venus_inst *i,int t,int s){(void)i;(void)t;(void)s;done_calls++;}
static int vdec_stop_capture(struct venus_inst *i){return hfi_command(i);}
static int vdec_stop_output(struct venus_inst *i){return hfi_command(i);}
static void venus_helper_sq2_retire_buffers(struct venus_inst *i){retire_calls++;if(i->sq2_scope&&!i->sq2_scope->ended&&!i->closing)unbarriered_retire++;}
static void v4l2_m2m_ctx_release(void *x);
static void v4l2_m2m_release(void *x){(void)x;}
static void vdec_ctrl_deinit(struct venus_inst *i){(void)i;}
static void ida_destroy(int *x){(void)x;}
static void hfi_session_destroy(struct venus_inst *i){destroyed++;unresolved_at_destroy=i->sq2_scope&&!i->sq2_scope->ended;sys_error_at_destroy=i->core->sys_error;i->sq2_scope=NULL;}
static void v4l2_fh_del(int *x,struct file *f){(void)x;(void)f;}
static void v4l2_fh_exit(int *x){(void)x;}
static void kfree(void *x){(void)x;freed++;}
"""
SUFFIX = r"""
static void v4l2_m2m_ctx_release(void *x){
 struct venus_inst *i=x;struct vb2_queue q={.inst=i,.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE};struct vb2_buffer vb={.vb2_queue=&q,.type=q.type};
 if(streaming)vdec_stop_streaming(&q);
 if(allocated)vdec_buf_cleanup(&vb);
}
int main(int argc,char **argv){
 struct ops ops={send_end};struct venus_core c={.ops=&ops};struct sq2_venus_scope scope={0};
 struct venus_inst i={.core=&c,.state=INST_INIT,.buf_count=1,.sq2_scope=&scope};struct file f={&i};int ret;
 if(argc!=2)return 2;i.m2m_ctx=&i;allocated=1;
 if(!strcmp(argv[1],"never-init")){i.state=INST_UNINIT;i.sq2_scope=NULL;allocated=0;}
 else if(!strcmp(argv[1],"outer-pm-fail")){power_fail_at=1;streaming=1;}
 else if(!strcmp(argv[1],"inner-pm-fail")){power_fail_at=2;}
 else if(!strcmp(argv[1],"stop-pm-fail")){power_fail_at=2;streaming=1;}
 else if(!strcmp(argv[1],"end-timeout")){end_error=-ETIMEDOUT;}
 else if(!strcmp(argv[1],"init-timeout")){i.state=INST_UNINIT;}
 else if(!strcmp(argv[1],"core-error")){c.sys_error=1;}
 else if(!strcmp(argv[1],"streaming")){streaming=1;}
 else if(!strcmp(argv[1],"release-pm-fail")){power_fail_at=1;}
 else if(!strcmp(argv[1],"streamoff-pm-fail")){power_fail_at=1;}
 else if(strcmp(argv[1],"reqbufs"))return 2;
 if(!strcmp(argv[1],"release-pm-fail")) {vdec_session_release(&i);hfi_session_destroy(&i);ret=-EIO;}
 else if(!strcmp(argv[1],"streamoff-pm-fail")) {struct vb2_queue q={.inst=&i,.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE};vdec_stop_streaming(&q);hfi_session_destroy(&i);ret=-EIO;}
 else ret=vdec_close(&f);
 printf("{\"scenario\":\"%s\",\"ret\":%d,\"pm_get_calls\":%d,\"pm_acquired\":%d,\"pm_put\":%d,\"hfi_commands\":%d,\"hfi_after_pm_failure\":%d,\"end_sent\":%d,\"end_confirmed\":%d,\"unresolved_at_destroy\":%d,\"sys_error_at_destroy\":%d,\"retire\":%d,\"freed\":%d,\"abort\":%d,\"provider\":%d,\"buffers_done\":%d,\"barrier_calls\":%d,\"barrier_under_lock\":%d,\"unbarriered_retire\":%d}\n",argv[1],ret,get_calls,got,put_count,hfi_calls,hfi_after_failure,end_calls,i.sq2_end_confirmed,unresolved_at_destroy,sys_error_at_destroy,retire_calls,freed,abort_calls,provider_calls,done_calls,barrier_calls,barrier_under_lock,unbarriered_retire);
 return 0;
}
"""
SCENARIOS=['never-init','reqbufs','streaming','outer-pm-fail','inner-pm-fail','stop-pm-fail','end-timeout','init-timeout','core-error','release-pm-fail','streamoff-pm-fail']
def build(source, output):
 output.mkdir(parents=True,exist_ok=True)
 code=PREFIX
 if (source/'venus-sq2-close-guard.h').exists():code+=(source/'venus-sq2-close-guard.h').read_text()
 code+=(source/'venus-sq2-session-end.h').read_text()
 code+='\nstatic int hfi_session_deinit(struct venus_inst *i){return sq2_session_end(i);}\n'
 s=(source/'vdec.c').read_text()
 for name in ['static void vdec_stop_streaming(', 'static void vdec_session_release(', 'static void vdec_buf_cleanup(', 'static int vdec_close(']:code+=extract(s,name)
 code+=SUFFIX
 (output/'actual-close.c').write_text(code)
 cmd=['cc','-std=c11','-Wall','-Wextra','-Wno-misleading-indentation',str(output/'actual-close.c'),'-o',str(output/'actual-close')]
 p=subprocess.run(cmd,capture_output=True,text=True);(output/'cc.stdout').write_text(p.stdout);(output/'cc.stderr').write_text(p.stderr);(output/'cc.command').write_text(' '.join(cmd)+'\n');(output/'cc.exit').write_text(str(p.returncode)+'\n');p.check_returncode()
 results=[]
 for scenario in SCENARIOS:
  p=subprocess.run([str(output/'actual-close'),scenario],capture_output=True,text=True)
  (output/(scenario+'.stdout')).write_text(p.stdout);(output/(scenario+'.stderr')).write_text(p.stderr);(output/(scenario+'.exit')).write_text(str(p.returncode)+'\n');p.check_returncode();results.append(json.loads(p.stdout))
 (output/'results.json').write_text(json.dumps(results,indent=2)+'\n');return results
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path);p.add_argument('--assert-safe',action='store_true');a=p.parse_args();results=build(a.source,a.output)
 if a.assert_safe:
  for t in results:
   assert t['pm_acquired']==t['pm_put'],t
   assert t['hfi_after_pm_failure']==0,t
   assert t['barrier_under_lock']==0 and t['unbarriered_retire']==0,t
   assert not t['unresolved_at_destroy'] or t['sys_error_at_destroy'],t
   if t['scenario'] in ['never-init','reqbufs','streaming']:assert not t['sys_error_at_destroy'] and t['ret']==0,t
  assert results[1]['end_sent']==1 and results[1]['end_confirmed'],results[1]
  assert results[6]['end_sent']==1 and not results[6]['end_confirmed'],results[6]
  assert results[7]['end_sent']==0 and results[7]['sys_error_at_destroy'],results[7]
 print(json.dumps(results,indent=2))
