"""Execute the candidate C helper with bounded AUX fault injection."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('wake', ROOT / 'scripts/patch-sq2-edp-wake.py')
wake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wake)

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#define DRM_MODE_CONNECTOR_eDP 14
#define DP_SET_POWER 0x600
#define DP_SET_POWER_D0 1
struct connector { int connector_type; };
struct dp_panel { struct connector *connector; struct { int revision; } link_info; };
struct dp_panel_private { void *aux, *dev; };
static bool surface = true;
static int calls, waits, replies[3];
static bool of_machine_is_compatible(const char *s) { (void)s; return surface; }
static ssize_t drm_dp_dpcd_writeb(void *aux, int reg, int value) {
    (void)aux; assert(reg == DP_SET_POWER && value == DP_SET_POWER_D0);
    assert(calls < 3); return replies[calls++];
}
static void usleep_range(int a, int b) { assert(a == 1000 && b == 2000); waits++; }
#define dev_info(...) ((void)0)
'''
CASES = r'''
int main(void) {
    struct connector c = {14};
    struct dp_panel p = {&c, {0x14}};
    struct dp_panel_private priv = {0};
    replies[0]=1;
    assert(sq2_edp_wake_before_caps(&p,&priv)==0 && calls==1 && waits==1);
    calls=waits=0; replies[0]=-ETIMEDOUT; replies[1]=0; replies[2]=1;
    assert(sq2_edp_wake_before_caps(&p,&priv)==0 && calls==3 && waits==3);
    calls=waits=0; replies[2]=-ETIMEDOUT;
    assert(sq2_edp_wake_before_caps(&p,&priv)==-ETIMEDOUT && calls==3);
    calls=waits=0; replies[0]=replies[1]=replies[2]=0;
    assert(sq2_edp_wake_before_caps(&p,&priv)==-EIO && calls==3);
    calls=waits=0; p.link_info.revision=0;
    assert(sq2_edp_wake_before_caps(&p,&priv)==0 && calls==0);
    p.link_info.revision=0x14; surface=false;
    assert(sq2_edp_wake_before_caps(&p,&priv)==0 && calls==0);
    surface=true; c.connector_type=10;
    assert(sq2_edp_wake_before_caps(&p,&priv)==0 && calls==0);
    p.connector=0;
    assert(sq2_edp_wake_before_caps(&p,&priv)==0 && calls==0);
    return 0;
}
'''


class WakeCandidate(unittest.TestCase):
    def test_real_c_helper_with_aux_failures_and_device_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'wake.c'
            binary = Path(directory) / 'wake'
            source.write_text(HARNESS + wake.HELPER + CASES)
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_unknown_source_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Expected unmodified'):
            wake.patch(b'unknown source')


if __name__ == '__main__':
    unittest.main()
