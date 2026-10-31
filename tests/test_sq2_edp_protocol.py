"""Execute the actual candidate AUX reader/writer with a fake receiver."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STAGE = Path(os.environ.get('SQ2_EDP_PROTOCOL_STAGE', '/root/.cache/sq2-edp-protocol-v9'))


@unittest.skipUnless(STAGE.exists(), 'requires isolated protocol candidate stage')
class ProtocolTest(unittest.TestCase):
    def test_actual_aux_functions(self):
        dp = STAGE / 'drivers/gpu/drm/msm/dp'
        panel = (dp / 'dp_panel.c').read_text()
        reader = panel[panel.index('static void sq2_edp_read_rates('):panel.index('\nvoid sq2_edp_diag_snapshot(')]
        ctrl = (dp / 'dp_ctrl.c').read_text()
        writer = ctrl[ctrl.index('static int dp_aux_link_configure('):ctrl.index('\nvoid dp_ctrl_push_idle(')]
        support = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
typedef uint8_t u8; typedef uint32_t u32;
#include "sq2_edp_rates.h"
#define DRM_MODE_CONNECTOR_eDP 14
#define DP_EDP_DPCD_REV 0x700
#define DP_EDP_14 3
#define DP_SUPPORTED_LINK_RATES 0x10
#define DP_LINK_BW_SET 0x100
#define DP_LANE_COUNT_SET 0x101
#define DP_LINK_RATE_SET 0x115
#define DP_LINK_CAP_ENHANCED_FRAMING 1
#define DP_LANE_COUNT_ENHANCED_FRAME_EN 0x80
#define min(a,b) ((a)<(b)?(a):(b))
#define dev_info(...) ((void)0)
struct drm_dp_aux { int unused; };
struct connector { int connector_type; };
struct dp_link_info { u32 rate, num_lanes, capabilities; };
struct dp_panel { u32 sq2_edp_rates[8]; bool sq2_edp_use_rate_set;
 struct connector *connector; u32 max_dp_link_rate, max_bw_code;
 struct dp_link_info link_info; };
struct dp_panel_private { struct drm_dp_aux *aux; };
static int compatible=1, bw=10, fail_read=-1, fail_write=-1, write_result=-110;
static unsigned addresses[4], lengths[4], writes; static u8 bytes[4][2];
static int of_machine_is_compatible(const char *s) { (void)s; return compatible; }
static u32 drm_dp_bw_code_to_link_rate(u32 c) { return c*27000; }
static u8 drm_dp_link_rate_to_bw_code(u32 r) { return r/27000; }
static int drm_dp_dpcd_read(struct drm_dp_aux *a,unsigned reg,void *buf,unsigned len) {
 (void)a; if ((int)reg==fail_read) return -110;
 if(reg==DP_EDP_DPCD_REV) *(u8*)buf=DP_EDP_14;
 else if(reg==DP_LINK_BW_SET) *(u8*)buf=bw;
 else { u32 rates[]={162000,216000,243000,270000,324000,432000,540000,0};
  for(unsigned i=0;i<8;i++){((u8*)buf)[2*i]=(rates[i]/20)&255;((u8*)buf)[2*i+1]=(rates[i]/20)>>8;}}
 return len;
}
static int drm_dp_dpcd_write(struct drm_dp_aux *a,unsigned reg,const void *buf,unsigned len) {
 (void)a; assert(writes<4 && len<=2); addresses[writes]=reg;lengths[writes]=len;
 memcpy(bytes[writes],buf,len);return (int)writes++==fail_write?write_result:(int)len;
}
'''
        main = r'''
int main(void) {
 struct drm_dp_aux aux={0};struct dp_panel_private priv={&aux};
 struct connector conn={DRM_MODE_CONNECTOR_eDP};
 struct dp_panel p={.connector=&conn,.max_dp_link_rate=540000,.max_bw_code=20,
  .link_info={270000,4,1}};
 sq2_edp_read_rates(&p,&priv);
 assert(p.link_info.rate==540000 && !p.sq2_edp_use_rate_set);
 assert(dp_aux_link_configure(&aux,&p.link_info,&p)==0 && writes==2);
 assert(addresses[0]==0x101 && bytes[0][0]==0x84);
 assert(addresses[1]==0x100 && bytes[1][0]==20);
 bw=0;writes=0;sq2_edp_read_rates(&p,&priv);
 assert(p.sq2_edp_use_rate_set);
 assert(dp_aux_link_configure(&aux,&p.link_info,&p)==0 && writes==2);
 assert(addresses[1]==0x115 && bytes[1][0]==6);
 for(int n=0;n<2;n++) for(int rc=0;rc<2;rc++) {
  writes=0;fail_write=n;write_result=rc?-110:0;
  assert(dp_aux_link_configure(&aux,&p.link_info,&p)==(rc?-110:-EIO));
  assert(writes==(unsigned)n+1);
 }
 fail_write=-1;writes=0;p.link_info.rate=432000;
 assert(dp_aux_link_configure(&aux,&p.link_info,&p)==-EINVAL && writes==0);
 p.link_info.rate=270000;fail_read=DP_LINK_BW_SET;sq2_edp_read_rates(&p,&priv);
 assert(!p.sq2_edp_use_rate_set && !sq2_edp_lower_rate(p.sq2_edp_rates,~0U));
 assert(p.link_info.rate==270000);
 assert(dp_aux_link_configure(&aux,&p.link_info,&p)==0 && writes==1);
 assert(addresses[0]==0x100 && lengths[0]==2 && bytes[0][0]==10 && bytes[0][1]==0x84);
 fail_read=-1;compatible=0;writes=0;sq2_edp_read_rates(&p,&priv);
 assert(!sq2_edp_lower_rate(p.sq2_edp_rates,~0U));
 compatible=1;conn.connector_type=0;sq2_edp_read_rates(&p,&priv);
 assert(!sq2_edp_lower_rate(p.sq2_edp_rates,~0U));
 puts("PASS: legacy/table selection, slot6, lane order, AUX failures, safe fallback, non-target");
}
'''
        with tempfile.TemporaryDirectory() as td:
            source = Path(td) / 'test.c'
            source.write_text(support + reader + writer + main)
            exe = Path(td) / 'test'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare',
                            '-I', str(ROOT / 'probe/graphics'), str(source), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
