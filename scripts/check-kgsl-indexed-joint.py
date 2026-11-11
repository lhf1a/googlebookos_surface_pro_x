#!/usr/bin/env python3
"""Execute one loader -> RPMh/HFI -> bootstrap/busmon -> GMU index chain."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
extractor=load('extract',ROOT/'scripts/check-kgsl-dmabuf.py')
rpmh_tool=load('rpmh_tool',ROOT/'scripts/check-kgsl-rpmh.py')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--kernel-source',type=Path,required=True);p.add_argument('--cmd-db-dump',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--bus-source',type=Path);p.add_argument('--mutations',action='store_true');a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=False);s=a.source.resolve();out=a.output.resolve()
    policy_bytes=(ROOT/'configs/kgsl-sc8180x-downstream-policy-reference.json').read_bytes();(out/'REFERENCE.json').write_bytes(policy_bytes);policy=json.loads(policy_bytes)
    dump_bytes=a.cmd_db_dump.read_bytes();(out/'CMD-DB.txt').write_bytes(dump_bytes)
    header='static const u32 source_ddr[]={'+','.join(map(str,policy['ddr_table_kBps']))+'};\nstatic const struct fixture_row source_rows[]={\n'
    header+=''.join('{%d,%d,%d,%d},\n'%(r['gpu_hz'],r['default_index'],r['min_index'],r['max_index']) for r in policy['frequency_bindings'])+'};\n'
    hz=sorted(policy['captured_target_frequency_state_from_existing_project_record']['hz'],reverse=True);assert len(hz)==10
    header+='static const u32 target_ten_hz[]={'+','.join(map(str,hz))+'};\n';(out/'source-policy.h').write_text(header)
    rpmh=(s/'adreno_a6xx_rpmh.c').read_text();hfi=(s/'adreno_a6xx_hfi.h').read_text();core=(s/'kgsl_gmu_core.h').read_text();tcs=(a.kernel_source/'include/soc/qcom/tcs.h').read_text()
    cnoc=re.search(r'u32 cnoc_table\[MAX_CNOC_LEVELS\] = \{([^}]+)\}',rpmh).group(1)
    with (out/'source-policy.h').open('a') as f:f.write('static const u32 source_cnoc[]={'+cnoc+'};\n')
    types=[rpmh_tool.block(rpmh,r'^(struct '+name+r' \{.*?^\};)') for name in ['bcm','bcm_data','rpmh_bw_votes']]
    types+=[rpmh_tool.block(hfi,r'^(struct hfi_bwtable_cmd \{.*?^\} __packed;)')]
    for name in ['MAX_GX_LEVELS','MAX_CNOC_LEVELS','MAX_CNOC_CMDS','MAX_BW_CMDS']:types.insert(0,rpmh_tool.macro(core,name))
    for text,names in [(tcs,['BCM_TCS_CMD_COMMIT_MASK','BCM_TCS_CMD_VALID_MASK','BCM_TCS_CMD_VOTE_MASK','BCM_TCS_CMD_VOTE_X_MASK','BCM_TCS_CMD_VOTE_Y_MASK','BCM_TCS_CMD']), (hfi,['HFI_MSG_CMD','CREATE_MSG_HDR','H2F_MSG_BW_VOTE_TBL','HFI_MAX_MSG_SIZE','CMD_MSG_HDR'])]:
        types += [rpmh_tool.macro(text,n) for n in names]
    types += [rpmh_tool.block(rpmh,r'^(static const struct bcm '+name+r'\[\] = \{.*?^\};)') for name in ['sc8180x_ddr_bcms','sc8180x_cnoc_bcms']]
    type_body='\n\n'.join(types)+'\n';(out/'joint-types.h').write_text(type_body)
    entries={name:(int(address,16),bytes.fromhex(data)) for address,name,data in re.findall(r'^0x([0-9a-f]+): (\S+) \[([0-9a-f ]+)\]$',dump_bytes.decode(),re.M)}
    target='static const struct db_entry target_entries[]={\n'
    for name in ['MC0','SH0','ACV','SH2','SN15','SN3','CN0']:
        addr,data=entries[name];assert addr and len(data)>=8 and len(data)<=64
        target+='{"%s",0x%x,%d,{%s}},\n'%(name,addr,len(data),','.join(map(str,data)))
    target+='};\n';(out/'joint-target.h').write_text(target)
    base=(ROOT/'tests/kgsl_indexed_policy_test.c').read_text();prefix=base[:base.index('/* REAL_BUS_SOURCE */')]
    extra='''
typedef uint16_t u16;typedef uint8_t u8;typedef uint16_t __le16;typedef uint32_t __le32;
#define __packed __attribute__((packed))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define SZ_1K 1024
#define BIT(n) (1U<<(n))
#define GENMASK(h,l) ((UINT32_MAX>>(31-(h)))&(UINT32_MAX<<(l)))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define do_div(n,b) ((n)/=(b))
#define WARN_ON(x) (!!(x))
static u32 u32_encode_bits(u32 value,u32 mask){return (value<<__builtin_ctz(mask))&mask;}
static u16 get_unaligned_le16(const void *p){const u8 *b=p;return b[0]|((u16)b[1]<<8);}
static u32 get_unaligned_le32(const void *p){const u8 *b=p;return get_unaligned_le16(b)|((u32)get_unaligned_le16(b+2)<<16);}
#include "joint-types.h"
'''
    prefix=prefix.replace('struct kgsl_pwrlevel {',extra+'\nstruct kgsl_pwrlevel {',1)
    prefix=prefix.replace('bool icc_dirty;','bool icc_dirty;int cur_buslevel;bool bus_control;',1)
    prefix=prefix.replace('struct device {struct device_node *of_node;};','struct device {struct device_node *of_node;struct kgsl_device *data;};')
    prefix=prefix.replace('struct kgsl_device {struct kgsl_pwrctrl pwrctrl;const struct kgsl_functable *ftbl;};','''struct kgsl_device {struct kgsl_pwrctrl pwrctrl;const struct kgsl_functable *ftbl;struct platform_device *pdev;int mutex;struct {bool enabled;struct {u32 flag,percent_ab;unsigned long ab_mbytes;} bus_profile;} pwrscale;};
struct a6xx_gmu_device {struct {struct hfi_bwtable_cmd bw_table;} hfi;};
struct adreno_device {struct kgsl_device device;struct a6xx_gmu_device gmu;bool a680;};
#define KGSL_DEVICE(a) (&(a)->device)
#define to_a6xx_gmu(a) (&(a)->gmu)
#define adreno_is_a680(a) ((a)->a680)''')
    prefix=prefix.replace('static struct kgsl_device gpu;','static struct adreno_device adreno;\n#define gpu adreno.device')
    (out/'joint-prefix.h').write_text(prefix)
    (out/'joint-reset.h').write_text(extractor.extract(base.replace('static void reset(void){','static void reset(void)\n{'),'reset'))
    hints=(s/'msm_adreno_devfreq.h').read_text();(out/'joint-hints.h').write_text('\n'.join(rpmh_tool.macro(hints,n) for n in ['DEVFREQ_FLAG_FAST_HINT','DEVFREQ_FLAG_SLOW_HINT'])+'\n')
    # These are the real enabled #ifdef branches: kgsl_pwrscale.h includes
    # msm_adreno_devfreq.h, which defines the two extracted flags (0x4/0x8).
    scale=(s/'kgsl_pwrscale.c').read_text();gmu=(s/'adreno_a6xx_gmu.c').read_text()
    hint_functions=[re.search(r'^static inline bool '+n+r'\([^;{]*\)\n\{.*?^\}',scale,re.M|re.S).group(0) for n in ['_check_fast_hint','_check_slow_hint']]
    consumers='\n\n'.join(hint_functions+[extractor.extract(scale,'kgsl_busmon_target'),extractor.extract(gmu,'a6xx_gmu_bus_set')])
    rpmh_funcs='\n\n'.join(extractor.extract(rpmh,n) for n in ['tcs_cmd_data','free_rpmh_bw_votes','build_rpmh_bw_votes','build_bw_table_cmd','build_bw_table'])
    bus_path=a.bus_source or s/'kgsl_bus.c';bus=re.sub(r'^#include[^\n]*\n','',bus_path.read_text(),flags=re.M)
    harness=ROOT/'tests/kgsl_indexed_joint_test.c'
    ledger=[]
    def run(cmd):
        r=subprocess.run(cmd,text=True,capture_output=True,timeout=30,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
        ledger.append(dict(command=list(map(str,cmd)),stdout=r.stdout,stderr=r.stderr,exit=r.returncode));(out/'LEDGER.json').write_text(json.dumps(ledger,indent=2)+'\n');return r
    def variant(name,b,c,t,cases):
        for suffix,text in [('bus',b),('consumers',c),('rpmh',t)]:
            (out/(name+'-'+suffix+'.h')).write_text(text)
        (out/'joint-bus.h').write_text(b);(out/'joint-consumers.h').write_text(c);(out/'joint-rpmh.h').write_text(t);binary=out/name
        r=run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-sign-compare','-fsanitize=address,undefined','-fno-pie','-no-pie','-fno-omit-frame-pointer','-I',str(out),str(harness),'-o',str(binary)])
        if r.returncode:raise RuntimeError(r.stderr)
        exits=[run([str(binary),case]).returncode for case in cases];summary=dict(variant=name,compiled_exit=0,passed=exits.count(0),failed=len(exits)-exits.count(0));print(json.dumps(summary),flush=True);return summary
    cases=['joint','ten-enabled','dcvs-error','icc-error']+['metadata-'+n for n in ['MC0','SH0','ACV','SH2','SN15','SN3','CN0']]+['alloc-'+str(i) for i in range(1,22)]
    summaries=[variant('actual',bus,consumers,rpmh_funcs,cases)]
    if a.mutations:
        mutations=[('bootstrap-default',bus.replace('level = pwr->pwrlevels[pwr->default_pwrlevel].bus_min;','level = pwr->pwrlevels[pwr->default_pwrlevel].bus_freq;'),consumers,rpmh_funcs,['joint']),
         ('global-clamp',bus,consumers.replace('requested = clamp_t(s64, requested, pwr_level->bus_min,\n\t\tpwr_level->bus_max);','requested = clamp_t(s64, requested, 1, pwr->pwrlevels[0].bus_max);'),rpmh_funcs,['joint']),
         ('hfi-truncate',bus,consumers,rpmh_funcs.replace('cmd->bw_level_num = ddr->num_levels;','cmd->bw_level_num = ddr->num_levels - 1;'),['joint']),
         ('hfi-repeat-off',bus,consumers,rpmh_funcs.replace('(u32) ddr->cmds[i][j]','(u32) ddr->cmds[0][j]'),['joint']),
         ('binary-kilo',bus,consumers,rpmh_funcs.replace('1000ULL','1024ULL'),['joint']),
         ('hfi-address',bus,consumers,rpmh_funcs.replace('cmd->ddr_cmd_addrs[i] = ddr->addrs[i];','cmd->ddr_cmd_addrs[i] = 0;'),['joint']),
         ('wrong-dcvs-index',bus,consumers.replace('INVALID_DCVS_IDX, buslevel','INVALID_DCVS_IDX, 0'),rpmh_funcs,['joint']),
         ('collapse-default',bus.replace('levels[i].bus_freq = def;','levels[i].bus_freq = max;'),consumers,rpmh_funcs,['joint']),
         ('missing-enabled-policy',bus.replace('if (!seen[i] || levels[i].bus_max > levels[0].bus_max)','if (levels[i].bus_max > levels[0].bus_max)'),consumers,rpmh_funcs,['ten-enabled'])]
        for name,b,c,t,cases in mutations:
            assert (b,c,t)!=(bus,consumers,rpmh_funcs),name
            summaries.append(variant(name,b,c,t,cases))
    # Restore extracted originals so artifacts describe the positive variant.
    (out/'joint-bus.h').write_text(bus);(out/'joint-consumers.h').write_text(consumers);(out/'joint-rpmh.h').write_text(rpmh_funcs)
    paths=[bus_path,s/'adreno_a6xx_rpmh.c',s/'kgsl_pwrscale.c',s/'adreno_a6xx_gmu.c',s/'adreno_a6xx_hfi.h',s/'kgsl_gmu_core.h',s/'msm_adreno_devfreq.h',s/'kgsl_pwrscale.h',a.kernel_source/'include/soc/qcom/tcs.h',harness,ROOT/'tests/kgsl_indexed_policy_test.c']
    report=dict(status='joint-host-contract-not-hardware-calibration',source_sha256={str(q.resolve()):sha(q) for q in paths},reference_sha256=hashlib.sha256(policy_bytes).hexdigest(),cmd_db_sha256=hashlib.sha256(dump_bytes).hexdigest(),summaries=summaries,same_instance=True,same_loader_table_allocation=True,real_hfi_ddr_words_checked=39,real_hfi_cnoc_words_checked=8,frequencies=7,hint_steps_per_success=280,models=['OF','OPP','ICC','DCVS transport','mutex','allocator'])
    (out/'RESULT.json').write_text(json.dumps(report,indent=2)+'\n')
    return int(summaries[0]['failed']!=0 or any(not s['failed'] for s in summaries[1:]))
if __name__=='__main__':raise SystemExit(main())
