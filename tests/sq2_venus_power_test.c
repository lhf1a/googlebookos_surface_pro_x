/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#define SQ2_VENUS_POWER_HOST_TEST
#define __iomem
#define GFP_KERNEL 0
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
typedef uint32_t u32;
struct device { int id; };
struct clk { unsigned long rate; };
struct reset_control { int id; };
struct icc_bulk_data { const char *name; unsigned int avg_bw,peak_bw; void *path; };
static struct device pd[3];static struct clk clks[7];static struct reset_control rst[4];
static int step,fail,heap,iccs,pds,clock_refs,resets_count,enabled,powered,ci,ri;
static char trace[512];static int ti;
static void t(char c){assert(ti<511);trace[ti++]=c;trace[ti]=0;}
static int bad(void){return ++step==fail;}
static void *kzalloc(size_t n,int f){(void)f;if(bad())return NULL;heap++;return calloc(1,n);}
static void kfree(void *p){heap--;free(p);}
static int of_icc_bulk_get(struct device *d,int n,struct icc_bulk_data *p)
{int i;(void)d;if(bad())return -EIO;iccs=n;for(i=0;i<n;i++)p[i].path=&pd[0];return 0;}
static int icc_bulk_set_bw(int n,struct icc_bulk_data *p)
{(void)n;assert(p[0].avg_bw==1000&&p[1].peak_bw==304000&&p[2].peak_bw==304000);t('B');return bad()?-EIO:0;}
static int icc_set_bw(void *p,int a,int b){(void)p;assert(!a&&!b);t('b');return 0;}
static void icc_bulk_put(int n,struct icc_bulk_data *p){(void)p;iccs-=n;}
static struct device *dev_pm_domain_attach_by_id(struct device *d,int i)
{(void)d;if(bad())return ERR_PTR(-EIO);pds++;return &pd[i];}
static void dev_pm_domain_detach(struct device *d,bool f){(void)d;(void)f;pds--;}
static struct clk *clk_get(struct device *d,const char *name)
{(void)d;(void)name;if(bad())return ERR_PTR(-EIO);clock_refs++;clks[ci].rate=19200000;return &clks[ci++];}
static void clk_put(struct clk *c){(void)c;clock_refs--;}
static struct reset_control *reset_control_get_exclusive(struct device *d,const char *n)
{(void)d;(void)n;if(bad())return ERR_PTR(-EIO);resets_count++;return &rst[ri++];}
static void reset_control_put(struct reset_control *r){(void)r;resets_count--;}
static int reset_status(struct device *d,int i,struct reset_control *r)
{(void)d;(void)i;(void)r;return bad()?-EIO:0;}
static int pm_runtime_resume_and_get(struct device *d)
{(void)d;t('P');if(bad())return -EIO;powered++;return 0;}
static int pm_runtime_put_sync(struct device *d){(void)d;t('p');powered--;return 0;}
static int reset_control_assert(struct reset_control *r){(void)r;t('A');return bad()?-EIO:0;}
static int reset_control_deassert(struct reset_control *r){(void)r;t('D');return bad()?-EIO:0;}
static void usleep_range(int a,int b){assert(a==150&&b==250);}
static int clk_prepare_enable(struct clk *c){(void)c;t('C');if(bad())return -EIO;enabled++;return 0;}
static void clk_disable_unprepare(struct clk *c){(void)c;t('c');enabled--;}
static unsigned long clk_get_rate(struct clk *c){return c->rate;}
static int clk_set_rate(struct clk *c,unsigned long rate){t('R');if(bad())return -EIO;c->rate=rate;return 0;}
static u32 readl(void *p){if((uintptr_t)p==0x10e0000){t('V');return 0x5010002f;}return 0;}
static void writel(u32 v,void *p){(void)v;(void)p;}
#include "../probe/video/sq2-venus-power.c"
static void empty(void){assert(!heap&&!iccs&&!pds&&!clock_refs&&!resets_count&&!enabled&&!powered);}
static void reset(void){empty();step=ti=ci=ri=0;trace[0]=0;}
int main(void)
{
 struct device dev={0};struct sq2_venus_power *p=NULL;int max,f;
 reset();fail=0;assert(!sq2_venus_power_on(&dev,(void *)0x1000000,&p));max=step;
 assert(!strcmp(trace,"BPPPAAAADDDDCCCCCCCRV"));
 assert(sq2_venus_power_ready(p));
 assert(!sq2_venus_power_off(&p,true));assert(!p);empty();
 for(f=1;f<=max;f++) {
  reset();fail=f;assert(sq2_venus_power_on(&dev,(void *)0x1000000,&p)<0);
  fail=0;assert(!sq2_venus_power_off(&p,false));assert(!p);empty();
 }
 reset();fail=0;assert(!sq2_venus_power_on(&dev,(void *)0x1000000,&p));
 fail=step+1;assert(sq2_venus_power_off(&p,true)==-EIO);
 assert(p&&powered==3&&enabled==7&&iccs==3&&!sq2_venus_power_ready(p));
 fail=0;assert(!sq2_venus_power_off(&p,true));empty();
 printf("SQ2_POWER_OWNER_PASS: actual C; %d acquisition/activation failure positions unwind; ICC/domain/reset/clock order; rate restore failure retains powered references; mock providers only\n",max);
 return 0;
}
