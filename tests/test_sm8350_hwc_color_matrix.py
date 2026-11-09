"""Execute the patched composer CSC path with real dataspace/color enums."""
from pathlib import Path
import hashlib
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "out/qualcomm-full-stack-20261108"
SOURCE = Path(os.environ.get("QTI_COMPOSER_SOURCE", str(BASE / "sm8350-composer-v14/source")))
COLOR_INCLUDE = BASE / "sm8350-reference-v1/android_vendor_qcom_opensource_display-commonsys-intf/include"
ANDROID_INCLUDE = Path("/mnt/spx-workspace/toolchains/sq2-quest2-hidl/include")
PATCH = ROOT / "patches/sm8350-hwc-color-matrix.patch"
BASE_SHA = "fbc78d554e2fa91d839708a7c5de0c3b4d88e5a671bb82734a3ed3af0f616ed2"
PATCHED_SHA = "d006c2743c308681d56719a20e12e681b600a81e912daa4e6a6be584d74b87ed"

def block(source, start):
    at = source.index(start)
    brace = source.index("{", at)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[at:end]

class ColorMatrix(unittest.TestCase):
    @unittest.skipUnless((SOURCE / "composer/hwc_layers.cpp").exists(), "requires real QTI composer source")
    def test_actual_csc_path_preserves_format_and_metadata_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            temp = Path(temporary)
            path = temp / "composer/hwc_layers.cpp"
            path.parent.mkdir()
            original = (SOURCE / "composer/hwc_layers.cpp").read_bytes()
            source_hash = hashlib.sha256(original).hexdigest()
            self.assertIn(source_hash, [BASE_SHA, PATCHED_SHA])
            path.write_bytes(original)
            if source_hash == BASE_SHA:
                subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(PATCH)],
                               cwd=temp, check=True, stdout=subprocess.PIPE)
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), PATCHED_SHA)
            source = path.read_text()
            layer_header = (SOURCE / "sdm/include/core/layer_buffer.h").read_text()
            harness = r"""
#include <cassert>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <color_metadata.h>
#include <system/graphics.h>
#define DLOGW_IF(...) ((void)0)
#define DLOGV_IF(...) ((void)0)
#define UINT32(x) uint32_t(x)
namespace sdm {
enum DisplayError { kErrorNone, kErrorNotSupported };
struct private_handle_t {};
enum { kMetadataUpdate };
@FORMAT_ENUM@;
struct LayerBuffer { LayerBufferFormat format=kFormatRGBA8888; ColorMetaData color_metadata={}; };
struct Layer { LayerBuffer input_buffer; std::bitset<1> update_mask; };
bool SameConfig(const void *a,const void *b,uint32_t n) { return std::memcmp(a,b,n)==0; }
ColorMetaData donor={};
DisplayError donor_result=kErrorNone;
unsigned donor_reads=0;
DisplayError SetCSC(const private_handle_t *,ColorMetaData *out) {
  ++donor_reads;
  if(donor_result==kErrorNone)*out=donor;
  return donor_result;
}
class HWCLayer {
 public:
  Layer *layer_;
  int32_t dataspace_=HAL_DATASPACE_UNKNOWN;
  bool dataspace_supported_=false;
  void ValidateAndSetCSC(const private_handle_t *);
};
@METHODS@
}
@DPU_GATE@
int main() {
 using namespace sdm;
 Layer storage;
 HWCLayer layer;layer.layer_=&storage;
 auto &color=storage.input_buffer.color_metadata;
 const int32_t limited=HAL_DATASPACE_RANGE_LIMITED;
 const int32_t full=HAL_DATASPACE_RANGE_FULL;
 const int32_t srgb=HAL_DATASPACE_TRANSFER_SRGB;
 const int32_t smpte=HAL_DATASPACE_TRANSFER_SMPTE_170M;
 auto check=[&](int32_t standard,MatrixCoEfficients matrix,bool permitted) {
   storage.input_buffer.format=kFormatYCbCr420SemiPlanarVenus;
   layer.dataspace_=standard|limited|srgb;
   storage.update_mask.reset();
   layer.ValidateAndSetCSC(nullptr);
   assert(layer.dataspace_supported_);
   assert(color.matrixCoefficients==matrix);
   assert(DpuColorSupported(true,color)==permitted);
 };
 check(HAL_DATASPACE_STANDARD_BT601_525,MatrixCoEff_BT601_6_525,true);
 assert(storage.update_mask.test(kMetadataUpdate) && donor_reads==0);
 storage.update_mask.reset();layer.ValidateAndSetCSC(nullptr);
 assert(!storage.update_mask.test(kMetadataUpdate));
 // A matrix-only correction must invalidate the previous HWC validation result.
 color.matrixCoefficients=MatrixCoEff_Identity;
 layer.ValidateAndSetCSC(nullptr);
 assert(storage.update_mask.test(kMetadataUpdate) && color.matrixCoefficients==MatrixCoEff_BT601_6_525);
 check(HAL_DATASPACE_STANDARD_BT601_625,MatrixCoEff_BT601_6_625,true);
 layer.dataspace_=HAL_DATASPACE_STANDARD_BT601_625|limited|smpte;
 layer.ValidateAndSetCSC(nullptr);assert(DpuColorSupported(true,color));
 // Android Dataspace and QTI color_metadata.h define distinct matrices.
 // Metadata acceptance is separate from the unchanged bounded DPU color gate.
 check(HAL_DATASPACE_STANDARD_BT709,MatrixCoEff_BT709_5,true);
 check(HAL_DATASPACE_STANDARD_BT601_525_UNADJUSTED,MatrixCoEff_SMPTE240M,false);
 check(HAL_DATASPACE_STANDARD_BT601_625_UNADJUSTED,MatrixCoEff_BT601_6_Unadjusted,false);
 // Android P3 specifies only RGB primaries here, not a supported YUV matrix.
 ColorMetaData before_p3=color;storage.update_mask.reset();
 layer.dataspace_=HAL_DATASPACE_STANDARD_DCI_P3|limited|srgb;layer.ValidateAndSetCSC(nullptr);
 assert(!layer.dataspace_supported_ && !storage.update_mask.test(kMetadataUpdate));
 assert(std::memcmp(&before_p3,&color,sizeof(color))==0);
 storage.input_buffer.format=kFormatInvalid;
 layer.dataspace_=HAL_DATASPACE_STANDARD_BT601_525|limited|srgb;
 layer.ValidateAndSetCSC(nullptr);assert(!layer.dataspace_supported_);
 check(HAL_DATASPACE_STANDARD_BT601_525,MatrixCoEff_BT601_6_525,true);
 layer.dataspace_=HAL_DATASPACE_STANDARD_BT601_525|full|srgb;
 layer.ValidateAndSetCSC(nullptr);
 assert(layer.dataspace_supported_ && color.range==Range_Full && !DpuColorSupported(true,color));
 layer.dataspace_=HAL_DATASPACE_STANDARD_BT601_525|limited|HAL_DATASPACE_TRANSFER_ST2084;
 layer.ValidateAndSetCSC(nullptr);
 assert(layer.dataspace_supported_ && !DpuColorSupported(true,color));
 // A storage-format change with the same standard must reset a stale YUV matrix.
 storage.input_buffer.format=kFormatRGBA8888;
 layer.dataspace_=HAL_DATASPACE_STANDARD_BT709|full|srgb;
 storage.update_mask.reset();layer.ValidateAndSetCSC(nullptr);
 assert(layer.dataspace_supported_ && color.matrixCoefficients==MatrixCoEff_Identity);
 assert(storage.update_mask.test(kMetadataUpdate) && DpuColorSupported(false,color));
 // Unknown standards remain unsupported, without modifying the accepted metadata.
 ColorMetaData saved=color;
 layer.dataspace_=HAL_DATASPACE_STANDARD_UNSPECIFIED|limited|srgb;
 storage.update_mask.reset();layer.ValidateAndSetCSC(nullptr);
 assert(!layer.dataspace_supported_ && !storage.update_mask.test(kMetadataUpdate));
 assert(std::memcmp(&saved,&color,sizeof(color))==0);
 // Explicit HDR dataspace wins over donor matrix but retains the real HDR flags.
 storage.input_buffer.format=kFormatYCbCr420SemiPlanarVenus;
 donor={};donor.matrixCoefficients=MatrixCoEff_BT601_6_525;
 donor.masteringDisplayInfo.colorVolumeSEIEnabled=true;
 layer.dataspace_=HAL_DATASPACE_STANDARD_BT2020|limited|HAL_DATASPACE_TRANSFER_ST2084;
 layer.ValidateAndSetCSC(nullptr);
 assert(layer.dataspace_supported_ && color.matrixCoefficients==MatrixCoEff_BT2020);
 assert(color.masteringDisplayInfo.colorVolumeSEIEnabled && !DpuColorSupported(true,color));
 assert(donor_reads==1);
 // When no dataspace is supplied, retain the existing metadata-authoritative path.
 storage.input_buffer.format=kFormatYCbCr420SemiPlanarVenus;
 storage.input_buffer.color_metadata={};donor={};
 donor.colorPrimaries=ColorPrimaries_BT601_6_525;donor.range=Range_Limited;
 donor.transfer=Transfer_SMPTE_170M;donor.matrixCoefficients=MatrixCoEff_BT601_6_525;
 layer.dataspace_=HAL_DATASPACE_UNKNOWN;layer.ValidateAndSetCSC(nullptr);
 assert(layer.dataspace_supported_ && DpuColorSupported(true,color) && donor_reads==2);
 donor_result=kErrorNotSupported;layer.ValidateAndSetCSC(nullptr);
 assert(!layer.dataspace_supported_ && donor_reads==3);
 std::puts("PATCHED_HWC_COLOR_MATRIX_PASS source_path_and_dpu_gate=1");
}
"""
            methods = "\n\n".join(block(source, name) for name in [
                "bool GetColorPrimary(", "bool GetTransfer(", "bool GetRange(",
                "bool IsBT2020(", "static bool GetSDMColorMatrix(", "bool GetSDMColorSpace(",
                "void HWCLayer::ValidateAndSetCSC("])
            gate = block((ROOT / "probe/graphics/qti-dpu-sdm-contract.h").read_text(),
                         "inline bool DpuColorSupported(")
            harness = harness.replace("@FORMAT_ENUM@", block(layer_header, "enum LayerBufferFormat"))
            harness = harness.replace("@METHODS@", methods).replace("@DPU_GATE@", gate)
            cpp, binary = temp / "actual-color.cpp", temp / "actual-color"
            cpp.write_text(harness)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I", str(COLOR_INCLUDE), "-I", str(ANDROID_INCLUDE),
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10,
                           env=dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                                    UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))

if __name__ == "__main__":
    unittest.main()
