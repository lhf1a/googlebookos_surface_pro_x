// SPDX-License-Identifier: GPL-2.0-only
#include "spx-ir-preprocess.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace spx::face {
namespace {

inline void unpackY10Row(const uint8_t* src, uint16_t* dst, unsigned width) {
    for (unsigned x = 0; x < width; ++x) {
        uint16_t v = static_cast<uint16_t>(src[x * 2]) |
                     (static_cast<uint16_t>(src[x * 2 + 1]) << 8);
        dst[x] = v & 0x03FFu;
    }
}

inline void unpackY10PRow(const uint8_t* src, uint16_t* dst, unsigned width) {
    for (unsigned x = 0; x < width; x += 4, src += 5) {
        const uint32_t lsb = src[4];
        dst[x + 0] = static_cast<uint16_t>((static_cast<uint32_t>(src[0]) << 2) | (lsb & 3u));
        dst[x + 1] =
            static_cast<uint16_t>((static_cast<uint32_t>(src[1]) << 2) | ((lsb >> 2) & 3u));
        dst[x + 2] =
            static_cast<uint16_t>((static_cast<uint32_t>(src[2]) << 2) | ((lsb >> 4) & 3u));
        dst[x + 3] =
            static_cast<uint16_t>((static_cast<uint32_t>(src[3]) << 2) | ((lsb >> 6) & 3u));
    }
}

inline uint16_t percentileFromHist(const std::array<uint32_t, 1024>& hist,
                                   uint64_t totalCount,
                                   double q) {
    if (totalCount == 0) return 0;
    const uint64_t target =
        static_cast<uint64_t>(std::floor(q * static_cast<double>(totalCount - 1)));
    uint64_t acc = 0;
    for (size_t bin = 0; bin < hist.size(); ++bin) {
        acc += hist[bin];
        if (acc > target) return static_cast<uint16_t>(bin);
    }
    return 1023u;
}

}  // namespace

Rotation parseRotationDegrees(const char* text, Rotation fallback) {
    if (!text || !*text) return fallback;
    if (std::strcmp(text, "0") == 0) return Rotation::Deg0;
    if (std::strcmp(text, "90") == 0) return Rotation::Deg90CW;
    if (std::strcmp(text, "180") == 0) return Rotation::Deg180;
    if (std::strcmp(text, "270") == 0) return Rotation::Deg270CW;
    return fallback;
}

