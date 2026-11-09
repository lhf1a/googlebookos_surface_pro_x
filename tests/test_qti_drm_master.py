"""Compile the complete real DRMMaster source and exercise its libdrm error boundary."""
import os, shutil, subprocess, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/"out/qualcomm-full-stack-20261108"
SOURCE=Path(os.environ.get("QTI_DISPLAY_SOURCE",str(BASE/"sm8350-display-v9/source")))
DRM=Path(os.environ.get("DRM_HEADERS","/mnt/spx-workspace/toolchains/sq2-graphics/src/mesa-26.2.4/subprojects/libdrm-2.4.133"))
class DrmMaster(unittest.TestCase):
    @unittest.skipUnless((SOURCE/"libdrmutils/drm_master.cpp").exists(),"requires actual pinned QTI source")
    def test_actual_device_discovery_and_reference_release(self):
        with tempfile.TemporaryDirectory() as temporary:
            temp=Path(temporary);source=temp/"libdrmutils";source.mkdir()
            for name in ("drm_master.cpp","drm_master.h","drm_logger.h"):
                shutil.copy2(SOURCE/"libdrmutils"/name,source/name)
            if "drmGetDevices2" not in (source/"drm_master.cpp").read_text():
                subprocess.run(["patch","--batch","--fuzz=0","-p1","-i",str(ROOT/"patches/sm8350-drm-device-lifecycle.patch")],cwd=temp,check=True,stdout=subprocess.PIPE)
            cpp=temp/"test.cpp";binary=temp/"test"
            cpp.write_text((ROOT/"tests/qti_drm_master_test.cpp.in").read_text().replace("@SOURCE@",str(source/"drm_master.cpp")))
            subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-pie","-no-pie",
                            "-I"+str(DRM),"-I"+str(DRM/"include/drm"),"-I"+str(BASE/"sm8350-reference-v1/kernel-uapi"),
                            str(cpp),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
if __name__=="__main__":unittest.main()
