/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
struct sq2_venus_fw { int dummy; };
struct sq2_venus_platform { bool touched, quarantined; int error; };
static struct sq2_venus_platform platform;
static int sq2_venus_platform_secure_begin(struct sq2_venus_platform *p)
{ if (p->touched || p->quarantined) return -EBUSY; p->touched=true; return 0; }
static int sq2_venus_platform_secure_end(struct sq2_venus_platform *p,int ret)
{ if (p->quarantined) return p->error; if (ret) {p->quarantined=true;p->error=ret;} else p->touched=false;return ret; }
struct venus_resources { unsigned int cp_start,cp_size,cp_nonpixel_start,cp_nonpixel_size; };
struct venus_core {
 const struct venus_resources *res;
 bool use_tz,iris1,v4,sq2_boot_attempted,sq2_secure_active;
 struct sq2_venus_platform *sq2_platform;
 void *dev;
 struct sq2_venus_fw *sq2_fw_owner;
};
#define IS_IRIS1(c) ((c)->iris1)
#define IS_V4(c) ((c)->v4)
static int boots,stops,clocks,protects,boot_error,stop_error,protect_error;
static bool quarantine;
static struct sq2_venus_fw owner;
static void venus_iris1_clock_config(struct venus_core *c) { (void)c; clocks++; }
static int sq2_venus_fw_boot(void *d,struct sq2_venus_fw **p,bool gap)
{ (void)d; assert(gap); boots++; if (!boot_error || quarantine) *p=&owner; return boot_error; }
static int sq2_venus_fw_shutdown(struct sq2_venus_fw **p)
{ if (!*p) return 0; stops++; if (!stop_error) *p=NULL; return stop_error; }
static int qcom_scm_mem_protect_video_var(unsigned int a,unsigned int b,unsigned int c,unsigned int d)
{ assert(a==0 && b==0x25800000 && c==0x1000000 && d==0x24800000); protects++; return protect_error; }
#define dev_err(d,fmt,...) ((void)(d))
#include "../probe/video/venus-sq2-firmware.h"
int main(void)
{
 const struct venus_resources r={0,0x25800000,0x1000000,0x24800000};
 int b,p,s;
 for(b=0;b<2;b++) for(p=0;p<2;p++) for(s=0;s<2;s++) {
  struct venus_core c={.res=&r,.use_tz=true,.iris1=true,.v4=true,.sq2_platform=&platform};
  platform=(struct sq2_venus_platform){0};
  boots=stops=clocks=protects=0;
  boot_error=b ? -ENOEXEC : 0; protect_error=p ? -EACCES : 0;
  stop_error=s ? -EIO : 0; quarantine=b && s;
  assert(venus_sq2_boot(&c)==(b ? -ENOEXEC : (p ? -EACCES : 0)));
  assert(boots==1 && clocks==1 && protects==!b);
  assert(stops==((b && s) || (!b && p)));
  assert(venus_sq2_boot(&c)==-EBUSY && boots==1);
  if (s && (b || p)) assert(c.sq2_fw_owner==&owner);
  {
   int expected = c.sq2_fw_owner && s ? -EIO : 0;
   assert(venus_sq2_shutdown(&c)==expected);
  }
  if (!s) assert(!c.sq2_fw_owner);
  assert(platform.quarantined == (bool)s);
  assert(c.sq2_secure_active == (bool)s);
  /* Even a successful shutdown must not re-arm this core's firmware boot. */
  assert(venus_sq2_boot(&c)==-EBUSY && boots==1);
 }
 {
  struct venus_core c={.res=&r,.use_tz=false,.iris1=true,.v4=true};
  assert(venus_sq2_boot(&c)==-EINVAL && !c.sq2_boot_attempted);
  c.use_tz=true;c.iris1=false;
  assert(venus_sq2_boot(&c)==-EINVAL && !c.sq2_boot_attempted);
  c.iris1=true;c.v4=false;
  assert(venus_sq2_boot(&c)==-EINVAL && !c.sq2_boot_attempted);
 }
 puts("SQ2_VENUS_FW_PASS: 8 boot/protection/shutdown combinations; retained owner and original errors; no second boot; 3 invalid platform checks");
 return 0;
}
