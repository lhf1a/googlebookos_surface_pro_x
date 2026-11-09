"""Execute production kernel-managed operation/color contracts and actual SDM preflight."""
from pathlib import Path
import os,subprocess,tempfile,unittest,shutil
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/"out/qualcomm-full-stack-20261108"
SOURCE=Path(os.environ.get("QTI_DPU_SOURCE",str(BASE/"sm8350-display-contract-v2/source")))
REF=BASE/"sm8350-reference-v1"
DRM=Path(os.environ.get("DRM_HEADERS","/mnt/spx-workspace/toolchains/sq2-graphics/src/mesa-26.2.4/subprojects/libdrm-2.4.133"))
def block(source,start):
    a=source.index(start);left=source.index("{",a);depth=1;i=left+1
    while depth:
        if source[i]=="{":depth+=1
        elif source[i]=="}":depth-=1
        i+=1
    return source[a:i]

def prepared_device_source(temp):
    actual=(SOURCE/"sdm/libs/core/drm/hw_device_drm.cpp").read_text()
    final_markers=[
        "DisplayError HWDeviceDRM::Registry::Register(",
        "int HWDeviceDRM::Registry::MapBufferToFbId(",
        "DisplayError HWDeviceDRM::ValidateDpuLayers(",
        "uint64_t *drm_format_modifier, bool kernel_managed_dpu)"]
    if all(marker in actual for marker in final_markers):
        return actual
    # Support the original unpatched baseline without reversing an existing patch.
    patch=ROOT/"patches/sm8350-dpu-sdm-contract.patch"
    names=[line[6:] for line in patch.read_text().splitlines() if line.startswith("+++ b/")]
    for name in names:
        target=Path(temp)/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(SOURCE/name,target)
    subprocess.run(["patch","--forward","--batch","--fuzz=0","-p1","-i",str(patch)],
                   cwd=temp,check=True,stdout=subprocess.PIPE)
    return (Path(temp)/"sdm/libs/core/drm/hw_device_drm.cpp").read_text()

class ManagedDpu(unittest.TestCase):
    @unittest.skipUnless((SOURCE/"libdrmutils/drm_interface.h").exists(),"requires patched QTI source")
    def test_actual_contracts_and_sdm_preflight(self):
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/"test.cpp";binary=Path(temp)/"test"
            text=(ROOT/"tests/qti_dpu_sdm_contract_test.cpp.in").read_text()
            source.write_text(text.replace("@VALIDATE_DPU_LAYERS@",
                              block(prepared_device_source(temp),
                                    "DisplayError HWDeviceDRM::ValidateDpuLayers(")))
            command=["g++","-std=c++17","-Wall","-Wextra","-Werror","-Wno-missing-field-initializers",
                     "-D__user=","-fsanitize=address,undefined","-fno-pie","-no-pie"]
            for p in [ROOT/"probe/graphics",SOURCE/"libdrmutils",REF/"kernel-uapi",
                      REF/"kernel-uapi/drm",DRM,DRM/"include/drm",
                      REF/"android_vendor_qcom_opensource_display-commonsys-intf/include"]:
                command+=["-I",str(p)]
            subprocess.run(command+[str(source),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    @unittest.skipUnless((SOURCE/"sdm/libs/core/drm/hw_device_drm.cpp").exists(),"requires source QTI baseline")
    def test_actual_fb_registration_failures_and_p010_mapping(self):
        with tempfile.TemporaryDirectory() as temp:
            temp=Path(temp)
            actual=prepared_device_source(temp)
            formats=(SOURCE/"sdm/include/core/layer_buffer.h").read_text()
            text=(ROOT/"tests/qti_dpu_fb_registration_test.cpp.in").read_text()
            text=text.replace("@FORMATS_ENUM@",block(formats,"enum LayerBufferFormat")+";")
            for marker,start in [
                ("@GET_DRM_FORMAT@","static void GetDRMFormat("),
                ("@CREATE_FB_ID@","int HWDeviceDRM::Registry::CreateFbId("),
                ("@MAP_BUFFER@","int HWDeviceDRM::Registry::MapBufferToFbId("),
                ("@REGISTER@","DisplayError HWDeviceDRM::Registry::Register(")]:
                text=text.replace(marker,block(actual,start))
            cpp=temp/"test.cpp";binary=temp/"test";cpp.write_text(text)
            command=["g++","-std=c++17","-Wall","-Wextra","-Werror","-Wno-missing-field-initializers",
                     "-D__user=","-fsanitize=address,undefined","-fno-pie","-no-pie"]
            for p in [SOURCE/"libdrmutils",REF/"kernel-uapi",REF/"kernel-uapi/drm",DRM,DRM/"include/drm"]:
                command+=["-I",str(p)]
            subprocess.run(command+[str(cpp),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

if __name__=="__main__":unittest.main()
