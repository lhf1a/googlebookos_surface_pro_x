#!/usr/bin/env python3
"""Check translated source tables and execute actual mode-gate branch in host C.

This does NOT emulate a modem or establish that firmware/DMA/LTE works.
"""
from pathlib import Path
import json,re,subprocess,sys,tempfile

def block(s,key):
 start=s.index('{',s.index(key));n=1;end=start+1
 while n:
  n+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start+1:end-1]

def check(src):
 root=Path(__file__).resolve().parents[1]
 cfg=json.loads((root/'configs/sq2-ipa-v4.1-reference.json').read_text())
 data=(src/'data/ipa_data-v4.1.c').read_text();reg=(src/'reg/ipa_reg-v4.1.c').read_text()
 for name,ep in cfg['endpoints'].items():
  b=block(data,'[IPA_ENDPOINT_'+name+']')
  assert int(re.search(r'\.endpoint_id\s*=\s*(\d+)',b)[1])==ep['endpoint_id']
  assert int(re.search(r'\.channel_id\s*=\s*(\d+)',b)[1])==ep['channel_id']
  if name.startswith('AP_'):
   assert int(re.search(r'\.tlv_count\s*=\s*(\d+)',b)[1])==ep['ipa_if_tlv']
   assert re.search(r'\.tre_count\s*=\s*(256|512)',b)
   assert re.search(r'\.event_count\s*=\s*(256|512)',b)
 for side,ref in [('src','source'),('dst','destination')]:
  b=block(data,'ipa_resource_'+side+'[]')
  actual=[tuple(map(int,x)) for x in re.findall(r'\.limits\[(\d+)\]\s*=\s*\{\s*\.min\s*=\s*(\d+),\s*\.max\s*=\s*(\d+)',b)]
  want=[(i,*v) for row in cfg['reference_resources']['data'][ref].values() for i,v in enumerate(row)]
  assert actual==want,(side,actual,want)
 assert re.search(r'\.imem_addr\s*=\s*0x146bd000',data)
 assert re.search(r'\.backward_compat\s*=\s*0x39',data)
 assert re.search(r'\.smem_id\s*=\s*497',data)
 mem=block(data,'ipa_mem_local_data[]')
 assert 'IPA_MEM_AP_HEADER,' not in mem
 assert all(int(x,0)>0 for x in re.findall(r'\.size\s*=\s*(0x[0-9a-fA-F]+|\d+)',mem))
 assert 'GENMASK(31, 0)' in block(reg,'reg_endp_init_hol_block_timer_fmask[]')
 assert 'GENMASK(1, 0)' in block(reg,'reg_endp_init_rsrc_grp_fmask[]')
 assert 'ENDP_SUSPEND' not in reg and '[ENDP_DELAY]' in reg
 assert 'FILT_ROUT_HASH_EN' not in reg and '[ENDP_FILTER_ROUTER_HSH_CFG]' in reg
 assert 'idle_indication_cfg, 0x00000220' in reg
 version=(src/'ipa_version.h').read_text()
 assert 'case IPA_VERSION_4_1:' in block(version,'ipa_version_supported(')
 main=(src/'ipa_main.c').read_text()
 match=block(main,'static const struct of_device_id ipa_match[]')
 assert match.count('.compatible')==1 and 'qcom,sc8180x-ipa' in match
 assert '.name\t\t= "ipa_sq2_lab"' in main
 assert 'module_param(sq2_lab_mode, uint, 0400)' in main
 guard=main[main.index('\tif (sq2_lab_mode < 1'):main.index('\tipa_validate_build();')]
 assert 'sq2_lab_preflight(pdev)' in guard and '-EAGAIN' in guard and '-EPERM' in guard
 preflight=block(main,'static int sq2_lab_preflight(')
 assert not re.search(r'\b(iowrite\w*|writel\w*|qcom_scm_\w+|request_irq|dma_alloc\w*)\s*\(',preflight)
 assert 'resource_size(&stage) < 0x60000' in preflight and 'spec->ids[0] != 0x520' in preflight
 assert 'gsi_status == 0' in preflight and 'clk_disable_unprepare' in preflight
 assert 'ipa_gsi_endpoint_data_empty(ep)' in preflight
 assert 'qcom_scm_pas_auth_and_reset(IPA_PAS_ID)' in main
 # The actual gate calls a mock preflight, never real hardware in this fixture.
 code=r'''
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
static unsigned int sq2_lab_mode,calls;
static void *dev,*pdev;
static int sq2_lab_preflight(void *p) { (void)p;calls++;return 0; }
static int dev_err_probe(void *p,int ret,const char *s,...) { (void)p;(void)s;return ret; }
static int run(void) { int ret;
'''+guard+r'''
 return 0;
}
int main(void) {
 sq2_lab_mode=0;calls=0;assert(run()==-EPERM && calls==0);
 sq2_lab_mode=1;calls=0;assert(run()==-EAGAIN && calls==1);
 sq2_lab_mode=2;calls=0;assert(run()==0 && calls==1);
 sq2_lab_mode=3;calls=0;assert(run()==-EPERM && calls==0);
 return 0;
}
'''
 with tempfile.TemporaryDirectory(prefix='ipa41-lab-gate-') as d:
  c=Path(d)/'gate.c';exe=Path(d)/'gate';c.write_text(code)
  subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 print('IPA41_LAB_SOURCE_PASS:8 endpoints,5+2 resource rows/four groups,IMEM/BCR,4.1 registers/GSI selectors')
 print('ACTUAL_GATE_FIXTURE_PASS:0 refuse before preflight;1 preflight then EAGAIN;2 reaches full init;3 refuse')
 print('LIMIT: source/gate verification only; firmware loading, packet DMA and LTE unverified')
 # Execute the actual sparse endpoint loop with the existing core emptiness
 # predicate. The current6.3 enum has a MODEM_LAN_TX hole at index5.
 core=(src/'ipa_gsi.c').read_text()
 empty='bool ipa_gsi_endpoint_data_empty(const struct ipa_gsi_endpoint_data *data)\n{'+block(core,'bool ipa_gsi_endpoint_data_empty(')+'}'
 loop=preflight[preflight.index(' for (i = 0; !ret && i < ipa_data_v4_1.endpoint_count; i++)'):preflight.index(' dev_info(dev, "SQ2_IPA_LAB_LAYOUT')]
 c=r'''
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#define GSI_EE_AP 0
struct ipa_gsi_endpoint_data { unsigned int ee_id,endpoint_id;bool toward_ipa;struct { unsigned int tlv_count; } channel; };
'''+empty+r'''
static struct ipa_gsi_endpoint_data rows[9]={
 {0,5,true,{20}},{0,10,false,{9}},{0,2,true,{16}},{0,11,false,{9}},
 {1,4,true,{0}},{0},{1,14,false,{0}},{1,3,true,{0}},{1,13,false,{0}} };
static struct { unsigned int endpoint_count;struct ipa_gsi_endpoint_data *endpoint_data; } ipa_data_v4_1={9,rows};
static int run(void) { unsigned int i,tx_count=10,rx_count=13,rx_base=10;int ret=0;
'''+loop+r'''
return ret; }
int main(void) {
 assert(run()==0); /* empty enum slot must not be treated as RX0 */
 rows[2].endpoint_id=10;assert(run()==-ERANGE);rows[2].endpoint_id=2;
 rows[1].endpoint_id=9;assert(run()==-ERANGE);rows[1].endpoint_id=10;
 rows[5].channel.tlv_count=1;assert(run()==-ERANGE); /* invalid nonempty slot still rejected */
 return 0;
}
'''
 with tempfile.TemporaryDirectory(prefix='ipa41-sparse-') as d:
  path=Path(d)/'sparse.c';exe=Path(d)/'sparse';path.write_text(c)
  subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(path),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 print('ACTUAL_SPARSE_LOOP_PASS:core empty-slot predicate; correct8 endpoints pass; nonempty invalid TX/RX rejected')

if __name__=='__main__':
 if len(sys.argv)!=2:raise SystemExit('usage: test_sq2_ipa41_lab.py IPA_SOURCE_SUBTREE')
 check(Path(sys.argv[1]))
