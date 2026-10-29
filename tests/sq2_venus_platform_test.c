/* SPDX-License-Identifier: GPL-2.0-only */
/* Execute the actual owner .c with fake kernel resource providers. */
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define SQ2_VENUS_PLATFORM_HOST_TEST
#define GFP_KERNEL 0
#define THIS_MODULE NULL
#define IORESOURCE_MEM 1
#define IRQF_TRIGGER_HIGH 1
#define IRQF_ONESHOT 2
#define IRQF_NO_AUTOEN 4
typedef uint64_t resource_size_t;
struct device { int dummy; };
struct platform_device { struct device dev; };
struct resource { resource_size_t start,end; };
struct venus_core { struct device *dev; int irq; void *base,*priv,*sq2_fw_owner,*sq2_platform; };
static struct resource resource={0xaa00000,0xaafffff};
static int fail,step,heap,modules,devices,regions,maps,irqs,enabled;
static int fault(void) { return ++step==fail; }
static void *kzalloc(size_t n,int flags)
{ (void)flags;if(fault()) return NULL;heap++;return calloc(1,n); }
static void kfree(void *p) { if(p) heap--;free(p); }
static bool try_module_get(void *m) { (void)m;if(fault())return false;modules++;return true; }
static void module_put(void *m) { (void)m;modules--; }
static struct device *get_device(struct device *d) {devices++;return d;}
static void put_device(struct device *d) {(void)d;devices--;}
static struct resource *platform_get_resource(struct platform_device *p,int t,int i)
{(void)p;(void)t;(void)i;return &resource;}
static resource_size_t resource_size(struct resource *r) {return r->end-r->start+1;}
static int platform_get_irq(struct platform_device *p,int i)
{(void)p;(void)i;return fault() ? -ENXIO : 99;}
static void *request_mem_region(resource_size_t a,resource_size_t b,const char *n)
{(void)a;(void)b;(void)n;if(fault())return NULL;regions++;return &resource;}
static void release_mem_region(resource_size_t a,resource_size_t b)
{(void)a;(void)b;assert(!maps);regions--;}
static void *ioremap(resource_size_t a,resource_size_t b)
{(void)a;(void)b;if(fault())return NULL;maps++;return &resource;}
static void iounmap(void *p) {(void)p;assert(!irqs);maps--;}
static int hfi_isr(int i,void *p) {(void)i;(void)p;return 0;}
static int hfi_isr_thread(int i,void *p) {(void)i;(void)p;return 0;}
static int request_threaded_irq(int i,int (*h)(int,void*),int (*t)(int,void*),
                                int flags,const char *name,void *p)
{(void)i;(void)h;(void)t;(void)name;(void)p;assert(flags & IRQF_NO_AUTOEN);
 if(fault())return -EIO;
 irqs++;
 return 0;}
static void free_irq(int i,void *p) {(void)i;(void)p;assert(!enabled);irqs--;}
static void enable_irq(int i) {(void)i;enabled++;}
static void disable_irq(int i) {(void)i;enabled--;}
#define dev_err(d,...) ((void)(d))
#include "../probe/video/sq2-venus-platform.c"
static void empty(void) {assert(!heap&&!modules&&!devices&&!regions&&!maps&&!irqs&&!enabled);}
int main(void)
{
 struct platform_device p={0};
 struct sq2_venus_platform *o=NULL;
 int f;
 for(f=1;f<=6;f++) {
  fail=f;step=0;
  assert(sq2_venus_platform_alloc(&p,&o)<0 && !o);
  empty();
 }
 fail=0;step=0;
 assert(!sq2_venus_platform_alloc(&p,&o));
 assert(heap==1&&modules==1&&devices==1&&regions==1&&maps==1&&irqs==1&&!enabled);
 assert(sq2_venus_platform_irq(o,true)==-EBUSY);
 o->core.priv=&p;
 assert(!sq2_venus_platform_irq(o,true)&&enabled==1);
 assert(!sq2_venus_platform_irq(o,true)&&enabled==1);
 assert(sq2_venus_platform_release(&o)==-EBUSY);
 assert(!sq2_venus_platform_secure_begin(o));
 assert(sq2_venus_platform_secure_end(o,-ETIMEDOUT)==-ETIMEDOUT&&!enabled);
 assert(sq2_venus_platform_irq(o,true)==-EBUSY);
 o->core.priv=NULL;
 assert(sq2_venus_platform_release(&o)==-EBUSY);
 assert(sq2_venus_platform_secure_end(o,0)==-ETIMEDOUT);
 assert(heap==1&&modules==1&&devices==1&&regions==1&&maps==1&&irqs==1);
 /* Test-only simulated reboot, not an API or driver recovery path. */
 o->lifetime=(struct sq2_venus_lifetime){0};
 assert(!sq2_venus_platform_release(&o)&&!o);empty();
 step=0;assert(!sq2_venus_platform_alloc(&p,&o));
 assert(!sq2_venus_platform_secure_begin(o));
 assert(!sq2_venus_platform_secure_end(o,0));
 o->core.sq2_fw_owner=&p;
 assert(sq2_venus_platform_release(&o)==-EBUSY);
 o->core.sq2_fw_owner=NULL;
 assert(!sq2_venus_platform_release(&o));empty();
 puts("SQ2_PLATFORM_OWNER_PASS: actual owner code; 6 acquisition failures unwind; quarantine pins all resources and masks IRQ; successful stop permits final release");
 return 0;
}
