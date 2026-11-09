/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual v20 DPB functions; retain the old branch to test the generator repair. */
static void free_dpb_buf(struct venus_inst *inst, struct intbuf *buf)
{
	if (IS_SQ2_HFI(inst->core)) {
		/* Do not recycle a tag still visible to live firmware. */
		sq2_intbuf_retire(inst, buf);
		return;
	}
	ida_free(&inst->dpb_ids, buf->dpb_out_tag);

	list_del_init(&buf->list);
	sq2_intbuf_dma_free(inst, buf);
	kfree(buf);
}
int venus_helper_free_dpb_bufs(struct venus_inst *inst)
{
	struct intbuf *buf, *n;

	list_for_each_entry_safe(buf, n, &inst->dpbbufs, list) {
		if (buf->owned_by == FIRMWARE &&
		    (!IS_SQ2_HFI(inst->core) ||
		     !sq2_venus_scope_ended(inst, &buf->sq2_retired)))
			continue;
		free_dpb_buf(inst, buf);
	}

	if (list_empty(&inst->dpbbufs))
		INIT_LIST_HEAD(&inst->dpbbufs);

	return 0;
}
int venus_helper_alloc_dpb_bufs(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	struct device *dev = core->dev;
	enum hfi_version ver = core->res->hfi_version;
	struct hfi_buffer_requirements bufreq;
	u32 buftype = inst->dpb_buftype;
	unsigned int dpb_size = 0;
	struct intbuf *buf;
	unsigned int i;
	u32 count;
	int ret;
	int id;

	/* no need to allocate dpb buffers */
	if (!inst->dpb_fmt)
		return 0;

	if (inst->dpb_buftype == HFI_BUFFER_OUTPUT)
		dpb_size = inst->output_buf_size;
	else if (inst->dpb_buftype == HFI_BUFFER_OUTPUT2)
		dpb_size = inst->output2_buf_size;

	if (!dpb_size)
		return 0;

	ret = venus_helper_get_bufreq(inst, buftype, &bufreq);
	if (ret)
		return ret;

	count = HFI_BUFREQ_COUNT_MIN(&bufreq, ver);

	for (i = 0; i < count; i++) {
		buf = kzalloc(sizeof(*buf), GFP_KERNEL);
		if (!buf) {
			ret = -ENOMEM;
			goto fail;
		}

		buf->type = buftype;
		buf->size = dpb_size;
		if (inst->sq2_extra_mask) {
			u32 total;
			ret = sq2_extra_dpb_size(dpb_size, inst->sq2_extra_size, PAGE_SIZE, &total);
			if (ret) {
				kfree(buf);
				goto fail;
			}
			buf->size = total;
		}
		buf->attrs = DMA_ATTR_WRITE_COMBINE |
			     DMA_ATTR_NO_KERNEL_MAPPING;
		buf->va = sq2_intbuf_alloc(inst, buf);
		if (!buf->va) {
			ret = -ENOMEM;
			goto fail;
		}
		if (IS_SQ2_HFI(inst->core) && !sq2_venus_buffer_valid(buf->da, buf->size)) {
			sq2_intbuf_dma_free(inst, buf);
			ret = -ERANGE;
			goto fail;
		}
		buf->owned_by = DRIVER;

		id = ida_alloc_min(&inst->dpb_ids, VB2_MAX_FRAME, GFP_KERNEL);
		if (id < 0) {
			if (IS_SQ2_HFI(inst->core))
				sq2_intbuf_dma_free(inst, buf);
			ret = id;
			goto fail;
		}

		buf->dpb_out_tag = id;

		list_add_tail(&buf->list, &inst->dpbbufs);
	}

	return 0;

fail:
	kfree(buf);
	venus_helper_free_dpb_bufs(inst);
	return ret;
}
