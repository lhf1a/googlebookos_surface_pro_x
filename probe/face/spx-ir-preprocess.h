// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace spx::face {

inline constexpr uint32_t kPixFmtY10 = 0x20303159u;   // 'Y10 ' (16-bit LE, 10 valid LSBs)
inline constexpr uint32_t kPixFmtY10P = 0x50303159u;  // 'Y10P' (MIPI packed 5 bytes / 4 px)

enum class Rotation : int {
    Deg0 = 0,
    Deg90CW = 90,
    Deg180 = 180,
    Deg270CW = 270,
};

struct IrFrameStats {
    unsigned width = 0;
    unsigned height = 0;
    uint16_t p1 = 0;
    uint16_t p50 = 0;
    uint16_t p99 = 0;
    float roiLinearMean = 0.0f;
    float darkRatio = 0.0f;
    float saturatedRatio = 0.0f;
    std::vector<uint16_t> upright10;
    std::vector<uint8_t> gray8;
};

// Parse integer degrees ("0", "90", "180", "270") to Rotation; falls back to Deg90CW.
Rotation parseRotationDegrees(const char* text, Rotation fallback = Rotation::Deg90CW);

// Unpack Y10/Y10P raw IR buffer, apply sensor rotation, compute center-ROI
// 10-bit statistics for AE, and normalize to 8-bit grayscale.
IrFrameStats preprocessIrFrame(const uint8_t* raw,
                               size_t size,
                               unsigned srcWidth,
                               unsigned srcHeight,
                               unsigned stride,
                               uint32_t v4l2PixFmt,
                               Rotation rot);

// Convert 8-bit grayscale buffer into R8G8B8A8_UNORM bytes for ANativeWindow preview.
void gray8ToRgba(const uint8_t* gray,
                 unsigned width,
                 unsigned height,
                 uint8_t* dstRgba,
                 unsigned dstStridePixels);

}  // namespace spx::face
