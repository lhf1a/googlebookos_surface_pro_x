"""Execute generated HFI deinit and END/load reply handlers with mock delivery."""
from pathlib import Path
import subprocess
import sys
import tempfile

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#define INST_UNINIT 0
#define INST_INIT 1
#define HFI_ERR_NONE 0
#define TIMEOUT 1000
struct completion {bool done;};
static void complete(struct completion *c){c->done=true;}
static void reinit_completion(struct completion *c){c->done=false;}
static int wait_for_completion_timeout(struct completion *c,int timeout)
{(void)timeout;return c->done;}
struct venus_inst;
struct hfi_ops {int (*session_end)(struct venus_inst *);};
struct venus_core {struct hfi_ops *ops;unsigned long sys_error;bool sq2;};
struct venus_inst {struct venus_core *core;int state,error,sq2_end_error;
 bool sq2_end_sent,sq2_end_confirmed,session_error;struct completion done,sq2_end_done;};
struct hfi_msg_session_end_done_pkt {int error_type;};
struct hfi_msg_session_load_resources_done_pkt {int error_type;};
#define IS_SQ2_HFI(c) ((c)->sq2)
static bool test_bit(int n,unsigned long *p){return (*p>>n)&1;}
static int wait_session_msg(struct venus_inst *i)
{return !wait_for_completion_timeout(&i->done,TIMEOUT)?-ETIMEDOUT:(i->error?-EIO:0);}
static void sq2_venus_scope_end(struct venus_inst *i){assert(i->sq2_end_confirmed);}
/* FUNCTIONS */
static int mode,sends;
static int send_end(struct venus_inst *i)
{
 struct hfi_msg_session_end_done_pkt end={mode==3?7:0};
 struct hfi_msg_session_load_resources_done_pkt other={0};
 sends++;
 if(mode==4)return -EINVAL;
 if(mode==1)hfi_session_load_res_done(i->core,i,&other);
 if(mode==2||mode==3)hfi_session_end_done(i->core,i,&end);
 return 0;
}
int main(int argc,char **argv)
{
 bool strict=argc>1;(void)argv;struct hfi_ops ops={send_end};
 for(mode=0;mode<5;mode++)for(int error=0;error<2;error++) {
  struct venus_core core={.ops=&ops,.sq2=true,.sys_error=error};
  struct venus_inst i={.core=&core,.state=INST_INIT};sends=0;
  int ret=hfi_session_deinit(&i);
  if(strict) {
   int expected=error?-EIO:(mode==4?-EINVAL:(mode==3?-EIO:(mode==2?0:-ETIMEDOUT)));
   assert(ret==expected);assert(i.sq2_end_confirmed==(!error&&mode==2));
   int before=sends;int repeat=hfi_session_deinit(&i);
   assert(sends==before);assert(repeat==(i.sq2_end_confirmed?0:-EIO));
  }
  if((mode==1&&!error)||(mode==0&&error))
   printf("END mode=%s ret=%d confirmed=%d sends=%d\n",error?"sys_error":"unrelated_reply",ret,i.sq2_end_confirmed,sends);
 }
 printf("10 END cases; strict=%d; generated functions, mock replies, not firmware proof\n",strict);
 return 0;
}
'''

def main():
    directory = Path(sys.argv[1])
    hfi = (directory / 'hfi.c').read_text()
    msgs = (directory / 'hfi_msgs.c').read_text()
    functions = []
    if 'return sq2_session_end(inst);' in hfi:
        functions.append((directory / 'venus-sq2-session-end.h').read_text())
    for text, begin, end in (
        (hfi, 'int hfi_session_deinit(', 'EXPORT_SYMBOL_GPL(hfi_session_deinit);'),
        (msgs, 'static void hfi_session_end_done(', 'static void hfi_session_abort_done('),
        (msgs, 'static void hfi_session_load_res_done(', 'static void hfi_session_flush_done('),
    ):
        functions.append(text[text.index(begin):text.index(end)])
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / 'end.c'
        c.write_text(HARNESS.replace('/* FUNCTIONS */', '\n'.join(functions)))
        exe = Path(tmp) / 'end'
        subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-unused-function',
                        '-fsanitize=address,undefined', '-fno-pie', '-no-pie', str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)] + (['strict'] if '--strict' in sys.argv else []), check=True)

if __name__ == '__main__':
    main()
