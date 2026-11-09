"""Mutation checks over real captured errors; no synthetic hardware claims."""
from pathlib import Path
import importlib.util
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/"out/qualcomm-full-stack-20261108"
CAPTURE=BASE/"composer-fence-errors-v3/capture"
spec=importlib.util.spec_from_file_location("error_audit",ROOT/"scripts/audit-sq2-composer-fence-errors.py")
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)

@unittest.skipUnless((CAPTURE/"probe.txt").exists(),"requires private real error capture")
class FenceErrors(unittest.TestCase):
    def run_mutation(self,name,change,reason):
        with tempfile.TemporaryDirectory() as directory:
            target=Path(directory)
            for path in CAPTURE.iterdir():
                if path.is_file():(target/path.name).symlink_to(path)
            file=target/name;original=file.read_text();file.unlink();file.write_text(change(original))
            with self.assertRaisesRegex(ValueError,reason):audit.audit(target,BASE/"sm8350-composer-v22")
    def test_baseline(self):
        result=audit.audit(CAPTURE,BASE/"sm8350-composer-v22")
        self.assertEqual([x["point_status"] for x in result["kernel_cases"]],[-2,1,-2])
    def test_invalid_fd_success_rejected(self):
        self.run_mutation("probe.txt",lambda s:s.replace("presents=0 rejected=1","presents=1 rejected=0",1),"Invalid FD")
    def test_wrong_error_code_rejected(self):
        self.run_mutation("probe.txt",lambda s:s.replace("COMMAND_ERROR location=6 error=4","COMMAND_ERROR location=6 error=6",1),"BAD_PARAMETER")
    def test_error_status_not_normalized_to_success(self):
        self.run_mutation("probe.txt",lambda s:s.replace("point_status=-2","point_status=1",1),"error/status")
    def test_drm_mutation_rejected(self):
        self.run_mutation("invalid-after-drm.txt",lambda s:s+"unexpected-state-change","DRM state")
    def test_trace_overrun_rejected(self):
        self.run_mutation("trace-stats.txt",lambda s:s.replace("overrun: 0","overrun: 1",1),"Trace loss")
