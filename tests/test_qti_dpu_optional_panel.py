"""Run the real SDM init/feature/cleanup functions with failure injection."""
from pathlib import Path
import os,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get("QTI_DISPLAY_SOURCE",str(ROOT/"out/qualcomm-full-stack-20261108/sm8350-display-v11/source")))
def block(source,start):
    a=source.index(start);left=source.index("{",a);depth=1;i=left+1
    while depth:
        if source[i]=="{":depth+=1
        elif source[i]=="}":depth-=1
        i+=1
    return source[a:i]
class OptionalPanel(unittest.TestCase):
    @unittest.skipUnless((SOURCE/"sdm/libs/core/display_builtin.cpp").exists(),"requires actual patched display source")
    def test_real_init_optional_feature_and_cleanup(self):
        builtin=(SOURCE/"sdm/libs/core/display_builtin.cpp").read_text()
        base=(SOURCE/"sdm/libs/core/display_base.cpp").read_text()
        text=(ROOT/"tests/qti_dpu_optional_panel_test.cpp.in").read_text()
        for marker,name,source in [
          ("@BASE_DEINIT@","DisplayBase::Deinit",base),("@SETUP_RC@","DisplayBase::SetupRC",base),
          ("@BUILTIN_INIT@","DisplayBuiltIn::Init",builtin),("@CREATE_PANEL@","DisplayBuiltIn::CreatePanelfeatures",builtin),
          ("@SETUP_SPR@","DisplayBuiltIn::SetupSPR",builtin),("@SETUP_DEMURA@","DisplayBuiltIn::SetupDemura",builtin),
          ("@SETUP_PANEL@","DisplayBuiltIn::SetupPanelfeatures",builtin)]:
            text=text.replace(marker,block(source,"DisplayError "+name+"()"))
        with tempfile.TemporaryDirectory() as temporary:
            cpp=Path(temporary)/"test.cpp";binary=Path(temporary)/"test";cpp.write_text(text)
            subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-Wno-misleading-indentation",
                            "-fsanitize=address,undefined","-fno-pie","-no-pie",str(cpp),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
if __name__=="__main__":unittest.main()
