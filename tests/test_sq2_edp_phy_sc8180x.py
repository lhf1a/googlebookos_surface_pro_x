"""Run the backported setter against fake MMIO, never a real PHY."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('phy', ROOT / 'scripts/patch-sq2-edp-phy-sc8180x.py')
phy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(phy)
BASE = Path('/root/.cache/sq2-thermal/kernel/drivers/phy/qualcomm/phy-qcom-edp.c')


class SignalBackport(unittest.TestCase):
    def test_reject_unknown_input(self):
        with self.assertRaises(ValueError):
            phy.patch(b'unknown')

    @unittest.skipUnless(BASE.exists(), 'requires pinned local kernel source')
    def test_actual_setter_tables_ldo_and_non_target(self):
        result = phy.patch(BASE.read_bytes()).decode()
        tables = result[result.index('static const u8 edp_swing_hbr_rbr'):result.index('static int qcom_edp_phy_init')]
        setter = result[result.index('static int qcom_edp_set_voltages'):result.index('static int qcom_edp_phy_configure')]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
typedef unsigned char u8;
#define max(a,b) ((a)>(b)?(a):(b))
#define TXn_LDO_CONFIG 0x84
#define TXn_TX_DRV_LVL 0x14
#define TXn_TX_EMP_POST1_LVL 4
struct qcom_edp_cfg { bool is_dp; const u8 (*swing_hbr_rbr)[4][4];
 const u8 (*swing_hbr3_hbr2)[4][4]; const u8 (*pre_emphasis_hbr_rbr)[4][4];
 const u8 (*pre_emphasis_hbr3_hbr2)[4][4]; };
struct phy_configure_opts_dp { unsigned int lanes, link_rate, voltage[4], pre[4]; };
struct qcom_edp { const struct qcom_edp_cfg *cfg; unsigned char *tx0,*tx1;
 struct phy_configure_opts_dp dp_opts; };
static int writes;
static void writel(unsigned int value, void *address) { *(unsigned char *)address=value;writes++; }
'''
        cases = r'''
int main(void) {
 unsigned char a[256]={0},b[256]={0};
 struct qcom_edp edp={&sc8180x_edp_phy_cfg,a,b,{.lanes=4,.link_rate=5400}};
 const u8 expected[4][4]={{0x0c,0x15,0x19,0x1e},{0x08,0x15,0x19,0xff},
 {0x0e,0x14,0xff,0xff},{0x0d,0xff,0xff,0xff}};
 for(unsigned int v=0;v<4;v++)for(unsigned int p=0;p<4;p++) {
  writes=0;edp.dp_opts.voltage[3]=v;edp.dp_opts.pre[2]=p;
  int rc=qcom_edp_set_voltages(&edp,&edp.dp_opts);
  if(expected[v][p]==0xff){assert(rc==-EINVAL && writes==0);continue;}
  assert(rc==0 && writes==6);
  assert(a[TXn_LDO_CONFIG]==0x41 && b[TXn_LDO_CONFIG]==0x41);
  assert(a[TXn_TX_EMP_POST1_LVL]==expected[v][p]);
  assert(b[TXn_TX_EMP_POST1_LVL]==expected[v][p]);
  assert(a[TXn_TX_DRV_LVL]==edp_swing_hbr2_hbr3[v][p]);
 }
 memset(&edp.dp_opts,0,sizeof edp.dp_opts);edp.dp_opts.lanes=2;edp.dp_opts.link_rate=2700;
 assert(qcom_edp_set_voltages(&edp,&edp.dp_opts)==0);
 assert(a[TXn_LDO_CONFIG]==0x81 && b[TXn_LDO_CONFIG]==0);
 assert(a[TXn_TX_EMP_POST1_LVL]==0x05 && a[TXn_TX_DRV_LVL]==0x07);
 writes=0;edp.cfg=0;assert(qcom_edp_set_voltages(&edp,&edp.dp_opts)==0 && writes==0);
 assert(sq2_edp_ldo_value(0,5400)==0);
 struct qcom_edp_cfg other={.is_dp=true};assert(sq2_edp_ldo_value(&other,5400)==1);
 assert(sq2_edp_ldo_value(&edp_phy_cfg,5400)==0);
 return 0;
}
'''
        self.assertEqual(result.count('ldo_config = sq2_edp_ldo_value(cfg, edp->dp_opts.link_rate);'), 2)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'signal.c'
            binary = Path(directory) / 'signal'
            source.write_text(harness + tables + setter + cases)
            # Match kernel treatment of the unchanged int-vs-u32 lane loop.
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare',
                            str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
