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

inline uint8_t clampByte(float v) {
    return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
}

inline float capsuleCoverage(float px, float py, float x0, float y0, float x1, float y1) {
    const float h = y1 - y0;
    const float w = x1 - x0;
    if (h <= 0.0f || w <= 0.0f) return 0.0f;
    const float r = 0.5f * h;
    const float cx0 = std::min(0.5f * (x0 + x1), x0 + r);
    const float cx1 = std::max(0.5f * (x0 + x1), x1 - r);
    const float cy = 0.5f * (y0 + y1);
    const float cx = std::clamp(px, cx0, cx1);
    const float dx = px - cx;
    const float dy = py - cy;
    const float dist = std::hypot(dx, dy) - r;
    return std::clamp(0.5f - dist, 0.0f, 1.0f);
}

template <typename T>
void rotateBufferCw(const T* src,
                    unsigned srcWidth,
                    unsigned srcHeight,
                    Rotation rot,
                    std::vector<T>& dst) {
    const bool swapAxes = (rot == Rotation::Deg90CW || rot == Rotation::Deg270CW);
    const unsigned dstW = swapAxes ? srcHeight : srcWidth;
    const unsigned dstH = swapAxes ? srcWidth : srcHeight;
    dst.resize(static_cast<size_t>(dstW) * dstH);

    if (rot == Rotation::Deg0) {
        std::copy_n(src, static_cast<size_t>(srcWidth) * srcHeight, dst.begin());
    } else if (rot == Rotation::Deg180) {
        for (unsigned y = 0; y < srcHeight; ++y) {
            const T* srow = src + static_cast<size_t>(y) * srcWidth;
            T* drow = dst.data() + static_cast<size_t>(srcHeight - 1u - y) * srcWidth;
            for (unsigned x = 0; x < srcWidth; ++x) {
                drow[srcWidth - 1u - x] = srow[x];
            }
        }
    } else if (rot == Rotation::Deg90CW) {
        for (unsigned y = 0; y < srcHeight; ++y) {
            const T* srow = src + static_cast<size_t>(y) * srcWidth;
            const unsigned dx = srcHeight - 1u - y;
            for (unsigned x = 0; x < srcWidth; ++x) {
                dst[static_cast<size_t>(x) * dstW + dx] = srow[x];
            }
        }
    } else {
        // Rotation::Deg270CW
        for (unsigned y = 0; y < srcHeight; ++y) {
            const T* srow = src + static_cast<size_t>(y) * srcWidth;
            const unsigned dx = y;
            for (unsigned x = 0; x < srcWidth; ++x) {
                const unsigned dy = srcWidth - 1u - x;
                dst[static_cast<size_t>(dy) * dstW + dx] = srow[x];
            }
        }
    }
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

int rotationToDegrees(Rotation rot) {
    switch (rot) {
    case Rotation::Deg0:
        return 0;
    case Rotation::Deg90CW:
        return 90;
    case Rotation::Deg180:
        return 180;
    case Rotation::Deg270CW:
        return 270;
    }
    return 0;
}

Rotation composeRotation(Rotation base, Rotation deltaCw) {
    const int deg = (rotationToDegrees(base) + rotationToDegrees(deltaCw)) % 360;
    switch (deg) {
    case 0:
        return Rotation::Deg0;
    case 90:
        return Rotation::Deg90CW;
    case 180:
        return Rotation::Deg180;
    case 270:
        return Rotation::Deg270CW;
    }
    return Rotation::Deg0;
}

IrFrameStats rotateIrFrameStats(const IrFrameStats& src, Rotation deltaCw) {
    if (deltaCw == Rotation::Deg0 || src.width == 0 || src.height == 0) {
        return src;
    }
    IrFrameStats out;
    const bool swapAxes = (deltaCw == Rotation::Deg90CW || deltaCw == Rotation::Deg270CW);
    out.width = swapAxes ? src.height : src.width;
    out.height = swapAxes ? src.width : src.height;
    out.p1 = src.p1;
    out.p50 = src.p50;
    out.p99 = src.p99;
    out.roiLinearMean = src.roiLinearMean;
    out.darkRatio = src.darkRatio;
    out.saturatedRatio = src.saturatedRatio;

    const size_t nSrc = static_cast<size_t>(src.width) * src.height;
    if (src.upright10.size() == nSrc) {
        rotateBufferCw(src.upright10.data(), src.width, src.height, deltaCw, out.upright10);
    }
    if (src.gray8.size() == nSrc) {
        rotateBufferCw(src.gray8.data(), src.width, src.height, deltaCw, out.gray8);
    }
    return out;
}

bool classifyGravityOrientation(float ax,
                                float ay,
                                float az,
                                bool hasPrevDelta,
                                Rotation prevDelta,
                                Rotation& outDelta) {
    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az)) {
        if (hasPrevDelta) {
            outDelta = prevDelta;
            return true;
        }
        return false;
    }

    const float horiz = std::hypot(ax, ay);
    if (horiz < 2.8f) {
        if (hasPrevDelta) {
            outDelta = prevDelta;
            return true;
        }
        return false;
    }

    const float absX = std::fabs(ax);
    const float absY = std::fabs(ay);
    const float ratio = hasPrevDelta ? 1.35f : 1.0f;

    if (absY >= ratio * absX) {
        outDelta = (ay >= 0.0f) ? Rotation::Deg0 : Rotation::Deg180;
        return true;
    }
    if (absX >= ratio * absY) {
        outDelta = (ax >= 0.0f) ? Rotation::Deg90CW : Rotation::Deg270CW;
        return true;
    }

    // Inside diagonal hysteresis deadband with an active prior orientation.
    outDelta = prevDelta;
    return true;
}

