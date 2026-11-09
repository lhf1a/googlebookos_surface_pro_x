"""Synthetic audit negatives check evidence rejection, not hardware operation."""
import importlib.util
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("audit", ROOT / "scripts/audit-quest2-vpu-hwc.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
SAMPLE = """VPU_ALLOCATION slot=0 id=1 inode=11 time_ns=1
VPU_ALLOCATION slot=1 id=2 inode=12 time_ns=2
VPU_REFILL slot=0 id=1 inode=11 time_ns=3
VPU_EVENT op=QBUF index=0 inode=11 bytes=0 pts=0 flags=0 time_ns=4
VPU_EVENT op=DQBUF index=0 inode=11 bytes=100 pts=0 flags=0 time_ns=5
VPU_HWC phase=ready frame=0 id=1 time_ns=6
VPU_PRESENT frame=0 stage=0 slot=0 id=1 pts=0 cached=0 time_ns=8
VPU_REFILL slot=1 id=2 inode=12 time_ns=9
VPU_EVENT op=QBUF index=1 inode=12 bytes=0 pts=0 flags=0 time_ns=10
VPU_EVENT op=DQBUF index=1 inode=12 bytes=100 pts=10 flags=0 time_ns=11
VPU_HWC phase=ready frame=1 id=2 time_ns=12
FENCE_POINT kind=vpu_release frame=0 slot=0 phase=after status=1 timestamp_ns=14
VPU_RELEASE released_frame=0 replaced_by=1 slot=0 id=1 time_ns=15
VPU_PRESENT frame=1 stage=0 slot=1 id=2 pts=10 cached=0 time_ns=16
VPU_REFILL slot=0 id=1 inode=11 time_ns=17
VPU_EVENT op=QBUF index=0 inode=11 bytes=0 pts=0 flags=0 time_ns=18
FENCE_POINT kind=vpu_release frame=1 slot=1 phase=after status=1 timestamp_ns=19
VPU_RELEASE released_frame=1 replaced_by=2 slot=1 id=2 time_ns=20
VPU_FREE slot=0 id=1 inode=11 time_ns=21
VPU_FREE slot=1 id=2 inode=12 time_ns=22
"""
class VpuHwcAudit(unittest.TestCase):
    def reject(self, text, message):
        with self.assertRaisesRegex(ValueError, message):
            audit.audit_ownership(text)

    def test_completed_outputs_replaced_then_reused_and_freed(self):
        rows = audit.audit_ownership(SAMPLE)
        self.assertEqual([r["inode"] for r in rows], [11, 12])
        self.assertEqual(rows[0]["release_ns"], 15)

    def test_early_application_refill_or_actual_qbuf_is_rejected(self):
        self.reject(SAMPLE.replace("inode=11 time_ns=17", "inode=11 time_ns=13"), "requeue")
        self.reject(SAMPLE.replace("flags=0 time_ns=18", "flags=0 time_ns=13"), "requeue")

    def test_missing_or_wrong_completed_output_is_rejected(self):
        self.reject(SAMPLE.replace("bytes=100 pts=10", "bytes=0 pts=10"), "completion")
        self.reject(SAMPLE.replace("bytes=100 pts=10", "bytes=100 pts=9"), "completion")
        self.reject(SAMPLE.replace("bytes=100 pts=10 flags=0", "bytes=100 pts=10 flags=64"), "completion")

    def test_wrong_or_unsignaled_release_is_rejected(self):
        self.reject(SAMPLE.replace("replaced_by=1 slot=0 id=1", "replaced_by=1 slot=0 id=2"), "different frame")
        self.reject(SAMPLE.replace("status=1 timestamp_ns=14", "status=-5 timestamp_ns=14"), "completed replacement")

    def test_free_before_release_or_missing_free_is_rejected(self):
        self.reject(SAMPLE.replace("inode=11 time_ns=21", "inode=11 time_ns=13"), "freed before")
        self.reject(SAMPLE.replace("VPU_FREE slot=0 id=1 inode=11 time_ns=21", ""), "freed exactly once")


class SnapshotAudit(unittest.TestCase):
    def test_only_observed_flush_boundary_accepts_empty_or_bootstrap(self):
        self.assertEqual(audit.snapshot_kind([], 10, {10}), "empty-flush")
        self.assertEqual(audit.snapshot_kind(["format=AB24 modifier=0x0"], 10, {10}),
                         "bootstrap-replacement")
        self.assertEqual(audit.snapshot_kind(["format=NV12 modifier=0x0"], 9, {10}), "video")
        for planes, count in [([], 9), (["format=AB24 modifier=0x0"], 9),
                              (["format=XR24 modifier=0x0"], 10),
                              (["format=NV12 modifier=0x1"], 9),
                              (["format=NV12 modifier=0x0"] * 2, 10)]:
            with self.assertRaises(ValueError):
                audit.snapshot_kind(planes, count, {10})
