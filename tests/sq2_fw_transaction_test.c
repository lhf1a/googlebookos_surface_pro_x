/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../probe/video/sq2-fw-transaction.h"
struct mock {
 int alloc_error, load_error, start_error, stop_error;
 int allocated, load_calls, start_calls, stop_calls, free_calls;
 int secure_touched, stopped;
};
static int alloc(void *ctx)
{
 struct mock *m = ctx;
 assert(!m->allocated);
 if (m->alloc_error) return m->alloc_error;
 m->allocated = 1;
 return 0;
}
static int load(void *ctx)
{
 struct mock *m = ctx;
 assert(m->allocated);
 m->load_calls++;
 m->secure_touched = 1;
 m->stopped = 0;
 return m->load_error;
}
static int start(void *ctx)
{
 struct mock *m = ctx;
 assert(m->allocated && m->secure_touched);
 m->start_calls++;
 return m->start_error;
}
static int stop(void *ctx)
{
 struct mock *m = ctx;
 assert(m->allocated && m->secure_touched);
 m->stop_calls++;
 if (!m->stop_error) m->stopped = 1;
 return m->stop_error;
}
static void release(void *ctx)
{
 struct mock *m = ctx;
 assert(m->allocated);
 /* Independent safety oracle: any secure touch requires confirmed shutdown. */
 assert(!m->secure_touched || m->stopped);
 m->allocated = 0;
 m->free_calls++;
}
static const struct sq2_fw_operations ops = {alloc, load, start, stop, release};
int main(void)
{
 unsigned int flags, i;
 for (flags = 0; flags < 16; flags++) {
  struct sq2_fw_transaction tx = {0};
  struct mock m = {0};
  int ret;
  m.alloc_error = flags & 1 ? -ENOMEM : 0;
  m.load_error = flags & 2 ? -ENOEXEC : 0;
  m.start_error = flags & 4 ? -EACCES : 0;
  m.stop_error = flags & 8 ? -ETIMEDOUT : 0;
  ret = sq2_fw_start(&tx, &ops, &m);
  if (m.alloc_error) {
   assert(ret == -ENOMEM && !m.load_calls && !m.stop_calls && !m.free_calls);
   assert(tx.state == SQ2_FW_EMPTY);
   continue;
  }
  if (m.load_error || m.start_error) {
   assert(ret == (m.load_error ? -ENOEXEC : -EACCES));
   assert(m.start_calls == (m.load_error ? 0 : 1));
  } else {
   assert(ret == 0 && tx.state == SQ2_FW_LIVE && m.allocated && !m.free_calls);
   assert(sq2_fw_start(&tx, &ops, &m) == -EBUSY);
  }
  ret = sq2_fw_stop(&tx, &ops, &m);
  if (m.stop_error) {
   assert(ret == -ETIMEDOUT && tx.state == SQ2_FW_QUARANTINED);
   assert(m.allocated && m.stop_calls == 1 && !m.free_calls);
   m.stop_error = 0; /* A later apparent recovery must not silently retry. */
   for (i = 0; i < 10; i++) {
    assert(sq2_fw_start(&tx, &ops, &m) == -EBUSY);
    assert(sq2_fw_stop(&tx, &ops, &m) == -ETIMEDOUT);
   }
   assert(m.stop_calls == 1 && !m.free_calls);
  } else {
   assert(ret == 0 && tx.state == SQ2_FW_EMPTY && !m.allocated);
   assert(m.stop_calls == 1 && m.free_calls == 1);
   assert(sq2_fw_stop(&tx, &ops, &m) == 0 && m.free_calls == 1);
  }
 }
 /* Repeated recovery only reuses storage after confirmed shutdown/release. */
 {
  struct sq2_fw_transaction tx = {0};
  struct mock m = {0};
  for (i = 0; i < 100; i++) {
   assert(!sq2_fw_start(&tx, &ops, &m));
   assert(!sq2_fw_stop(&tx, &ops, &m));
  }
  assert(m.free_calls == 100 && m.stop_calls == 100);
 }
 puts("SQ2_FW_TRANSACTION_PASS: all 16 fault combinations; quarantine blocks free/retry; 100 clean recoveries");
 return 0;
}
