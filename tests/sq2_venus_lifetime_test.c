/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include "../probe/video/sq2-venus-lifetime.h"
int main(void)
{
 int errors[]={-EIO,-ETIMEDOUT,-EACCES,-EINVAL};
 unsigned int i;
 for(i=0;i<sizeof(errors)/sizeof(errors[0]);i++) {
  struct sq2_venus_lifetime s={0};
  assert(sq2_venus_lifetime_releasable(&s));
  assert(sq2_venus_lifetime_end(&s,0)==-EINVAL);
  assert(!sq2_venus_lifetime_begin(&s));
  assert(!sq2_venus_lifetime_releasable(&s));
  assert(sq2_venus_lifetime_begin(&s)==-EBUSY);
  assert(sq2_venus_lifetime_end(&s,errors[i])==errors[i]);
  assert(!sq2_venus_lifetime_releasable(&s));
  assert(sq2_venus_lifetime_end(&s,0)==errors[i]);
  assert(sq2_venus_lifetime_begin(&s)==-EBUSY);
 }
 for(i=0;i<100;i++) {
  struct sq2_venus_lifetime s={0};
  assert(!sq2_venus_lifetime_begin(&s));
  assert(!sq2_venus_lifetime_end(&s,0));
  assert(sq2_venus_lifetime_releasable(&s));
 }
 puts("SQ2_PLATFORM_LIFETIME_PASS: 4 sticky shutdown failures refuse release/restart; 100 clean lifetimes; not kernel-resource or hardware proof");
 return 0;
}
