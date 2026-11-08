#!/usr/bin/env python3
"""Run the generated boot loop against immediate, late, invalid and absent replies."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (Path(sys.argv[1]) / 'hfi_venus.c').read_text()
fn = source[source.index('static int venus_boot_core('):source.index('static u32 venus_hwversion(')]
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
#define __iomem
#define BIT(n) (1U<<(n))
#define READ_ONCE(x) (x)
#define module_param(a,b,c)
#define IS_SQ2_HFI(c) ((c)->sq2)
#define IS_IRIS1(c) 1
#define IS_V6(c) 0
#define VIDC_CTRL_INIT_CTRL_SHIFT 0
#define VIDC_CTRL_INIT 0x48
#define CPU_CS_SCIACMDARG0 0x4c
#define CPU_CS_SCIACMDARG3 0x58
#define CPU_CS_SCIACMDARG0_ERROR_STATUS_MASK 0xfe
#define WRAPPER_INTR_MASK 0x10
#define WRAPPER_INTR_MASK_A2HWD_BASK_V6 1
#define WRAPPER_INTR_MASK_A2HCPU_MASK 2
#define WRAPPER_INTR_MASK_A2HVCODEC_MASK 4
#define CPU_CS_H2XSOFTINTEN_V6 0x20
#define CPU_CS_X2RPMH_V6 0x24
struct device {int unused;};
struct core {struct device *dev;bool sq2;void *cpu_cs_base,*wrapper_base;};
struct hfi_queue_header {u32 read_idx,write_idx,rx_req;};
struct queue {struct hfi_queue_header *qhdr;};
struct mem {void *kva;unsigned long da;unsigned int size;};
struct venus_hfi_device {struct core *core;struct queue queues[3];struct mem sfr,ifaceq_table;};
static unsigned char mmio[512];
static unsigned reads,sleeps,records,ready_at,writes;
static u32 reply;
static u32 readl(void *p) {
 unsigned off=(unsigned char *)p-mmio;
 if(off==0x110) return ~0U;
 assert(off==0x4c);
 return reads++>=ready_at ? reply:0;
}
static void writel(u32 v,void *p) {(void)v;unsigned off=(unsigned char *)p-mmio;assert(off==0x48||off==0x58||off==0x110);writes++;}
static void usleep_range(unsigned lo,unsigned hi) {assert(lo==500&&hi==1000);sleeps++;}
static u32 venus_iris1_irq_mask(u32 v) {return v&~3U;}
static void dma_rmb(void) {}
static void dev_info(void *d,const char *fmt,...) {(void)d;(void)fmt;records++;}
static void dev_err(void *d,const char *fmt,...) {(void)d;(void)fmt;}
#include "venus-sq2-boot-watch.h"
'''
tests = r'''
int main(void) {
 struct core c={.cpu_cs_base=mmio,.wrapper_base=mmio+256};
 struct hfi_queue_header q[3]={{.rx_req=1},{0},{0}};
 u32 sfr[5]={4096};
 struct venus_hfi_device h={.core=&c,.sfr={.kva=sfr,.size=sizeof(sfr)}};
 for(unsigned i=0;i<3;i++) h.queues[i].qhdr=&q[i];
 for(unsigned sq=0;sq<2;sq++) for(unsigned watch=0;watch<2;watch++) {
  c.sq2=sq;sq2_boot_watch=watch;
  for(unsigned late=0;late<6;late++) {
   reads=sleeps=records=writes=0;
   const unsigned points[]={0,99,149,1999,10000,0};
   ready_at=points[late];
   reply=late==5 ? 4:1;
   int ret=venus_boot_core(&h);
   unsigned limit=sq&&watch ? 2000:100;
   if(late==5) assert(ret==-EINVAL && reads==1 && !sleeps);
   else if(ready_at>=limit) assert(ret==-ETIMEDOUT && reads==limit);
   else assert(!ret && reads==ready_at+1);
   assert(writes==(sq ? 2:3));
   if (sq && watch) assert(records>0 && records<6);
   else assert(records==(sq && ret==-ETIMEDOUT ? 1U:0U));
   assert(q[0].rx_req==1 && !q[0].write_idx && !q[1].write_idx && !sfr[1]);
  }
 }
 /* Malformed or absent CPU buffers must suppress the bounded snapshot. */
 c.sq2=true;sq2_boot_watch=false;
 for(unsigned broken=0;broken<5;broken++) {
  reads=sleeps=records=writes=0;ready_at=10000;reply=1;
  h.sfr.kva=sfr;h.sfr.size=sizeof(sfr);
  for(unsigned i=0;i<3;i++) h.queues[i].qhdr=&q[i];
  if(broken<3) h.queues[broken].qhdr=NULL;
  else if(broken==3) h.sfr.kva=NULL;
  else h.sfr.size=19;
  assert(venus_boot_core(&h)==-ETIMEDOUT);
  assert(reads==100 && writes==2 && !records);
 }
 puts("BOOT_WATCH_PASS: 24 reply/profile +5 buffer-boundary cases; default SQ2 timeout one snapshot; legacy/default bound100, explicit SQ2 bound2000, no new MMIO/queue writes");
}
'''
with tempfile.TemporaryDirectory() as name:
    d=Path(name)
    (d/'check.c').write_text(prefix+fn+tests)
    subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-no-pie','-I',str(root/'probe/video'),str(d/'check.c'),
                    '-o',str(d/'check')],check=True)
    subprocess.run([str(d/'check')],check=True)
