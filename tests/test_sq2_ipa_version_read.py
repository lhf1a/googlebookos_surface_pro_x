import re,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class Probe(unittest.TestCase):
 def test_actual_decoder(self):
  with tempfile.TemporaryDirectory() as td:
   exe=Path(td)/'test'
   subprocess.run(['cc','-Wall','-Wextra','-Werror',str(ROOT/'tests/sq2_ipa_version_test.c'),'-o',str(exe)],check=True)
   r=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
   self.assertIn('PASS: version bit slices',r.stdout)
 def test_bounded_read_policy(self):
  s=(ROOT/'probe/test2/sq2-ipa-version-read.c').read_text()
  s=re.sub(r'/\*.*?\*/|//[^\n]*','',s,flags=re.S)
  self.assertEqual(len(re.findall(r'\breadl\s*\(',s)),1)
  self.assertIn('#define SQ2_IPA_VERSION_PHYS 0x01e40030UL',s)
  self.assertFalse(re.search(r'\b(?:writel|writeb|writew|writeq|dma_alloc_coherent|dma_map_single|request_irq|reset_control_assert)\s*\(',s))
  self.assertLess(s.index('if (!confirm_lab)'),s.index('of_clk_get_from_provider(&args)'))
  self.assertLess(s.index('clk_prepare_enable(core)'),s.index('version_raw = readl(reg)'))
  self.assertLess(s.index('version_raw = readl(reg)'),s.index('clk_disable_unprepare(core)'))
  self.assertIn('clk_put(core)',s)
 def test_rollback_resources_and_owner_guard(self):
  s=(ROOT/'probe/test2/sq2-ipa-version-read.c').read_text()
  self.assertIn('bus_for_each_dev(&platform_bus_type',s)
  self.assertIn('strcmp(dev->driver->name, "ipa")',s)
  self.assertIn('clk_set_rate(core, rate_before)',s)
  self.assertIn('iounmap(reg)',s)
if __name__=='__main__':unittest.main()
