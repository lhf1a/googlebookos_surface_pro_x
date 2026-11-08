// Diagnostic executable only: links the patched AOSP converter, never opens a codec.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaCodecConstants.h>
#define private public
#include <media/stagefright/ColorConverter.h>
#undef private

using namespace android;
#include "sq2-p010-buffer-check.inc"

static OMX_COLOR_FORMATTYPE color(int32_t value) {
    return static_cast<OMX_COLOR_FORMATTYPE>(value);
}

static void geometry() {
    sp<AMessage> f = new AMessage;
    f->setInt32("width", 320);
    f->setInt32("slice-height", 256);
    f->setInt32("stride", 640);
    f->setRect("crop", 0, 0, 319, 239);
    assert(validP010Buffer(f, 245760));
    assert(!validP010Buffer(f, 245759));
    f->setInt32("stride", 642);
    assert(!validP010Buffer(f, 300000));
    f->setInt32("stride", 636);
    assert(!validP010Buffer(f, 300000));
    f->setInt32("stride", 640);
    f->setRect("crop", 2, 1, 4, 3);
    assert(validP010Buffer(f, 245760));
    f->setRect("crop", 1, 0, 319, 239);
    assert(!validP010Buffer(f, 245760));
    f->setRect("crop", -2, 0, 319, 239);
    assert(!validP010Buffer(f, 245760));
    f->setRect("crop", 0, 0, 320, 239);
    assert(!validP010Buffer(f, 245760));
    f->setRect("crop", 0, 0, 319, 256);
    assert(!validP010Buffer(f, 245760));
    f->setRect("crop", 4, 2, 2, 3);
    assert(!validP010Buffer(f, 245760));
    f->setInt32("width", INT32_MAX);
    f->setInt32("slice-height", INT32_MAX);
    f->setInt32("stride", INT32_MAX - 3);
    assert(!validP010Buffer(f, SIZE_MAX));
    puts("GEOMETRY=pass");
}

static void ramp() {
    constexpr size_t width = 1024, height = 2;
    std::vector<uint16_t> src(width * 3, 512 << 6);
    std::vector<uint32_t> dst(width * height, 0);
    for (size_t y = 0; y < height; ++y)
        for (size_t x = 0; x < width; ++x)
            src[y * width + x] = x << 6;
    ColorConverter cc(color(COLOR_FormatYUVP010), color(COLOR_Format32bitABGR2101010));
    assert(cc.isValid() && cc.isDstRGB());
    cc.setSrcColorSpace(6, 1, 7); // BT.2020 full range, HLG is preserved, not tone mapped.
    ColorConverter::BitmapParams in(src.data(), width, height, width * 2,
            0, 0, width - 1, height - 1, color(COLOR_FormatYUVP010));
    ColorConverter::BitmapParams out(dst.data(), width, height, width * 4,
            0, 0, width - 1, height - 1, color(COLOR_Format32bitABGR2101010));
    assert(cc.convertYUVP010(in, out) == OK);
    for (size_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            assert(dst[y * width + x] == (0xc0000000u | x | (x << 10) | (x << 20)));
    puts("FULL_RANGE_1024_LEVELS=pass");
}

static int clamp10(double v) {
    return std::clamp(static_cast<int>(std::lround(v)), 0, 1023);
}

static void crop(int range, int transfer) {
    constexpr size_t width = 6, height = 4, stride = 8, dstStride = 5;
    std::vector<uint16_t> src(stride * 6, 0);
    std::vector<uint32_t> dst(dstStride * 3, 0x13579bdf);
    for (size_t y = 0; y < height; ++y)
        for (size_t x = 0; x < stride; ++x)
            src[y * stride + x] = (64 + y * 150 + x * 63) << 6;
    for (size_t y = 0; y < 2; ++y)
        for (size_t x = 0; x < stride; x += 2) {
            src[stride * height + y * stride + x] = (y ? 680 : 340) << 6;
            src[stride * height + y * stride + x + 1] = (y ? 380 : 650) << 6;
        }
    ColorConverter cc(color(COLOR_FormatYUVP010), color(COLOR_Format32bitABGR2101010));
    cc.setSrcColorSpace(6, range, transfer);
    ColorConverter::BitmapParams in(src.data(), width, height, stride * 2,
            2, 1, 4, 3, color(COLOR_FormatYUVP010));
    ColorConverter::BitmapParams out(dst.data(), dstStride, 3, dstStride * 4,
            0, 0, 2, 2, color(COLOR_Format32bitABGR2101010));
    assert(cc.convertYUVP010(in, out) == OK);
    for (size_t y = 0; y < 3; ++y) {
        for (size_t x = 0; x < 3; ++x) {
            size_t sx = x + 2, sy = y + 1;
            double luma = src[sy * stride + sx] >> 6;
            double u = (src[stride * height + (sy / 2) * stride + (sx & ~1u)] >> 6) - 512.0;
            double v = (src[stride * height + (sy / 2) * stride + (sx & ~1u) + 1] >> 6) - 512.0;
            if (range == 2) {
                luma = (luma - 64) * 1023 / 876;
                u *= 1023.0 / 896;
                v *= 1023.0 / 896;
            }
            // Independent BT.2020 non-constant-luminance floating-point matrix.
            int ref[] = {clamp10(luma + 1.4746 * v),
                         clamp10(luma - .164553 * u - .571353 * v),
                         clamp10(luma + 1.8814 * u)};
            uint32_t pixel = dst[y * dstStride + x];
            assert((pixel >> 30) == 3);
            for (unsigned c = 0; c < 3; ++c)
                assert(std::abs(int((pixel >> (10 * c)) & 1023) - ref[c]) <= 2);
        }
        assert(dst[y * dstStride + 3] == 0x13579bdf);
        assert(dst[y * dstStride + 4] == 0x13579bdf);
    }
    printf("ODD_CROP_BT2020 range=%d transfer=%d pass\n", range, transfer);
}

int main() {
    geometry();
    ramp();
    crop(2, 6);
    crop(2, 7);
    crop(1, 6);
    puts("RESULT=pass (CPU conversion only; no Surface or tone-mapping proof)");
}
