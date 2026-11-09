"""Fence identity/lifetime parser rejects loss and impossible ownership ordering."""
import importlib.util
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("audit",ROOT/"scripts/audit-sq2-dpu-fence-trace.py")
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)

def event(t,kind,driver,timeline,context,seq):
 return f" HwBinder:1_1-12 [003] ..... 1.{t:06d}: dma_fence_{kind}: driver={driver} timeline={timeline} context={context} seqno={seq}"

def valid():
 return [
  event(1,"init","msm","CRTC:17-crtc-0",3,10),
  event(2,"init","msm-dpu","dpu-video-buffer-release",4,7),
  event(3,"signaled","msm","CRTC:17-crtc-0",3,10),
  event(4,"init","msm","CRTC:17-crtc-0",3,11),
  event(5,"init","msm-dpu","dpu-video-buffer-release",4,8),
  event(6,"signaled","msm-dpu","dpu-video-buffer-release",4,7),
  event(7,"signaled","msm","CRTC:17-crtc-0",3,11),
  event(8,"destroy","detached-driver","signaled-timeline",3,10),
  event(9,"destroy","detached-driver","signaled-timeline",4,7)]

class FenceTrace(unittest.TestCase):
 def test_identity_survives_detached_names_and_pending_tail(self):
  result=audit.analyze_fences(audit.parse_events("\n".join(valid())))
  self.assertEqual(result["creation_adjacent_same_task_pairs"],2)
  self.assertEqual(result["tracked_destroyed"],2)
  self.assertEqual(result["pending_release_at_trace_end"][0]["seqno"],8)
  self.assertEqual(result["pairs"][0]["release"]["events"]["dma_fence_destroy"]["driver_at_event"],"detached-driver")
 def test_early_release_and_early_destroy_rejected(self):
  for replacement in [event(0,"signaled","msm-dpu","dpu-video-buffer-release",4,7),
                      event(0,"destroy","detached-driver","signaled-timeline",4,7)]:
   rows=valid();rows[5 if "signaled:" in replacement else 8]=replacement
   with self.assertRaises(ValueError):audit.analyze_fences(audit.parse_events("\n".join(rows)))
 def test_duplicate_identity_and_creation_task_mismatch_rejected(self):
  rows=valid();rows.insert(1,rows[0])
  with self.assertRaises(ValueError):audit.analyze_fences(audit.parse_events("\n".join(rows)))
  rows=valid();rows[1]=rows[1].replace("-12 [","-13 [")
  with self.assertRaises(ValueError):audit.analyze_fences(audit.parse_events("\n".join(rows)))
 def test_trace_loss_and_unknown_format_rejected(self):
  stats="TRACE_CPU_STATS=cpu0\nentries: 9\noverrun: 0\ncommit overrun: 0\ndropped events: 0\n"
  self.assertEqual(audit.parse_stats(stats)["cpu0"]["entries"],9)
  for field in ["overrun","commit overrun","dropped events"]:
   with self.assertRaises(ValueError):audit.parse_stats(stats.replace(field+": 0",field+": 1"))
  with self.assertRaises(ValueError):audit.parse_events(" worker-1 [000] ..... 1.0: dma_fence_init: unknown-format")
 def test_actual_GPU_signal_is_joined_to_display_wait_stack(self):
  rows=[event(1,"init","drm_sched","ring0",10,99),
        event(3,"wait_start","drm_sched","ring0",10,99),
        " HwBinder:1_1-12 [003] ..... 1.000004: <stack trace>",
        " => dma_fence_wait_timeout"," => drm_atomic_helper_wait_for_fences",
        " => commit_tail"," => commit_work",
        event(6,"signaled","drm_sched","ring0",10,99),
        event(7,"wait_end","detached-driver","signaled-timeline",10,99)]
  text="\n".join(rows)
  result=audit.analyze_display_waits(text,audit.parse_events(text))
  self.assertEqual(len(result["pending_GPU_display_waits"]),1)
  unrelated=text.replace("drm_atomic_helper_wait_for_fences","msm_ioctl_wait_fence")
  self.assertFalse(audit.analyze_display_waits(unrelated,audit.parse_events(unrelated))["pending_GPU_display_waits"])
 def test_already_signaled_wait_is_not_pending_acquire_proof(self):
  rows=[event(1,"init","drm_sched","ring0",10,99),event(2,"signaled","drm_sched","ring0",10,99),
        event(3,"wait_start","detached-driver","signaled-timeline",10,99),
        " HwBinder:1_1-12 [003] ..... 1.000004: <stack trace>",
        " => drm_atomic_helper_wait_for_fences"," => commit_tail"," => commit_work",
        event(7,"wait_end","detached-driver","signaled-timeline",10,99)]
  text="\n".join(rows);result=audit.analyze_display_waits(text,audit.parse_events(text))
  self.assertEqual(len(result["display_commit_waits"]),1)
  self.assertFalse(result["pending_GPU_display_waits"])
if __name__=="__main__":unittest.main()