IrFrameStats preprocessIrFrame(const uint8_t* raw,
                               size_t size,
                               unsigned srcWidth,
                               unsigned srcHeight,
                               unsigned stride,
                               uint32_t v4l2PixFmt,
                               Rotation rot) {
    if (!raw || srcWidth == 0 || srcHeight == 0) {
        throw std::invalid_argument("invalid IR frame dimensions");
    }

    const bool isPacked = (v4l2PixFmt == kPixFmtY10P);
    if (!isPacked && v4l2PixFmt != kPixFmtY10) {
        throw std::invalid_argument("unsupported IR pixel format");
    }
    if (isPacked && (srcWidth % 4u) != 0) {
        throw std::invalid_argument("Y10P width must be a multiple of 4");
    }

    const size_t minRowBytes =
        isPacked ? (static_cast<size_t>(srcWidth) / 4u) * 5u : static_cast<size_t>(srcWidth) * 2u;
    if (stride < minRowBytes) {
        throw std::invalid_argument("IR frame stride smaller than active row width");
    }
    const size_t requiredBytes =
        static_cast<size_t>(srcHeight - 1u) * static_cast<size_t>(stride) + minRowBytes;
    if (size < requiredBytes) {
        throw std::invalid_argument("IR frame buffer truncated");
    }

    std::vector<uint16_t> unpacked(static_cast<size_t>(srcWidth) * srcHeight);
    for (unsigned y = 0; y < srcHeight; ++y) {
        const uint8_t* row = raw + static_cast<size_t>(y) * stride;
        uint16_t* dst = unpacked.data() + static_cast<size_t>(y) * srcWidth;
        if (isPacked) {
            unpackY10PRow(row, dst, srcWidth);
        } else {
            unpackY10Row(row, dst, srcWidth);
        }
    }

    IrFrameStats out;
    const bool swapAxes = (rot == Rotation::Deg90CW || rot == Rotation::Deg270CW);
    out.width = swapAxes ? srcHeight : srcWidth;
    out.height = swapAxes ? srcWidth : srcHeight;
    const size_t nPixels = static_cast<size_t>(out.width) * out.height;
    out.upright10.resize(nPixels);
    out.gray8.resize(nPixels);

    if (rot == Rotation::Deg0) {
        std::copy(unpacked.begin(), unpacked.end(), out.upright10.begin());
    } else if (rot == Rotation::Deg180) {
        for (unsigned y = 0; y < srcHeight; ++y) {
            const uint16_t* srow = unpacked.data() + static_cast<size_t>(y) * srcWidth;
            uint16_t* drow = out.upright10.data() + static_cast<size_t>(srcHeight - 1u - y) * srcWidth;
            for (unsigned x = 0; x < srcWidth; ++x) {
                drow[srcWidth - 1u - x] = srow[x];
            }
        }
    } else if (rot == Rotation::Deg90CW) {
        const unsigned dstW = out.width;
        for (unsigned y = 0; y < srcHeight; ++y) {
            const uint16_t* srow = unpacked.data() + static_cast<size_t>(y) * srcWidth;
            const unsigned dx = srcHeight - 1u - y;
            for (unsigned x = 0; x < srcWidth; ++x) {
                out.upright10[static_cast<size_t>(x) * dstW + dx] = srow[x];
            }
        }
    } else {
        // Rotation::Deg270CW
        const unsigned dstW = out.width;
        for (unsigned y = 0; y < srcHeight; ++y) {
            const uint16_t* srow = unpacked.data() + static_cast<size_t>(y) * srcWidth;
            const unsigned dx = y;
            for (unsigned x = 0; x < srcWidth; ++x) {
                const unsigned dy = srcWidth - 1u - x;
                out.upright10[static_cast<size_t>(dy) * dstW + dx] = srow[x];
            }
        }
    }

    const unsigned roiX0 = (out.width * 15u) / 100u;
    const unsigned roiX1 = std::max(roiX0 + 1u, (out.width * 85u) / 100u);
    const unsigned roiY0 = (out.height * 12u) / 100u;
    const unsigned roiY1 = std::max(roiY0 + 1u, (out.height * 88u) / 100u);

    std::array<uint32_t, 1024> hist{};
    uint64_t sum = 0;
    uint64_t darkCount = 0;
    uint64_t satCount = 0;
    uint64_t roiCount = 0;

    for (unsigned y = roiY0; y < roiY1; ++y) {
        const uint16_t* row = out.upright10.data() + static_cast<size_t>(y) * out.width;
        for (unsigned x = roiX0; x < roiX1; ++x) {
            const uint16_t v = row[x];
            hist[v]++;
            sum += v;
            if (v < 18u) ++darkCount;
            if (v >= 1005u) ++satCount;
            ++roiCount;
        }
    }

    out.p1 = percentileFromHist(hist, roiCount, 0.01);
    out.p50 = percentileFromHist(hist, roiCount, 0.50);
    out.p99 = percentileFromHist(hist, roiCount, 0.99);
    out.roiLinearMean =
        roiCount > 0 ? static_cast<float>(sum) / (static_cast<float>(roiCount) * 1023.0f) : 0.0f;
    out.darkRatio =
        roiCount > 0 ? static_cast<float>(darkCount) / static_cast<float>(roiCount) : 1.0f;
    out.saturatedRatio =
        roiCount > 0 ? static_cast<float>(satCount) / static_cast<float>(roiCount) : 0.0f;

    // Linear percentile stretch with floor span 64 so near-zero frames do not
    // amplify dark read noise into false contrast.
    const uint16_t lo = out.p1;
    const uint32_t span = std::max<uint32_t>(64u, static_cast<uint32_t>(out.p99 - lo));
    const float scale = 255.0f / static_cast<float>(span);

    size_t i = 0;
#if defined(__aarch64__) && defined(__ARM_NEON)
    const uint16x8_t vlo = vdupq_n_u16(lo);
    const float32x4_t vscale = vdupq_n_f32(scale);
    const float32x4_t vhalf = vdupq_n_f32(0.5f);
    const float32x4_t vmax = vdupq_n_f32(255.0f);
    for (; i + 8 <= nPixels; i += 8) {
        uint16x8_t v = vld1q_u16(out.upright10.data() + i);
        uint16x8_t shifted = vqsubq_u16(v, vlo);
        uint32x4_t lo32 = vmovl_u16(vget_low_u16(shifted));
        uint32x4_t hi32 = vmovl_u16(vget_high_u16(shifted));
        float32x4_t f0 = vminq_f32(vmlaq_f32(vhalf, vcvtq_f32_u32(lo32), vscale), vmax);
        float32x4_t f1 = vminq_f32(vmlaq_f32(vhalf, vcvtq_f32_u32(hi32), vscale), vmax);
        uint32x4_t u0 = vcvtq_u32_f32(f0);
        uint32x4_t u1 = vcvtq_u32_f32(f1);
        uint16x8_t packed16 = vcombine_u16(vqmovn_u32(u0), vqmovn_u32(u1));
        uint8x8_t packed8 = vqmovn_u16(packed16);
        vst1_u8(out.gray8.data() + i, packed8);
    }
#endif
    const uint32_t q16Scale = static_cast<uint32_t>(std::lround(scale * 65536.0f));
    for (; i < nPixels; ++i) {
        const uint16_t v = out.upright10[i];
        if (v <= lo) {
            out.gray8[i] = 0;
        } else {
            uint32_t mapped = ((static_cast<uint32_t>(v - lo) * q16Scale) + 32768u) >> 16;
            out.gray8[i] = static_cast<uint8_t>(std::min<uint32_t>(mapped, 255u));
        }
    }

    return out;
}

