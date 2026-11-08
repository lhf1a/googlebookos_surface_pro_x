// Runs the patched system renderer in an isolated diagnostic process.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <unistd.h>
#include <android/hardware_buffer.h>
#include <android/data_space.h>
#include <vndk/hardware_buffer.h>
#include <ui/GraphicBufferMapper.h>
#include <media/stagefright/foundation/ColorUtils.h>
#include <media/NdkImageReader.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaCodecConstants.h>
#include "SoftwareRenderer.h"

using namespace android;

int main() {
    setbuf(stdout, nullptr);
    AImageReader *reader = nullptr;
    assert(AImageReader_newWithDataSpace(3, 3,
            AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
            2, AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM, ADATASPACE_UNKNOWN, &reader) == AMEDIA_OK);
    ANativeWindow *window = nullptr;
    assert(AImageReader_getWindow(reader, &window) == AMEDIA_OK);
    assert(native_window_api_connect(window, NATIVE_WINDOW_API_MEDIA) == 0);
    {
        SoftwareRenderer renderer(window);
        sp<AMessage> format = new AMessage;
        format->setInt32("color-format", COLOR_FormatYUVP010);
        format->setInt32("width", 6);
        format->setInt32("slice-height", 4);
        format->setRect("crop", 2, 1, 4, 3);
        format->setInt32("color-standard", 6);
        format->setInt32("color-range", 1);
        for (int frame = 0; frame < 3; ++frame) {
            // Changing only byte stride must reset the renderer's cached geometry.
            size_t stride = frame == 0 ? 8 : 12;
            std::vector<uint16_t> input(stride * 6, 512 << 6);
            for (size_t y = 0; y < 4; ++y)
                for (size_t x = 0; x < stride; ++x)
                    input[y * stride + x] = (100 + y * 130 + x * 61) << 6;
            int transfer = frame == 0 ? 6 : 7;
            int32_t dataspace = HAL_DATASPACE_STANDARD_BT2020 | HAL_DATASPACE_RANGE_FULL
                    | (frame == 0 ? HAL_DATASPACE_TRANSFER_ST2084 : HAL_DATASPACE_TRANSFER_HLG);
            format->setInt32("stride", stride * 2);
            format->setInt32("color-transfer", transfer);
            format->setInt32("android._dataspace", dataspace);
            HDRStaticInfo hdr = {};
            if (frame == 0) {
                hdr.sType1.mR = {34000, 16000};
                hdr.sType1.mG = {13250, 34500};
                hdr.sType1.mB = {7500, 3000};
                hdr.sType1.mW = {15635, 16450};
                hdr.sType1.mMaxDisplayLuminance = 1000;
                hdr.sType1.mMinDisplayLuminance = 50;
                hdr.sType1.mMaxContentLightLevel = 1000;
                hdr.sType1.mMaxFrameAverageLightLevel = 400;
            }
            ColorUtils::setHDRStaticInfoIntoFormat(hdr, format);
            renderer.render(input.data(), input.size() * 2, frame * 33333, -1, 1, format);
            AImage *image = nullptr;
            media_status_t acquired = AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE;
            for (int attempt = 0; attempt < 100 && acquired == AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE; ++attempt) {
                acquired = AImageReader_acquireNextImage(reader, &image);
                if (acquired == AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE) usleep(10000);
            }
            assert(acquired == AMEDIA_OK && image);
            int32_t actualDataspace = 0;
            assert(AImage_getDataSpace(image, &actualDataspace) == AMEDIA_OK);
            assert(actualDataspace == dataspace);
            AImageCropRect crop;
            assert(AImage_getCropRect(image, &crop) == AMEDIA_OK);
            assert(crop.left == 0 && crop.top == 0 && crop.right == 3 && crop.bottom == 3);
            AHardwareBuffer *ahb = nullptr;
            assert(AImage_getHardwareBuffer(image, &ahb) == AMEDIA_OK);
            std::optional<ui::Smpte2086> mastering;
            std::optional<ui::Cta861_3> light;
            auto& mapper = GraphicBufferMapper::get();
            auto handle = AHardwareBuffer_getNativeHandle(ahb);
            int smpteResult = mapper.getSmpte2086(handle, &mastering);
            int lightResult = mapper.getCta861_3(handle, &light);
            printf("HDR_METADATA frame=%d smpte_rc=%d present=%d cta_rc=%d present=%d\n",
                    frame, smpteResult, mastering.has_value(), lightResult, light.has_value());
            assert(smpteResult == 0 && lightResult == 0);
            assert(mastering.has_value() == (frame == 0));
            assert(light.has_value() == (frame == 0));
            if (frame == 0) {
                assert(mastering->maxLuminance == 1000.f);
                assert(std::abs(mastering->minLuminance - .005f) < .000001f);
                assert(std::abs(mastering->primaryRed.x - .68f) < .00001f);
                assert(light->maxContentLightLevel == 1000.f);
                assert(light->maxFrameAverageLightLevel == 400.f);
            }
            AHardwareBuffer_Desc desc;
            AHardwareBuffer_describe(ahb, &desc);
            assert(desc.format == AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM);
            assert(desc.width >= 3 && desc.height >= 3 && desc.stride >= 3);
            void *pixels = nullptr;
            assert(AHardwareBuffer_lock(ahb, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
                    -1, nullptr, &pixels) == 0);
            for (uint32_t y = 0; y < 3; ++y)
                for (uint32_t x = 0; x < 3; ++x) {
                    uint32_t level = 100 + (y + 1) * 130 + (x + 2) * 61;
                    uint32_t expected = 0xc0000000u | level | (level << 10) | (level << 20);
                    assert(static_cast<uint32_t *>(pixels)[y * desc.stride + x] == expected);
                }
            assert(AHardwareBuffer_unlock(ahb, nullptr) == 0);
            printf("SURFACE frame=%d source_stride=%zu output_stride=%u dataspace=%#x RGB10=exact crop=0,0,3,3\n",
                    frame, stride * 2, desc.stride, actualDataspace);
            AImage_delete(image);
        }
    }
    assert(native_window_api_disconnect(window, NATIVE_WINDOW_API_MEDIA) == 0);
    AImageReader_delete(reader);
    puts("RESULT=pass (isolated renderer; no HIDL integration or display tone-mapping proof)");
}
