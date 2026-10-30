/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
#define SQ2_CALLBACK_HOST_TEST
typedef unsigned int u32;
#define HFI_MSG_SESSION_EMPTY_BUFFER 1
#define HFI_MSG_SESSION_FILL_BUFFER 2
#define HFI_MSG_EVENT_NOTIFY 3
#define CONTROL_REPLY 4
struct waitqueue {pthread_mutex_t lock;pthread_cond_t cond;int entered;};
struct venus_core {pthread_mutex_t lock;struct waitqueue sys_err_done;bool sq2;};
struct venus_inst {struct venus_core *core;atomic_int sq2_callbacks;bool sq2_closing;};
#define IS_SQ2_HFI(c) ((c)->sq2)
#define WRITE_ONCE(p,v) ((p)=(v))
#define atomic_inc(p) ((void)atomic_fetch_add(p,1))
#define atomic_dec_and_test(p) (atomic_fetch_sub(p,1)==1)
#define atomic_read(p) atomic_load(p)
static void mutex_lock(pthread_mutex_t *p){assert(!pthread_mutex_lock(p));}
static void mutex_unlock(pthread_mutex_t *p){assert(!pthread_mutex_unlock(p));}
static void wake_up_all(struct waitqueue *q)
{mutex_lock(&q->lock);assert(!pthread_cond_broadcast(&q->cond));mutex_unlock(&q->lock);}
#define wait_event(q,condition) do { \
 mutex_lock(&(q).lock);(q).entered++;pthread_cond_broadcast(&(q).cond); \
 while(!(condition))assert(!pthread_cond_wait(&(q).cond,&(q).lock)); \
 mutex_unlock(&(q).lock); \
} while(0)
#include "../probe/video/venus-sq2-callback.h"
static atomic_bool finished;
static void *close_thread(void *p)
{sq2_callback_close_begin(p);atomic_store(&finished,true);return NULL;}
int main(void)
{
 for(int n=0;n<200;n++) {
  struct venus_core c={.sq2=true};struct venus_inst i={.core=&c};pthread_t t;
  assert(!pthread_mutex_init(&c.lock,NULL));
  assert(!pthread_mutex_init(&c.sys_err_done.lock,NULL));
  assert(!pthread_cond_init(&c.sys_err_done.cond,NULL));
  atomic_init(&i.sq2_callbacks,0);atomic_store(&finished,false);
  mutex_lock(&c.lock);assert(sq2_callback_get(&i,HFI_MSG_SESSION_FILL_BUFFER));mutex_unlock(&c.lock);
  assert(!pthread_create(&t,NULL,close_thread,&i));
  mutex_lock(&c.sys_err_done.lock);
  while(!c.sys_err_done.entered)assert(!pthread_cond_wait(&c.sys_err_done.cond,&c.sys_err_done.lock));
  mutex_unlock(&c.sys_err_done.lock);
  assert(!atomic_load(&finished));
  mutex_lock(&c.lock);
  assert(i.sq2_closing);
  assert(!sq2_callback_get(&i,HFI_MSG_SESSION_EMPTY_BUFFER));
  assert(!sq2_callback_get(&i,HFI_MSG_SESSION_FILL_BUFFER));
  assert(!sq2_callback_get(&i,HFI_MSG_EVENT_NOTIFY));
  assert(sq2_callback_get(&i,CONTROL_REPLY));mutex_unlock(&c.lock);
  sq2_callback_put(&i);assert(atomic_read(&i.sq2_callbacks)==1&&!atomic_load(&finished));
  sq2_callback_put(&i);assert(!pthread_join(t,NULL));assert(atomic_load(&finished));
  sq2_callback_drain(&i);assert(!atomic_read(&i.sq2_callbacks));
  c.sq2=false;assert(sq2_callback_get(&i,HFI_MSG_SESSION_FILL_BUFFER));
  sq2_callback_put(&i);assert(!atomic_read(&i.sq2_callbacks));
  assert(!pthread_mutex_destroy(&c.lock));assert(!pthread_mutex_destroy(&c.sys_err_done.lock));
  assert(!pthread_cond_destroy(&c.sys_err_done.cond));
 }
 puts("CALLBACK_PASS: 200 close/ref interleavings; close waits for active callback; new data/event refused, control replies allowed; legacy unchanged. Actual helper with mock waitqueue, not full IRQ/VB2 proof.");
 return 0;
}
