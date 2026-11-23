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

struct Rgb8 {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
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

// Convert Rotation enum to integer degrees (0, 90, 180, or 270).
int rotationToDegrees(Rotation rot);

// Compose base rotation with an additional clockwise delta rotation.
Rotation composeRotation(Rotation base, Rotation deltaCw);

// Rotate already-preprocessed IrFrameStats clockwise by deltaCw while preserving frame stats.
IrFrameStats rotateIrFrameStats(const IrFrameStats& src, Rotation deltaCw);

// Classify 3-axis Android accelerometer vector (m/s^2, natural landscape ROTATION_0 axes:
// +Y = top bezel/camera edge, +X = right edge, +Z = out of screen) into the CW sensor
// rotation delta needed on top of baseRot (persist.spx.face.rotation):
//   ay > 0 dominant (ROTATION_0, normal landscape)      -> Deg0
//   ax > 0 dominant (ROTATION_90, portrait, cam left)   -> Deg90CW
//   ay < 0 dominant (ROTATION_180, upside-down land)    -> Deg180
//   ax < 0 dominant (ROTATION_270, portrait, cam right) -> Deg270CW
// Returns prevDelta when flat on desk (hypot(ax, ay) < 2.8 m/s^2) or within diagonal
// hysteresis deadband (dominant < 1.35 * minor when prevDelta is present).
bool classifyGravityOrientation(float ax,
                                float ay,
                                float az,
                                bool hasPrevDelta,
                                Rotation prevDelta,
                                Rotation& outDelta);

// Map enrollment progress in [0.0, 1.0] to the progress bar fill color (blue while learning, green when complete).
Rgb8 enrollProgressFillColor(float progress);

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

// Render centered, optionally zoomed and horizontally mirrored IR preview scaled into RGBA8888 buffer.
// When enrollProgress >= 0.0f, draws a subtle bottom scrim and pill progress bar inside the visible square.
void renderIrPreviewRgba(const uint8_t* gray,
                         unsigned srcW,
                         unsigned srcH,
                         uint8_t* dstRgba,
                         unsigned dstW,
                         unsigned dstH,
                         unsigned dstStridePixels,
                         bool mirrorX = true,
                         float zoom = 1.0f,
                         float enrollProgress = -1.0f);

// Render centered, optionally zoomed and horizontally mirrored IR preview scaled into RGB565 buffer.
// When enrollProgress >= 0.0f, draws a subtle bottom scrim and pill progress bar inside the visible square.
void renderIrPreviewRgb565(const uint8_t* gray,
                           unsigned srcW,
                           unsigned srcH,
                           uint16_t* dstRgb565,
                           unsigned dstW,
                           unsigned dstH,
                           unsigned dstStridePixels,
                           bool mirrorX = true,
                           float zoom = 1.0f,
                           float enrollProgress = -1.0f);

}  // namespace spx::face
