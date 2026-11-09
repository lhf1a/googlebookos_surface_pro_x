"""Exercise real output-fence width conversion, fd transfer, and rollback."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
DRM = Path(os.environ.get("DRM_HEADERS", "/mnt/spx-workspace/toolchains/sq2-graphics/src/mesa-26.2.4/subprojects/libdrm-2.4.133"))
class OutputFences(unittest.TestCase):
    def test_actual_helper_fd_ownership_and_width(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary=Path(tmp)/"test"
            subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",
                            "-fsanitize=address,undefined","-fno-pie","-no-pie",
                            "-I"+str(DRM),"-I"+str(DRM/"include/drm"),
                            "-I"+str(ROOT/"probe/graphics"),
                            str(ROOT/"tests/qti_dpu_output_fence_test.cpp"),
                            "-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
if __name__=="__main__":unittest.main()
