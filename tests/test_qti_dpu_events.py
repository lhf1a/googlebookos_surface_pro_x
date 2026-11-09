"""Execute actual QTI DRM event functions under fd/thread/vblank fault injection."""
from pathlib import Path
import os,re,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get("QTI_DPU_SOURCE",str(ROOT/"out/qualcomm-full-stack-20261108/dpu-events-work/source")))
def block(source,start):
    a=source.index(start);left=source.index("{",a);depth=1;i=left+1
    while depth:
        if source[i]=="{":depth+=1
        elif source[i]=="}":depth-=1
        i+=1
    return source[a:i]
class DpuEvents(unittest.TestCase):
    @unittest.skipUnless((SOURCE/"sdm/libs/core/drm/hw_events_drm.cpp").exists(),"requires patched source")
    def test_actual_events_failure_lifecycle(self):
        source=(SOURCE/"sdm/libs/core/drm/hw_events_drm.cpp").read_text()
        header=(SOURCE/"sdm/libs/core/drm/hw_events_drm.h").read_text()
        cls=block(header,"class HWEventsDRM")+";"
        cls=cls.replace(" private:"," public:")
        functions=[]
        for name in ["OpenDrmEventFd","InitializePollFd","SetEventParser","PopulateHWEventData",
                     "Init","Deinit","SetEventState","WakeUpEventThread","CloseFds",
                     "DisplayEventThread","DisplayEventHandler","RegisterVSync","HandleVSync",
                     "KernelVSyncHandlerCallback","VSyncHandlerCallback"]:
            match=re.search(r"^(?:DisplayError|int|void \*|void) ?HWEventsDRM::"+name+r"\(",source,re.M)
            self.assertIsNotNone(match,name)
            functions.append(block(source,match.group(0)))
        text=(ROOT/"tests/qti_dpu_events_test.cpp.in").read_text()
        text=text.replace("@CLASS@",cls).replace("@FUNCTIONS@","\n\n".join(functions))
        with tempfile.TemporaryDirectory() as temp:
            cpp=Path(temp)/"test.cpp";binary=Path(temp)/"test";cpp.write_text(text)
            subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-Wno-unused-parameter",
                            "-fsanitize=address,undefined","-fno-pie","-no-pie","-pthread",
                            str(cpp),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=15,
                           env=dict(os.environ,ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                                    UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
if __name__=="__main__":unittest.main()
