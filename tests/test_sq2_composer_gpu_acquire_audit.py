"""Reject corrupted ownership/trace evidence from real GPU/HWC captures."""
from pathlib import Path
import importlib.util
import shutil
import tempfile
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/"out/qualcomm-full-stack-20261108"
spec=importlib.util.spec_from_file_location("gpu_audit",ROOT/"scripts/audit-sq2-composer-gpu-acquire.py")
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
FIXED=BASE/"composer-gpu-acquire-v2/capture"
BASELINE=BASE/"composer-gpu-acquire-v1/capture"

@unittest.skipUnless((FIXED/"fence-trace.txt").exists(),"requires local actual GPU capture")
class GpuAcquireAudit(unittest.TestCase):
 def mutate(self,name,callback):
  with tempfile.TemporaryDirectory() as directory:
   temp=Path(directory)
   for source in FIXED.iterdir():
    if source.is_file() and source.suffix in (".txt",".exit"):shutil.copy2(source,temp/source.name)
   path=temp/name;path.write_text(callback(path.read_text()))
   return audit.audit(temp)
 def test_actual_fixed_capture(self):
  result=audit.audit(FIXED)
  self.assertTrue(result["inherited_plane_handoff_verified"])
  self.assertEqual(result["frames"],30)
  self.assertEqual(result["cached_reuses"],27)
  self.assertGreater(result["pending_GPU_signals_during_display_wait"],0)
 def test_inherited_plane_is_not_accepted(self):
  if not BASELINE.exists():self.skipTest("requires baseline capture")
  with self.assertRaisesRegex(ValueError,"Inherited"):
   audit.audit(BASELINE)
  self.assertFalse(audit.audit(BASELINE,True)["inherited_plane_handoff_verified"])
 def test_negative_acquire_status(self):
  with self.assertRaisesRegex(ValueError,"Negative fence"):
   self.mutate("probe.txt",lambda t:t.replace("status=0 points=1","status=-5 points=1",1))
 def test_missing_frame(self):
  with self.assertRaisesRegex(ValueError,"Missing GPU frames"):
   self.mutate("probe.txt",lambda t:"\n".join(x for x in t.splitlines() if not x.startswith("GPU_FRAME frame=17 ")))
 def test_live_target_reuse(self):
  def early(t):
   return "\n".join(x[:x.index("time_ns=")]+"time_ns=1" if x.startswith("GPU_REUSE frame=3 ") else x for x in t.splitlines())
  with self.assertRaisesRegex(ValueError,"live scanout"):
   self.mutate("probe.txt",early)
 def test_marker_buffer_identity(self):
  with self.assertRaisesRegex(ValueError,"wrong AHB"):
   self.mutate("fence-trace.txt",lambda t:t.replace("phase=present_begin frame=0 slot=0","phase=present_begin frame=0 slot=1",1))
 def test_missing_phase_marker(self):
  with self.assertRaisesRegex(ValueError,"phase markers"):
   self.mutate("fence-trace.txt",lambda t:"\n".join(x for x in t.splitlines() if "phase=present_begin frame=17 " not in x))
 def test_trace_loss(self):
  with self.assertRaisesRegex(ValueError,"loss/overrun"):
   self.mutate("trace-stats.txt",lambda t:t.replace("overrun: 0","overrun: 1",1))
 def test_mismatched_display_library_pair(self):
  with tempfile.TemporaryDirectory() as directory:
   output=Path(directory)/"invalid.json"
   result=subprocess.run(["python3",str(ROOT/"scripts/audit-sq2-composer-gpu-acquire.py"),str(FIXED),
      "--composer-build",str(BASE/"sm8350-composer-v19"),
      "--display-build",str(BASE/"sm8350-display-v18"),"--output",str(output)],text=True,capture_output=True)
   self.assertNotEqual(result.returncode,0)
   self.assertIn("Actual display libraries do not match",result.stderr)
   self.assertFalse(output.exists())
 def test_global_trace_mutation(self):
  with self.assertRaisesRegex(ValueError,"Global tracing"):
   self.mutate("global-trace-after.txt",lambda t:t+"\nchanged\n")

if __name__=="__main__":unittest.main()
