"""Exercise actual Commit code and output-fence ownership across transient DRM errors."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from tests.test_qti_dpu_empty_flush import block
from tests.test_qti_dpu_output_fence import DRM
ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "out/qualcomm-full-stack-20261108"
SOURCE = Path(os.environ.get("QTI_BUSY_SOURCE", str(BASE / "dpu-busy-commit-v1/build/source/sde-drm/drm_atomic_req.cpp")))
class BusyCommit(unittest.TestCase):
    def check_source(self, source, original=False):
        fixtures = (ROOT / "tests/qti_dpu_output_fence_test.cpp").read_text().split("int main() {")[0]
        fixtures = fixtures.replace("static bool closed(int fd) { errno=0; return fcntl(fd,F_GETFD)==-1 && errno==EBADF; }", "")
        template = (ROOT / "tests/qti_dpu_busy_commit_test.cpp.in").read_text()
        code = fixtures + template.replace("@COMMIT@", block(source.read_text(), "int DRMAtomicReq::Commit("))
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/"test.cpp"; binary=Path(directory)/"test"
            cpp.write_text(code)
            subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",
                            "-fsanitize=address,undefined","-fno-pie","-no-pie",
                            "-I"+str(DRM),"-I"+str(DRM/"include/drm"),
                            "-I"+str(ROOT/"probe/graphics"),str(cpp),"-o",str(binary)],check=True)
            result=subprocess.run([str(binary)],text=True,capture_output=True,timeout=10)
            self.assertEqual(result.returncode,3 if original else 0,result.stdout+result.stderr)
            print(result.stdout.strip())
    def test_original_busy_commit_propagates_to_flush(self):
        self.check_source(BASE/"sm8350-display-v20/source/sde-drm/drm_atomic_req.cpp",True)
    def test_retry_fences_errors_and_legacy(self):
        self.check_source(SOURCE)
if __name__=="__main__":unittest.main()
