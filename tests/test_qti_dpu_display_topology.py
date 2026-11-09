"""Actual connector topology helper with real libdrm structures and failure cleanup."""
import os, subprocess, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
DRM=Path(os.environ.get("DRM_HEADERS","/mnt/spx-workspace/toolchains/sq2-graphics/src/mesa-26.2.4/subprojects/libdrm-2.4.133"))
class DisplayTopology(unittest.TestCase):
    def test_actual_routing_capacity_and_read_failures(self):
        with tempfile.TemporaryDirectory() as temp:
            binary=Path(temp)/"test"
            subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-pie","-no-pie",
                            "-I"+str(DRM),"-I"+str(DRM/"include/drm"),"-I"+str(ROOT/"probe/graphics"),
                            str(ROOT/"tests/qti_dpu_display_topology_test.cpp"),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
if __name__=="__main__":unittest.main()
