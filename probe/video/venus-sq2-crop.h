/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VENUS_SQ2_CROP_H
#define VENUS_SQ2_CROP_H
/* A capture format switch after SOURCE_CHANGE must not replace the firmware's
 * visible rectangle with the allocation's aligned dimensions. No scaling or
 * guessed crop: preserve only an existing, bounded rectangle at identical size.
 */
static bool venus_sq2_keep_crop(struct venus_inst *inst, u32 type,
                              u32 width, u32 height)
{
 const struct v4l2_rect *r = &inst->crop;
 if (!IS_SQ2_HFI(inst->core) || !inst->reconfig ||
     type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ||
     width != inst->width || height != inst->height ||
     r->left < 0 || r->top < 0 || !r->width || !r->height)
  return false;
 return (u32)r->left < width && (u32)r->top < height &&
        r->width <= width - (u32)r->left &&
        r->height <= height - (u32)r->top;
}
#endif