void gray8ToRgba(const uint8_t* gray,
                 unsigned width,
                 unsigned height,
                 uint8_t* dstRgba,
                 unsigned dstStridePixels) {
    if (!gray || !dstRgba || width == 0 || height == 0 || dstStridePixels < width) return;
    for (unsigned y = 0; y < height; ++y) {
        const uint8_t* s = gray + static_cast<size_t>(y) * width;
        uint8_t* d = dstRgba + static_cast<size_t>(y) * dstStridePixels * 4u;
        for (unsigned x = 0; x < width; ++x) {
            const uint8_t g = s[x];
            d[x * 4u + 0] = g;
            d[x * 4u + 1] = g;
            d[x * 4u + 2] = g;
            d[x * 4u + 3] = 255u;
        }
    }
}

namespace {

template <typename WriteBlackRow, typename WritePixel>
void renderIrPreviewCommon(const uint8_t* gray,
                           unsigned srcW,
                           unsigned srcH,
                           unsigned dstW,
                           unsigned dstH,
                           unsigned dstStridePixels,
                           bool mirrorX,
                           WriteBlackRow&& writeBlackRow,
                           WritePixel&& writePixel) {
    if (!gray || srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0 || dstStridePixels < dstW) {
        return;
    }
    for (unsigned y = 0; y < dstH; ++y) {
        writeBlackRow(y, dstW);
    }

    const unsigned srcSide = std::min(srcW, srcH);
    const unsigned srcX0 = (srcW - srcSide) / 2u;
    const unsigned srcY0 = (srcH - srcSide) / 2u;

    // Align square output at top edge (dstY0=0) so 480x640 FaceEnrollPreviewFragment
    // scale(1.0, 640/480) maps rows 0..dstSide-1 1:1 into the circular view.
    const unsigned dstSide = std::min(dstW, dstH);
    const unsigned dstX0 = (dstW > dstSide) ? (dstW - dstSide) / 2u : 0u;
    const unsigned dstY0 = 0u;

    for (unsigned dy = 0; dy < dstSide; ++dy) {
        const unsigned sy = srcY0 + (dy * srcSide) / dstSide;
        const uint8_t* srow = gray + static_cast<size_t>(sy) * srcW;
        const unsigned outY = dstY0 + dy;
        for (unsigned dx = 0; dx < dstSide; ++dx) {
            const unsigned sxOffset = (dx * srcSide) / dstSide;
            const unsigned sx = srcX0 + (mirrorX ? (srcSide - 1u - sxOffset) : sxOffset);
            writePixel(outY, dstX0 + dx, srow[sx]);
        }
    }
}

}  // namespace

void renderIrPreviewRgba(const uint8_t* gray,
                         unsigned srcW,
                         unsigned srcH,
                         uint8_t* dstRgba,
                         unsigned dstW,
                         unsigned dstH,
                         unsigned dstStridePixels,
                         bool mirrorX) {
    if (!dstRgba) return;
    renderIrPreviewCommon(
        gray,
        srcW,
        srcH,
        dstW,
        dstH,
        dstStridePixels,
        mirrorX,
        [&](unsigned y, unsigned width) {
            uint8_t* row = dstRgba + static_cast<size_t>(y) * dstStridePixels * 4u;
            for (unsigned x = 0; x < width; ++x) {
                row[x * 4u + 0] = 0u;
                row[x * 4u + 1] = 0u;
                row[x * 4u + 2] = 0u;
                row[x * 4u + 3] = 255u;
            }
        },
        [&](unsigned y, unsigned x, uint8_t g) {
            uint8_t* px = dstRgba + (static_cast<size_t>(y) * dstStridePixels + x) * 4u;
            px[0] = g;
            px[1] = g;
            px[2] = g;
            px[3] = 255u;
        });
}

void renderIrPreviewRgb565(const uint8_t* gray,
                           unsigned srcW,
                           unsigned srcH,
                           uint16_t* dstRgb565,
                           unsigned dstW,
                           unsigned dstH,
                           unsigned dstStridePixels,
                           bool mirrorX) {
    if (!dstRgb565) return;
    renderIrPreviewCommon(
        gray,
        srcW,
        srcH,
        dstW,
        dstH,
        dstStridePixels,
        mirrorX,
        [&](unsigned y, unsigned width) {
            uint16_t* row = dstRgb565 + static_cast<size_t>(y) * dstStridePixels;
            std::fill_n(row, width, static_cast<uint16_t>(0u));
        },
        [&](unsigned y, unsigned x, uint8_t g) {
            uint16_t* px = dstRgb565 + static_cast<size_t>(y) * dstStridePixels + x;
            *px = static_cast<uint16_t>(((static_cast<uint16_t>(g >> 3)) << 11) |
                                        ((static_cast<uint16_t>(g >> 2)) << 5) |
                                        static_cast<uint16_t>(g >> 3));
        });
}

}  // namespace spx::face
