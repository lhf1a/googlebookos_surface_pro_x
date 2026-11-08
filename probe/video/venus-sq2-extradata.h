/* SPDX-License-Identifier: GPL-2.0-only */
/* Private opt-in ABI: plane 1 contains firmware HFI extradata.
 * Property values and two-word payload match downstream vidc_hfi.h and packetizer.
 */
#ifndef VENUS_SQ2_EXTRADATA_H
#define VENUS_SQ2_EXTRADATA_H
#define SQ2_CID_HDR_EXTRADATA 0x00982f00
#define SQ2_EXTRA_VUI 1U
#define SQ2_EXTRA_MASTERING 2U
#define SQ2_EXTRA_LIGHT 4U
#define SQ2_EXTRA_MASK 7U
#define SQ2_HFI_EXTRA_VUI 0x0120301bU
#define SQ2_HFI_EXTRA_MASTERING 0x0120301eU
#define SQ2_HFI_EXTRA_LIGHT 0x0120301fU
static inline u32 sq2_extra_property(unsigned bit)
{
	switch (bit) {
	case SQ2_EXTRA_VUI: return SQ2_HFI_EXTRA_VUI;
	case SQ2_EXTRA_MASTERING: return SQ2_HFI_EXTRA_MASTERING;
	case SQ2_EXTRA_LIGHT: return SQ2_HFI_EXTRA_LIGHT;
	default: return 0;
	}
}
struct sq2_extra_config { u32 enable, index; };
/* DPB owns pixels plus the metadata tail in one retirement allocation. */
static inline int sq2_extra_dpb_size(u32 pixels, u32 extra, u32 alignment, u32 *total)
{
	if (!alignment || (alignment & (alignment - 1)))
		return -EINVAL;
	if (!pixels || extra > ~(u32)0 - pixels ||
	    pixels + extra > ~(u32)0 - (alignment - 1))
		return -EOVERFLOW;
	*total = (pixels + extra + alignment - 1) & ~(alignment - 1);
	return 0;
}


#ifdef SQ2_EXTRADATA_DECODER
static int sq2_extra_query(struct venus_inst *inst)
{
	union hfi_get_property prop;
	enum hfi_version ver = inst->core->res->hfi_version;
	u32 largest = 0;
	unsigned int i;
	int ret;
	if (!inst->sq2_extra_mask)
		return 0;
	ret = hfi_session_get_property(inst, HFI_PROPERTY_CONFIG_BUFFER_REQUIREMENTS, &prop);
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(prop.bufreq); i++) {
		struct hfi_buffer_requirements *r = &prop.bufreq[i];
		if (r->type == HFI_BUFFER_EXTRADATA_OUTPUT(ver) ||
		    r->type == HFI_BUFFER_EXTRADATA_OUTPUT2(ver))
			largest = max(largest, r->size);
	}
	if (!largest)
		return -ENODATA;
	inst->sq2_extra_size = largest;
	return 0;
}
static int sq2_extra_enable(struct venus_inst *inst)
{
	unsigned int bit;
	int ret;
	if (!inst->sq2_extra_mask)
		return 0;
	if (!IS_SQ2_HFI(inst->core) ||
	    (inst->hfi_codec != HFI_VIDEO_CODEC_HEVC &&
	     (inst->hfi_codec != HFI_VIDEO_CODEC_H264 ||
	      inst->sq2_extra_mask != SQ2_EXTRA_VUI)))
		return -EOPNOTSUPP;
	for (bit = 1; bit <= SQ2_EXTRA_LIGHT; bit <<= 1) {
		u32 property = sq2_extra_property(bit);
		struct sq2_extra_config config = { .enable = 1, .index = property };
		if (!(inst->sq2_extra_mask & bit))
			continue;
		ret = hfi_session_set_property(inst, property, &config);
		if (ret)
			return ret;
	}
	return sq2_extra_query(inst);
}
#endif

#ifdef SQ2_EXTRADATA_BUFFERS
static int sq2_extra_validate(struct venus_inst *inst, struct vb2_buffer *vb)
{
	unsigned int i;
	bool extra = inst->session_type == VIDC_SESSION_TYPE_DEC &&
		vb->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && inst->sq2_extra_mask;
	if (vb->num_planes != (extra ? 2 : 1))
		return -EINVAL;
	for (i = 0; i < vb->num_planes; i++)
		if (!sq2_venus_buffer_valid(vb2_dma_contig_plane_dma_addr(vb, i),
					   vb2_plane_size(vb, i)))
			return -ERANGE;
	if (extra && (!inst->sq2_extra_size ||
	    vb2_plane_size(vb, 1) < inst->sq2_extra_size || vb->planes[1].data_offset))
		return -EINVAL;
	return 0;
}
#endif
#endif
