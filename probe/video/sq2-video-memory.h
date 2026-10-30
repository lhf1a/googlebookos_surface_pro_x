#pragma once
#include <linux/videodev2.h>
#include <cstdint>

// Native capture storage only; compressed input and optional mapping caches
// have separate limits. P010 needs two bytes per sample instead of one.
inline constexpr uint64_t sq2CaptureFrameLimit(uint32_t format) {
 return format == V4L2_PIX_FMT_P010 ? 32ull*1024*1024 : 16ull*1024*1024;
}
inline constexpr uint64_t sq2CapturePoolLimit(uint32_t format) {
 return format == V4L2_PIX_FMT_P010 ? 384ull*1024*1024 : 192ull*1024*1024;
}
