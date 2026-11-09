"""Fault injection executes patched HWC init/deinit and real factory/service main."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/"out/qualcomm-full-stack-20261108"
SOURCE=Path(os.environ.get("QTI_COMPOSER_SOURCE",str(BASE/"sm8350-composer-v6/source")))
def block(source,start):
    a=source.index(start);left=source.index("{",a);depth=1;i=left+1
    while depth:
        if source[i]=="{":depth+=1
        elif source[i]=="}":depth-=1
        i+=1
    return source[a:i]
class HwcInitialization(unittest.TestCase):
    @unittest.skipUnless((SOURCE/"composer/hwc_session.cpp").exists(),"requires actual QTI composer source")
    def test_actual_init_errors_never_register_bad_service(self):
        with tempfile.TemporaryDirectory() as temp:
            temp=Path(temp);source=(SOURCE/"composer/hwc_session.cpp").read_text()
            if "int HWCSession::InitSupportedDisplaySlots()" not in source:
                (temp/"composer").mkdir()
                for name in ["hwc_session.cpp","hwc_session.h"]:
                    shutil.copy2(SOURCE/"composer"/name,temp/"composer"/name)
                subprocess.run(["patch","--forward","--batch","--fuzz=0","-p1","-i",
                                str(ROOT/"patches/sm8350-hwc-session-init.patch")],
                               cwd=temp,check=True,stdout=subprocess.PIPE)
                source=(temp/"composer/hwc_session.cpp").read_text()
            if "leave the init lock first" not in source:
                (temp/"composer").mkdir(exist_ok=True)
                (temp/"composer/hwc_session.cpp").write_text(source)
                subprocess.run(["patch","--forward","--batch","--fuzz=0","-p1","-i",
                                str(ROOT/"patches/sm8350-hwc-primary-cleanup.patch")],
                               cwd=temp,check=True,stdout=subprocess.PIPE)
                source=(temp/"composer/hwc_session.cpp").read_text()
            text=(ROOT/"tests/sm8350_hwc_session_init_test.cpp.in").read_text()
            text=text.replace("@LOCKER@",(SOURCE/"sdm/include/utils/locker.h").read_text())
            for marker,start in [
                ("@INIT_SLOTS@","int HWCSession::InitSupportedDisplaySlots()"),
                ("@INIT_NULL_SLOTS@","void HWCSession::InitSupportedNullDisplaySlots()"),
                ("@DEINIT@","int HWCSession::Deinit()"),
                ("@SESSION_INIT@","int HWCSession::Init()"),
                ("@CREATE_PRIMARY@","int HWCSession::CreatePrimaryDisplay()")]:
                text=text.replace(marker,block(source,start))
            text=text.replace("@FACTORY@",block((SOURCE/"composer/QtiComposer.cpp").read_text(),
                                               "QtiComposer *QtiComposer::initialize()"))
            service_source=(SOURCE/"composer/service.cpp").read_text()
            if 'initWithDriver("/dev/vndbinder")' in service_source:
                (temp/"composer").mkdir(exist_ok=True)
                (temp/"composer/service.cpp").write_text(service_source)
                subprocess.run(["patch","--batch","--fuzz=0","-p1","-i",
                                str(ROOT/"patches/sm8350-composer-system-binder.patch")],
                               cwd=temp,check=True,stdout=subprocess.PIPE)
                service_source=(temp/"composer/service.cpp").read_text()
            service=block(service_source,"int main(int, char **)")
            text=text.replace("@SERVICE_MAIN@",service.replace("int main(","int ServiceMain(",1))
            cpp=temp/"test.cpp";binary=temp/"test";cpp.write_text(text)
            subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",
                            "-fsanitize=address,undefined","-fno-pie","-no-pie","-pthread",
                            str(cpp),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10,
                           env=dict(os.environ,ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                                    UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
if __name__=="__main__":unittest.main()
