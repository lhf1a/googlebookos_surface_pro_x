#!/usr/bin/env python3
"""Exercise the backported qcom_smmu_cfg_probe with/without hypervisor emulation."""
from pathlib import Path
import subprocess
import sys
import tempfile

source = (Path(sys.argv[1]) / 'drivers/iommu/arm/arm-smmu/arm-smmu-qcom.c').read_text()
assert 'SQ2_SMMU_EXTENDED_SMR_UPSTREAM_5583a55e074b' in source
fn = source[source.index('static int qcom_smmu_cfg_probe('):source.index('static void qcom_smmu_write_s2cr(')]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t u32;
#define FIELD_PREP(m,v) ((v)<<__builtin_ctz(m))
#define FIELD_GET(m,v) (((v)&(m))>>__builtin_ctz(m))
#define ARM_SMMU_GR0_S2CR(n) (0xc00+4*(n))
#define ARM_SMMU_GR0_SMR(n) (0x800+4*(n))
#define ARM_SMMU_SMR_VALID (1U<<31)
#define ARM_SMMU_SMR_ID 0xffffU
#define ARM_SMMU_SMR_MASK 0xffff0000U
#define ARM_SMMU_S2CR_TYPE 0x30000U
#define ARM_SMMU_S2CR_CBNDX 0xffU
#define ARM_SMMU_S2CR_PRIVCFG 0x3000000U
#define S2CR_TYPE_BYPASS 1
#define S2CR_PRIVCFG_DEFAULT 0
#define ARM_SMMU_CB_SCTLR 0
#define CBAR_TYPE_S1_TRANS_S2_BYPASS 1
#define ARM_SMMU_CBAR_TYPE 0x30000U
#define ARM_SMMU_GR1_CBAR(n) (4*(n))
#define dev_notice(...) ((void)0)
struct smr { u32 id,mask;bool valid; };
struct s2cr {u32 type,privcfg,cbndx;};
struct arm_smmu_device {
 unsigned num_mapping_groups,num_context_banks;
 unsigned long context_map[2];
 struct smr smrs[255];struct s2cr s2crs[255];
};
struct qcom_smmu {bool bypass_quirk;unsigned bypass_cbndx;} q;
static bool hyp;
static unsigned last,smr_reads,cb_writes;
static struct qcom_smmu *to_qcom_smmu(struct arm_smmu_device *s) {(void)s;return &q;}
static void arm_smmu_gr0_write(struct arm_smmu_device *s,unsigned off,u32 v)
{(void)s;assert(FIELD_GET(ARM_SMMU_S2CR_TYPE,v)==S2CR_TYPE_BYPASS);last=off;}
static u32 arm_smmu_gr0_read(struct arm_smmu_device *s,unsigned off)
{
 (void)s;
 if(off==last) return FIELD_PREP(ARM_SMMU_S2CR_TYPE,hyp?2:1);
 assert(off>=0x800 && off<0xbfc);smr_reads++;
 return off==ARM_SMMU_GR0_SMR(183) ? 0x80002342 : 0;
}
static void set_bit(unsigned n,unsigned long *p) {assert(n==126);p[1]|=1UL<<62;}
static void arm_smmu_cb_write(struct arm_smmu_device *s,unsigned cb,unsigned off,u32 v)
{(void)s;assert(cb==126 && off==0 && v==0);cb_writes++;}
static void arm_smmu_gr1_write(struct arm_smmu_device *s,unsigned off,u32 v)
{(void)s;assert(off==4*126 && v==0x10000);}
'''
harness += fn
harness += r'''
int main(void) {
 const unsigned sizes[]={64,128,212};
 for(unsigned h=0;h<2;h++) for(unsigned i=0;i<3;i++) {
  struct arm_smmu_device s={.num_mapping_groups=sizes[i],.num_context_banks=127};
  memset(&q,0,sizeof(q));hyp=h;last=smr_reads=cb_writes=0;
  assert(!qcom_smmu_cfg_probe(&s));
  unsigned expected=h && sizes[i]>128 ? 128:sizes[i];
  assert(s.num_mapping_groups==expected && smr_reads==expected);
  assert(last==ARM_SMMU_GR0_S2CR(sizes[i]>128?127:sizes[i]-1));
  assert(q.bypass_quirk==(bool)h && cb_writes==h);
  if(!h && sizes[i]>183) assert(s.smrs[183].valid && s.smrs[183].id==0x2342 &&
   s.smrs[183].mask==0 && s.s2crs[183].type==S2CR_TYPE_BYPASS);
 }
 puts("SMR_GROUPS_PASS: 64/128/212 groups, preserved firmware entry183, hypervisor cap retained");
}
'''
with tempfile.TemporaryDirectory() as name:
    d=Path(name)
    (d/'check.c').write_text(harness)
    subprocess.run(['cc','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=address,undefined',
                    '-no-pie',str(d/'check.c'),'-o',str(d/'check')],check=True)
    subprocess.run([str(d/'check')],check=True)
