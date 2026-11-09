/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VENUS_SQ2_CAPTURE_LAYOUT_H
#define VENUS_SQ2_CAPTURE_LAYOUT_H
/* Keep coded dimensions/crop separate from the client's linear allocation.
 * Constraints describe real firmware output; metadata alone cannot change it.
 */
static int sq2_capture_layout(u32 format, u32 width, u32 height,
                             u32 stride, u32 scan, u32 minimum, u32 *size)
{
 u32 bpp;
 if (format != V4L2_PIX_FMT_NV12 && format != V4L2_PIX_FMT_P010)
  return -EINVAL;
 bpp = format == V4L2_PIX_FMT_P010 ? 2 : 1;
 if (!width || !height || width > 8192 || height > 8192 ||
     stride < width * bpp || stride > 16384 || stride % 128 ||
     scan < height || scan > 8192 || scan % 32)
  return -EINVAL;
 *size = max(minimum, ALIGN(stride * scan * 3 / 2, 4096));
 return 0;
}
#endif
