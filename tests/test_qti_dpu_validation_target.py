"""Actual source first-frame TEST_ONLY storage, isolation, cleanup, and final target refresh."""
from pathlib import Path
import os,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get("QTI_DISPLAY_SOURCE",str(ROOT/"out/qualcomm-full-stack-20261108/dpu-validation-target-work/source")))
def block(source,start):
    a=source.index(start);left=source.index("{",a);depth=1;i=left+1
    while depth:
        if source[i]=="{":depth+=1
        elif source[i]=="}":depth-=1
        i+=1
    return source[a:i]
class ValidationTarget(unittest.TestCase):
    def test_actual_validate_and_commit_never_present_scratch(self):
        hw=(SOURCE/"sdm/libs/core/drm/hw_device_drm.cpp").read_text()
        base=(SOURCE/"sdm/libs/core/display_base.cpp").read_text()
        text=(ROOT/"tests/qti_dpu_validation_target_test.cpp.in").read_text()
        for marker,start,source in [
          ("@VALIDATE_LAYERS@","DisplayError HWDeviceDRM::ValidateDpuLayers(",hw),
          ("@VALIDATE@","DisplayError HWDeviceDRM::Validate(",hw),("@COMMIT@","DisplayError HWDeviceDRM::Commit(",hw),
          ("@COMMIT_PARAMS@","void DisplayBase::CommitLayerParams(",base)]:text=text.replace(marker,block(source,start))
        with tempfile.TemporaryDirectory() as temp:
            cpp=Path(temp)/"test.cpp";binary=Path(temp)/"test";cpp.write_text(text)
            subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-Wno-misleading-indentation",
              "-fsanitize=address,undefined","-fno-pie","-no-pie",str(cpp),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
if __name__=="__main__":unittest.main()
