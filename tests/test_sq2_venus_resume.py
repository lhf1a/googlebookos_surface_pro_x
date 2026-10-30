"""Compile the generated HFI resume/deinit functions with mock power calls."""
from pathlib import Path
import subprocess
import sys
import tempfile

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
enum venus_state { VENUS_STATE_DEINIT=1, VENUS_STATE_INIT };
struct venus_hfi_device;
struct venus_core {bool sq2;unsigned long sys_error;struct venus_hfi_device *priv;};
struct venus_hfi_device {struct venus_core *core;pthread_mutex_t lock;
 enum venus_state state;bool suspended,power_enabled;};
#define IS_SQ2_HFI(c) ((c)->sq2)
static bool test_bit(int n,unsigned long *p){return (*p>>n)&1;}
static struct venus_hfi_device *to_hfi_priv(struct venus_core *c){return c->priv;}
static void mutex_lock(pthread_mutex_t *p){assert(!pthread_mutex_lock(p));}
static void mutex_unlock(pthread_mutex_t *p){assert(!pthread_mutex_unlock(p));}
static int calls;
static int venus_power_on(struct venus_hfi_device *h){calls++;h->power_enabled=true;return 0;}
/* FUNCTIONS */
int main(int argc,char **argv) {
 bool strict=argc>1;(void)argv;
 for(int sq2=0;sq2<2;sq2++)for(int state=0;state<3;state++)
 for(int error=0;error<2;error++)for(int suspended=0;suspended<2;suspended++) {
  struct venus_core core={.sq2=sq2,.sys_error=error};
  struct venus_hfi_device h={.core=&core,.state=state,.suspended=suspended};core.priv=&h;
  assert(!pthread_mutex_init(&h.lock,NULL));calls=0;
  int ret=venus_resume(&core);
  if(strict && sq2 && (state==VENUS_STATE_DEINIT || error))
   assert(ret==-EIO&&!calls&&h.suspended==suspended);
  else if(!sq2 || (state!=VENUS_STATE_DEINIT && !error))
   assert(!ret&&calls==suspended&&!h.suspended);
  assert(!pthread_mutex_destroy(&h.lock));
 }
 struct venus_core core={.sq2=true};struct venus_hfi_device h={.core=&core};core.priv=&h;
 assert(!pthread_mutex_init(&h.lock,NULL));calls=0;
 /* Initial zero state before SYS_INIT must still permit the first resume. */
 h.suspended=true;assert(!venus_resume(&core)&&calls==1);
 assert(!venus_core_deinit(&core));calls=0;
 int ret=venus_resume(&core);
 printf("SQ2 deinit then resume: ret=%d power_calls=%d suspended=%d state=%d\n",ret,calls,h.suspended,h.state);
 if(strict)assert(ret==-EIO&&!calls&&h.suspended&&!h.power_enabled);
 assert(!pthread_mutex_destroy(&h.lock));
 printf("24 state cases observed; strict=%d; mock power only\n",strict);
 return 0;
}
'''

def main():
    source = Path(sys.argv[1]).read_text()
    functions = []
    for start, end in (
        ('static void venus_set_state(', 'static void venus_dump_packet('),
        ('static int venus_core_deinit(', 'static int venus_core_ping('),
        ('static int venus_resume(', 'static int venus_suspend_1xx('),
    ):
        functions.append(source[source.index(start):source.index(end)])
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / 'resume.c'
        c.write_text(HARNESS.replace('/* FUNCTIONS */', '\n'.join(functions)))
        exe = Path(tmp) / 'resume'
        # Baseline lacks the new guard, so two mock/static helpers are unused.
        subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-pthread',
                        str(c), '-o', str(exe)], check=True)
        subprocess.run([str(exe)] + (['strict'] if '--strict' in sys.argv else []), check=True)

if __name__ == '__main__':
    main()
