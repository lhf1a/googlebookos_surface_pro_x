"""Compile the actual DPU reader against the source QTI interface under sanitizers."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/"out/qualcomm-full-stack-20261108"
SOURCE=Path(os.environ.get("QTI_DPU_SOURCE",str(BASE/"sm8350-display-v5/source")))
REF=BASE/"sm8350-reference-v1"
CATALOG=ROOT/"out/quest2-video-618-20261107/uncached-native/source/external/edp-618/drivers/gpu/drm/msm/disp/dpu1/dpu_hw_catalog.c"
DRM=Path(os.environ.get("DRM_HEADERS","/mnt/spx-workspace/toolchains/sq2-graphics/src/mesa-26.2.4/subprojects/libdrm-2.4.133"))

class Capabilities(unittest.TestCase):
    @unittest.skipUnless(CATALOG.exists(),"requires pinned kernel catalog")
    @unittest.skipUnless((SOURCE/"libdrmutils/drm_interface.h").exists(),"requires patched source interface")
    def test_actual_reader_bounds_and_kernel_contract(self):
        with tempfile.TemporaryDirectory() as temp:
            output=Path(temp)/"caps"
            macros=re.findall(r"^#define SSPP_SCALER_VER\([^\n]+$",CATALOG.read_text(),re.MULTILINE)
            self.assertEqual(len(macros),1)
            catalog_header=Path(temp)/"catalog-version.h"
            catalog_header.write_text(macros[0]+"\n")
            includes=[ROOT/"probe/graphics",SOURCE/"libdrmutils",REF/"kernel-uapi",
                      REF/"kernel-uapi/drm",DRM,DRM/"include/drm"]
            command=["g++","-include",str(catalog_header),"-std=c++17","-O1","-Wall","-Wextra","-Werror",
                     "-D__user=","-Wno-missing-field-initializers","-fsanitize=address,undefined","-fno-pie","-no-pie"]
            for p in includes:command+=["-I",str(p)]
            command += [str(ROOT/"tests/qti_dpu_caps_test.cpp"),"-o",str(output)]
            subprocess.run(command,check=True)
            # Compile the actual kernel emitter with the existing DRM ownership fixture.
            emitter_header=Path(temp)/"dpu_caps.h"
            text=(ROOT/"probe/graphics/dpu-caps.h").read_text()
            emitter_header.write_text("\n".join(line for line in text.splitlines()
                                              if not line.startswith("#include"))+"\n")
            fixture=(ROOT/"tests/dpu_caps_emit_test.c").read_text()
            # The generic emitter test uses an arbitrary ratio; use the real driver FRAC here.
            fixture=fixture.replace("3277","3276")
            fixture=fixture.replace('assert(strstr(captured,"physical_pipe=1',
                                    'printf("PLANE_BEGIN\\n%sPLANE_END\\n", captured);\n    assert(strstr(captured,"physical_pipe=1',1)
            fixture=fixture.replace('assert(strstr(captured,"hw_major=5',
                                    'printf("CRTC_BEGIN\\n%sCRTC_END\\n", captured);\n    assert(strstr(captured,"hw_major=5',1)
            emitter_source=Path(temp)/"emit.c";emitter_source.write_text(fixture)
            emitter=Path(temp)/"emit"
            subprocess.run(["gcc","-include",str(catalog_header),"-std=c11","-Wall","-Wextra","-Werror","-Wno-unused-parameter",
                            "-fsanitize=address,undefined","-fno-pie","-no-pie",
                            '-DDPU_CAPS_HEADER="'+str(emitter_header)+'"',str(emitter_source),
                            "-o",str(emitter)],check=True)
            emitted=subprocess.run([str(emitter)],text=True,capture_output=True,check=True).stdout
            env=os.environ.copy()
            env["DPU_CAPS_PLANE"]=emitted.split("PLANE_BEGIN\n",1)[1].split("PLANE_END\n",1)[0]
            env["DPU_CAPS_CRTC"]=emitted.split("CRTC_BEGIN\n",1)[1].split("CRTC_END\n",1)[0]
            subprocess.run([str(output)],env=env,check=True)

if __name__=="__main__":
    unittest.main()
