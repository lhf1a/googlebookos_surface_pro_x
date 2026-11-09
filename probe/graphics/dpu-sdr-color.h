/* SPDX-License-Identifier: GPL-2.0-only */
/* BT.709 limited coefficients match public QTI drm_plane.cpp's 10-bit CSC table,
 * converted from S31.32 to the Linux block's S15.16; clamps/bias use block depth. */
#ifndef DPU_SDR_COLOR_H
#define DPU_SDR_COLOR_H
static const struct dpu_csc_cfg dpu_sdr709_8 = {
    {0x12a00, 0, 0x1cb00, 0x12a00, 0xffffc980, 0xffff7780, 0x12a00, 0x21d00, 0},
    {0xfff0, 0xff80, 0xff80}, {0, 0, 0},
    {0x10, 0xeb, 0x10, 0xf0, 0x10, 0xf0}, {0, 0xff, 0, 0xff, 0, 0xff},
};
static const struct dpu_csc_cfg dpu_sdr709_10 = {
    {0x12a00, 0, 0x1cb00, 0x12a00, 0xffffc980, 0xffff7780, 0x12a00, 0x21d00, 0},
    {0xffc0, 0xfe00, 0xfe00}, {0, 0, 0},
    {0x40, 0x3ac, 0x40, 0x3c0, 0x40, 0x3c0}, {0, 0x3ff, 0, 0x3ff, 0, 0x3ff},
};
static inline const struct dpu_csc_cfg *
dpu_sdr_color_csc(enum drm_color_encoding encoding, enum drm_color_range range, bool ten_bit)
{
    if (range != DRM_COLOR_YCBCR_LIMITED_RANGE)
        return NULL;
    if (encoding == DRM_COLOR_YCBCR_BT601)
        return ten_bit ? &dpu_csc10_YUV2RGB_601L : &dpu_csc_YUV2RGB_601L;
    if (encoding == DRM_COLOR_YCBCR_BT709)
        return ten_bit ? &dpu_sdr709_10 : &dpu_sdr709_8;
    return NULL;
}
#endif