Rgb8 enrollProgressFillColor(float progress) {
    const float p = std::clamp(progress, 0.0f, 1.0f);
    if (p >= 0.995f) {
        return {52u, 168u, 83u};
    }
    return {66u, 133u, 244u};
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
    rotateBufferCw(unpacked.data(), srcWidth, srcHeight, rot, out.upright10);
    out.gray8.resize(nPixels);

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

template <typename WriteBlackRow, typename WriteRgbPixel>
void renderIrPreviewCommon(const uint8_t* gray,
                           unsigned srcW,
                           unsigned srcH,
                           unsigned dstW,
                           unsigned dstH,
                           unsigned dstStridePixels,
                           bool mirrorX,
                           float zoom,
                           float enrollProgress,
                           WriteBlackRow&& writeBlackRow,
                           WriteRgbPixel&& writeRgbPixel) {
    if (!gray || srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0 || dstStridePixels < dstW) {
        return;
    }
    for (unsigned y = 0; y < dstH; ++y) {
        writeBlackRow(y, dstW);
    }

    const unsigned srcSide = std::min(srcW, srcH);
    const float z = std::clamp(zoom, 1.0f, 3.0f);
    const unsigned cropSide =
        std::max(1u, static_cast<unsigned>(std::lround(static_cast<float>(srcSide) / z)));
    const unsigned srcX0 = (srcW - cropSide) / 2u;
    const unsigned srcY0 = (srcH - cropSide) / 2u;

    // Align square output at top edge (dstY0=0) so 480x640 FaceEnrollPreviewFragment
    // scale(1.0, 640/480) maps rows 0..dstSide-1 1:1 into the square view.
    const unsigned dstSide = std::min(dstW, dstH);
    const unsigned dstX0 = (dstW > dstSide) ? (dstW - dstSide) / 2u : 0u;
    const unsigned dstY0 = 0u;

    const bool drawProgress = (enrollProgress >= 0.0f);
    const float p = drawProgress ? std::clamp(enrollProgress, 0.0f, 1.0f) : 0.0f;
    const unsigned scrimH = drawProgress ? std::min(dstSide, std::max(40u, dstSide / 7u)) : 0u;
    const unsigned scrimY0 = dstSide - scrimH;
    const unsigned barW = drawProgress ? std::min(dstSide, std::max(32u, (dstSide * 72u) / 100u)) : 0u;
    const unsigned barH = drawProgress ? std::min(dstSide, std::max(8u, dstSide / 34u)) : 0u;
    const unsigned barX0 = (dstSide - barW) / 2u;
    const unsigned barBottomMargin = drawProgress ? std::max(14u, dstSide / 18u) : 0u;
    const unsigned barY0 =
        (dstSide > barH + barBottomMargin) ? (dstSide - barBottomMargin - barH) : 0u;
    const float barXf0 = static_cast<float>(barX0);
    const float barYf0 = static_cast<float>(barY0);
    const float barYf1 = static_cast<float>(barY0 + barH);
    const float fillW = drawProgress
                            ? std::min(static_cast<float>(barW),
                                       std::max(static_cast<float>(barH), p * static_cast<float>(barW)))
                            : 0.0f;
    const Rgb8 fillCol = drawProgress ? enrollProgressFillColor(p) : Rgb8{};

    for (unsigned dy = 0; dy < dstSide; ++dy) {
        const unsigned sy = srcY0 + (dy * cropSide) / dstSide;
        const uint8_t* srow = gray + static_cast<size_t>(sy) * srcW;
        const unsigned outY = dstY0 + dy;
        const float scrimAlpha =
            (drawProgress && dy >= scrimY0)
                ? (0.58f * ((scrimH > 1u)
                                ? (static_cast<float>(dy - scrimY0) / static_cast<float>(scrimH - 1u))
                                : 1.0f))
                : 0.0f;
        const bool inBarY = drawProgress && (dy + 1u >= barY0) && (dy <= barY0 + barH);
        for (unsigned dx = 0; dx < dstSide; ++dx) {
            const unsigned sxOffset = (dx * cropSide) / dstSide;
            const unsigned sx = srcX0 + (mirrorX ? (cropSide - 1u - sxOffset) : sxOffset);
            const uint8_t g = srow[sx];
            if (!drawProgress || (scrimAlpha <= 0.0f && !inBarY)) {
                writeRgbPixel(outY, dstX0 + dx, g, g, g);
                continue;
            }
            float fr = static_cast<float>(g);
            float fg = fr;
            float fb = fr;
            if (scrimAlpha > 0.0f) {
                constexpr float kScrimGray = 16.0f;
                fr = fr * (1.0f - scrimAlpha) + kScrimGray * scrimAlpha;
                fg = fr;
                fb = fr;
            }
            if (inBarY) {
                const float px = static_cast<float>(dx) + 0.5f;
                const float py = static_cast<float>(dy) + 0.5f;
                const float trackCov =
                    capsuleCoverage(px, py, barXf0, barYf0, barXf0 + static_cast<float>(barW), barYf1);
                if (trackCov > 0.0f) {
                    const float ta = 0.30f * trackCov;
                    fr = fr * (1.0f - ta) + 255.0f * ta;
                    fg = fg * (1.0f - ta) + 255.0f * ta;
                    fb = fb * (1.0f - ta) + 255.0f * ta;
                    if (p > 0.001f) {
                        const float fillCov = std::min(
                            trackCov, capsuleCoverage(px, py, barXf0, barYf0, barXf0 + fillW, barYf1));
                        if (fillCov > 0.0f) {
                            fr = fr * (1.0f - fillCov) + static_cast<float>(fillCol.r) * fillCov;
                            fg = fg * (1.0f - fillCov) + static_cast<float>(fillCol.g) * fillCov;
                            fb = fb * (1.0f - fillCov) + static_cast<float>(fillCol.b) * fillCov;
                        }
                    }
                }
            }
            writeRgbPixel(outY, dstX0 + dx, clampByte(fr), clampByte(fg), clampByte(fb));
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
                         bool mirrorX,
                         float zoom,
                         float enrollProgress) {
    if (!dstRgba) return;
    renderIrPreviewCommon(
        gray,
        srcW,
        srcH,
        dstW,
        dstH,
        dstStridePixels,
        mirrorX,
        zoom,
        enrollProgress,
        [&](unsigned y, unsigned width) {
            uint8_t* row = dstRgba + static_cast<size_t>(y) * dstStridePixels * 4u;
            for (unsigned x = 0; x < width; ++x) {
                row[x * 4u + 0] = 0u;
                row[x * 4u + 1] = 0u;
                row[x * 4u + 2] = 0u;
                row[x * 4u + 3] = 255u;
            }
        },
        [&](unsigned y, unsigned x, uint8_t r, uint8_t g, uint8_t b) {
            uint8_t* px = dstRgba + (static_cast<size_t>(y) * dstStridePixels + x) * 4u;
            px[0] = r;
            px[1] = g;
            px[2] = b;
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
                           bool mirrorX,
                           float zoom,
                           float enrollProgress) {
    if (!dstRgb565) return;
    renderIrPreviewCommon(
        gray,
        srcW,
        srcH,
        dstW,
        dstH,
        dstStridePixels,
        mirrorX,
        zoom,
        enrollProgress,
        [&](unsigned y, unsigned width) {
            uint16_t* row = dstRgb565 + static_cast<size_t>(y) * dstStridePixels;
            std::fill_n(row, width, static_cast<uint16_t>(0u));
        },
        [&](unsigned y, unsigned x, uint8_t r, uint8_t g, uint8_t b) {
            uint16_t* px = dstRgb565 + static_cast<size_t>(y) * dstStridePixels + x;
            *px = static_cast<uint16_t>(((static_cast<uint16_t>(r >> 3)) << 11) |
                                        ((static_cast<uint16_t>(g >> 2)) << 5) |
                                        static_cast<uint16_t>(b >> 3));
        });
}

}  // namespace spx::face
