from pathlib import Path
import subprocess,tempfile,unittest,importlib.util
R=Path(__file__).resolve().parents[1]
class SystemPM(unittest.TestCase):
 def compile_run(self,name):
  with tempfile.TemporaryDirectory() as td:
   out=Path(td)/'test'
   p=subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',str(R/'tests'/name),'-o',str(out)],capture_output=True)
   self.assertEqual(p.returncode,0,p.stderr.decode())
   p=subprocess.run([str(out)],capture_output=True)
   self.assertEqual(p.returncode,0,p.stdout.decode()+p.stderr.decode())
 def test_actual_transaction_state_code(self):self.compile_run('sq2_venus_system_pm_test.c')
 def test_actual_power_owner_code_and_retained_domain_handles(self):self.compile_run('sq2_venus_power_test.c')
 def test_source_contract_stop_before_free_and_clean_restart_only(self):
  s=(R/'probe/video/venus-sq2-system-pm.h').read_text()
  stop=s[s.index('static int venus_sq2_pm_stop'):s.index('static int venus_sq2_pm_start')]
  self.assertLess(stop.index('venus_shutdown(core)'),stop.index('hfi_destroy(core)'))
  self.assertLess(stop.index('hfi_destroy(core)'),stop.index('sq2_venus_platform_system_quiet'))
  self.assertIn('sq2_vb2_bytes',s);self.assertIn('list_empty(&core->instances)',s)
  self.assertIn('SQ2_PM_BROKEN',s);self.assertIn('core->sq2_boot_attempted = false',s)
  p=(R/'probe/video/sq2-venus-power.c').read_text()
  system=p[p.index('int sq2_venus_power_system_quiet'):]
  for forbidden in ['dev_pm_domain_detach','pm_runtime_put','clk_put','kfree(']:
   self.assertNotIn(forbidden,system)
  self.assertIn('sq2_system_pm_image',(R/'probe/video/sq2-venus-fw.c').read_text())
 def test_busy_retry_is_limited_to_untouched_stop_and_irq_restored(self):
  s=(R/'probe/video/venus-sq2-system-pm.h').read_text()
  stop=s[s.index('static int venus_sq2_pm_stop'):s.index('static int venus_sq2_pm_start')]
  self.assertIn('if (!ret) return SQ2_PM_STOP_UNCHANGED_BUSY;',stop)
  self.assertLess(stop.index('stop:busy-irq-restore'),stop.index('SQ2_PM_STOP_UNCHANGED_BUSY'))
  self.assertLess(stop.index('SQ2_PM_STOP_UNCHANGED_BUSY'),stop.index('venus_shutdown(core)'))
  self.assertNotIn('clear_bit',s)
  patch=(R/'scripts/patch-sq2-venus-system-pm.py').read_text()
  self.assertIn('READ_ONCE(core->sq2_system_pm.phase) != SQ2_PM_RUNNING',patch)
  self.assertIn('module_param_cb(sq2_pm_status, &sq2_pm_status_ops, NULL, 0400)',s)
  for stage in ['stop:firmware-shutdown','stop:hfi-deinit','stop:power-quiet','start:power-restore','start:hfi-init']:
   self.assertIn(stage,s)
 def test_flag_requires_full_independent_owner(self):
  with tempfile.TemporaryDirectory() as td:
   p=subprocess.run(['python3',str(R/'scripts/prepare-sq2-venus-iris1.py'),'--system-sleep','--kernel',td+'/reference-source','--output',td+'/x'],capture_output=True)
   self.assertEqual(p.returncode,2);self.assertIn(b'requires firmware-lease',p.stderr)
if __name__=='__main__':unittest.main()
