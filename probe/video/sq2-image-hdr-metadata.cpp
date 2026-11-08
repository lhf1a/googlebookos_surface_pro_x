// Diagnostic readback of actual queued-buffer metadata, not decoder input hints.
#include <cstdio>
#include <optional>
#include <vndk/hardware_buffer.h>
#include <ui/GraphicBufferMapper.h>

extern "C" int sq2_dump_image_hdr_metadata(AHardwareBuffer *buffer) {
    auto& mapper = android::GraphicBufferMapper::get();
    auto handle = AHardwareBuffer_getNativeHandle(buffer);
    std::optional<android::ui::Smpte2086> mastering;
    std::optional<android::ui::Cta861_3> light;
    int a = mapper.getSmpte2086(handle, &mastering);
    int b = mapper.getCta861_3(handle, &light);
    std::printf("IMAGE_HDR smpte_rc=%d present=%d cta_rc=%d present=%d\n",
                a, mastering.has_value(), b, light.has_value());
    if (a || b) return 0;
    if (mastering) {
        auto& m = *mastering;
        std::printf("IMAGE_MASTERING R=%.6f,%.6f G=%.6f,%.6f B=%.6f,%.6f W=%.6f,%.6f max=%.6f min=%.6f\n",
            m.primaryRed.x, m.primaryRed.y, m.primaryGreen.x, m.primaryGreen.y,
            m.primaryBlue.x, m.primaryBlue.y, m.whitePoint.x, m.whitePoint.y,
            m.maxLuminance, m.minLuminance);
    }
    if (light) {
        std::printf("IMAGE_LIGHT max_cll=%.6f max_fall=%.6f\n",
                    light->maxContentLightLevel, light->maxFrameAverageLightLevel);
    }
    return 1;
}
