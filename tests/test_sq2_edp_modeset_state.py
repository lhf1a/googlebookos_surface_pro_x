"""Run the candidate's actual bridge functions against a state-machine mock."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

STAGE = Path(os.environ.get('SQ2_EDP_MODESET_STAGE', '/root/.cache/sq2-edp-modeset-v11'))


@unittest.skipUnless(STAGE.exists(), 'requires isolated modeset stage')
class ModesetTest(unittest.TestCase):
    def test_actual_bridge_transitions(self):
        source = (STAGE / 'drivers/gpu/drm/msm/dp/dp_display.c').read_text()
        enable = source[source.index('void dp_bridge_enable('):source.index('\nvoid dp_bridge_disable(')]
        disable = source[source.index('void dp_bridge_post_disable('):source.index('\nvoid dp_bridge_mode_set(')]
        support = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef unsigned u32;
enum { ST_DISCONNECTED, ST_MAINLINK_READY, ST_CONNECTED, ST_DISCONNECT_PENDING, ST_DISPLAY_OFF };
struct mode { struct { int clock; } drm_mode; };
struct msm_dp { bool is_edp; struct mode dp_mode; int connector_type; };
struct dp_display_private { struct msm_dp dp_display; struct mode dp_mode; int event_mutex;
 u32 hpd_state; int drm_dev; };
struct msm_dp_bridge { struct msm_dp *dp_display; };
struct drm_bridge { int unused; };
static struct dp_display_private display;
static struct msm_dp_bridge bridge={&display.dp_display};
static int compatible=1, plugs, unplugs, enables, disables, phy_inits; static bool forced;
#define container_of(p,t,m) ((void)(p), &display)
#define to_dp_bridge(p) ((void)(p), &bridge)
#define DRM_ERROR(...) ((void)0)
#define drm_dbg_dp(...) ((void)0)
static void mutex_lock(int *p) {(void)p;} static void mutex_unlock(int *p){(void)p;}
static int of_machine_is_compatible(const char *s){(void)s;return compatible;}
static void sq2_trace_display(struct dp_display_private *p,const char *s){(void)p;(void)s;}
static void dp_hpd_plug_handle(struct dp_display_private *p,int n){(void)n;plugs++;p->hpd_state=ST_MAINLINK_READY;}
static void dp_hpd_unplug_handle(struct dp_display_private *p,int n){(void)n;unplugs++;p->hpd_state=ST_DISCONNECT_PENDING;}
static int dp_display_set_mode(struct msm_dp *p,struct mode *m){(void)p;(void)m;return 0;}
static void dp_display_host_phy_init(struct dp_display_private *p){(void)p;phy_inits++;}
static void dp_display_enable(struct dp_display_private *p,bool f){(void)p;enables++;forced=f;}
static int dp_display_post_enable(struct msm_dp *p){(void)p;return 0;}
static void dp_display_disable(struct dp_display_private *p){(void)p;disables++;}
'''
        main = r'''
static void reset(int compat,bool edp,u32 state){
 compatible=compat;plugs=unplugs=enables=disables=phy_inits=0;forced=false;
 display.hpd_state=state;display.dp_display.is_edp=edp;display.dp_mode.drm_mode.clock=355500;
}
int main(void){struct drm_bridge b={0};
 reset(1,true,ST_CONNECTED);dp_bridge_post_disable(&b);
 assert(!unplugs && disables==1 && display.hpd_state==ST_DISPLAY_OFF);
 dp_bridge_enable(&b);
 assert(!plugs && enables==1 && phy_inits==1 && forced && display.hpd_state==ST_CONNECTED);
 reset(1,true,ST_DISCONNECTED);dp_bridge_enable(&b);
 assert(plugs==1 && enables==1 && !forced && display.hpd_state==ST_CONNECTED);
 reset(0,true,ST_CONNECTED);dp_bridge_post_disable(&b);
 assert(unplugs==1 && disables==1 && display.hpd_state==ST_DISCONNECTED);
 dp_bridge_enable(&b);assert(plugs==1 && enables==1);
 reset(1,false,ST_CONNECTED);dp_bridge_post_disable(&b);
 assert(!unplugs && disables==1 && display.hpd_state==ST_DISPLAY_OFF);
 reset(1,true,ST_DISCONNECT_PENDING);dp_bridge_post_disable(&b);
 assert(!unplugs && disables==1 && display.hpd_state==ST_DISCONNECTED);
 puts("PASS: internal modeset preserves DISPLAY_OFF, forced retraining, initial connect, external/non-target and real disconnect");
}
'''
        with tempfile.TemporaryDirectory() as td:
            path=Path(td); (path/'test.c').write_text(support+enable+disable+main)
            subprocess.run(['cc','-Wall','-Wextra','-Werror',str(path/'test.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)


if __name__ == '__main__':
    unittest.main()
