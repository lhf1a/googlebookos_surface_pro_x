/* SPDX-License-Identifier: GPL-2.0-only */
/* Versioned hardware/driver facts; format support remains in IN_FORMATS. */
#ifndef DPU_CAPS_H
#define DPU_CAPS_H

#include <linux/seq_buf.h>
#include <linux/slab.h>
#include <linux/soc/qcom/ubwc.h>
#include <drm/drm_managed.h>
#include <drm/drm_property.h>
#include "dpu_kms.h"
#include "dpu_hw_sspp.h"

static inline void dpu_caps_blob_release(struct drm_device *dev, void *data)
{
	drm_property_blob_put(data);
}

static inline int dpu_caps_attach(struct drm_device *dev,
				 struct drm_mode_object *object,
				 struct seq_buf *buf)
{
	struct drm_property_blob *blob;
	struct drm_property *prop;
	int ret;

	if (seq_buf_has_overflowed(buf))
		return -EOVERFLOW;

	blob = drm_property_create_blob(dev, seq_buf_used(buf) + 1, buf->buffer);
	if (IS_ERR(blob))
		return PTR_ERR(blob);

	prop = drm_property_create(dev, DRM_MODE_PROP_BLOB | DRM_MODE_PROP_IMMUTABLE,
				   "QCOM_DPU_CAPS", 0);
	if (!prop) {
		drm_property_blob_put(blob);
		return -ENOMEM;
	}

	ret = drmm_add_action_or_reset(dev, dpu_caps_blob_release, blob);
	if (ret) {
		drm_property_destroy(dev, prop);
		return ret;
	}

	drm_object_attach_property(object, prop, blob->base.id);
	return 0;
}

static inline const char *dpu_caps_pipe_type(u32 type)
{
	switch (type) {
	case SSPP_TYPE_VIG:
		return "vig";
	case SSPP_TYPE_RGB:
		return "rgb";
	case SSPP_TYPE_DMA:
		return "dma";
	case SSPP_TYPE_CURSOR:
		return "cursor";
	default:
		return "unknown";
	}
}

static inline int dpu_caps_add_plane(struct drm_plane *plane,
				    const struct dpu_kms *kms,
				    const struct dpu_hw_sspp *sspp,
				    u32 max_upscale, u32 max_downscale,
				    u32 min_scale_16_16, u32 max_scale_16_16)
{
	const struct dpu_sspp_sub_blks *sblk;
	struct seq_buf buf;
	char *data;
	bool scaler, csc;
	int ret;

	data = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	seq_buf_init(&buf, data, PAGE_SIZE);
	seq_buf_printf(&buf, "version=1\nobject=plane\nphysical_pipe=%u\n", !!sspp);
	/* Virtual planes have no stable pipe; no physical limits may be inferred. */
	if (!sspp)
		goto attach;

	if (!sspp->cap || !sspp->cap->sblk || !kms->catalog || !kms->catalog->caps) {
		ret = -EINVAL;
		goto free_data;
	}
	sblk = sspp->cap->sblk;
	/* Match dpu_plane_atomic_check_pipe, not just the silicon feature bits. */
	scaler = sblk->scaler_blk.len && sspp->ops.setup_scaler;
	csc = sblk->csc_blk.len && sspp->ops.setup_csc;
	seq_buf_printf(&buf, "pipe_id=%u\npipe_type=%s\nscaler=%u\ncsc=%u\n",
		       sspp->cap->id, dpu_caps_pipe_type(sspp->cap->type), scaler, csc);
	seq_buf_printf(&buf, "scaler_hw_version=%u\nmax_linewidth=%u\n",
		       sblk->scaler_blk.version, kms->catalog->caps->max_linewidth);
	seq_buf_printf(&buf, "max_upscale=%u\nmax_downscale=%u\n"
		       "min_scale_16_16=%u\nmax_scale_16_16=%u\n",
		       scaler ? max_upscale : 1, scaler ? max_downscale : 1,
		       scaler ? min_scale_16_16 : 65536, scaler ? max_scale_16_16 : 65536);
	seq_buf_printf(&buf, "max_per_pipe_bw_kbps=%u\n", sblk->max_per_pipe_bw);
	/* These describe the standard plane path, not downstream scaler/CSC properties. */
	seq_buf_printf(&buf, "kernel_managed_scaler=%u\nkernel_managed_csc=%u\n"
		       "csc_bt601_limited_only=%u\n", scaler, csc, csc);
attach:
	ret = dpu_caps_attach(plane->dev, &plane->base, &buf);
free_data:
	kfree(data);
	return ret;
}

static inline int dpu_caps_add_crtc(struct drm_crtc *crtc, const struct dpu_kms *kms)
{
	const struct dpu_mdss_cfg *catalog = kms->catalog;
	const struct dpu_perf_cfg *perf;
	struct seq_buf buf;
	char *data;
	int ret;

	if (!catalog || !catalog->caps || !catalog->perf || !catalog->mdss_ver ||
	    !kms->mdss || !kms->perf.max_core_clk_rate)
		return -EINVAL;
	perf = catalog->perf;
	data = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	seq_buf_init(&buf, data, PAGE_SIZE);
	seq_buf_printf(&buf, "version=1\nobject=crtc\nhw_major=%u\nhw_minor=%u\nhw_version=%u\n",
		       catalog->mdss_ver->core_major_ver, catalog->mdss_ver->core_minor_ver,
		       kms->core_rev);
	seq_buf_printf(&buf, "max_mixer_width=%u\nmax_blendstages=%u\nhas_src_split=%u\n",
		       catalog->caps->max_mixer_width, catalog->caps->max_mixer_blendstages,
		       catalog->caps->has_src_split);
	seq_buf_printf(&buf, "max_bw_low_kbps=%u\nmax_bw_high_kbps=%u\n"
		       "max_core_clk_hz=%llu\nclk_inefficiency_pct=%u\nbw_inefficiency_pct=%u\n",
		       perf->max_bw_low, perf->max_bw_high, kms->perf.max_core_clk_rate,
		       perf->clk_inefficiency_factor, perf->bw_inefficiency_factor);
	seq_buf_printf(&buf, "kernel_bw_votes=1\nubwc_version=%u\nubwc_dec_version=%u\n",
		       kms->mdss->ubwc_enc_version, kms->mdss->ubwc_dec_version);
#define DPU_CAPS_PERF_FIELD(field) seq_buf_printf(&buf, #field "=%u\n", perf->field)
	DPU_CAPS_PERF_FIELD(min_core_ib);
	DPU_CAPS_PERF_FIELD(min_llcc_ib);
	DPU_CAPS_PERF_FIELD(min_dram_ib);
	DPU_CAPS_PERF_FIELD(undersized_prefill_lines);
	DPU_CAPS_PERF_FIELD(xtra_prefill_lines);
	DPU_CAPS_PERF_FIELD(dest_scale_prefill_lines);
	DPU_CAPS_PERF_FIELD(macrotile_prefill_lines);
	DPU_CAPS_PERF_FIELD(yuv_nv12_prefill_lines);
	DPU_CAPS_PERF_FIELD(linear_prefill_lines);
	DPU_CAPS_PERF_FIELD(downscaling_prefill_lines);
	DPU_CAPS_PERF_FIELD(amortizable_threshold);
	DPU_CAPS_PERF_FIELD(min_prefill_lines);
#undef DPU_CAPS_PERF_FIELD
	ret = dpu_caps_attach(crtc->dev, &crtc->base, &buf);
	kfree(data);
	return ret;
}

#endif
