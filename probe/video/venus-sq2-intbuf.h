/* SPDX-License-Identifier: GPL-2.0-only */
/* Included after struct intbuf in helpers.c. No callback retains inst/IDA. */
#ifndef VENUS_SQ2_INTBUF_H
#define VENUS_SQ2_INTBUF_H
static void *sq2_intbuf_alloc(struct venus_inst *inst, struct intbuf *buf)
{
 void *va;
 buf->sq2_core = inst->core;
 if (IS_SQ2_HFI(inst->core)) {
  if (sq2_venus_vb2_reserve(inst->core, buf->size)) return NULL;
  buf->sq2_charged = buf->size;
 }
 va = dma_alloc_attrs(inst->core->dev, buf->size, &buf->da, GFP_KERNEL, buf->attrs);
 if (!va && buf->sq2_charged) {
  sq2_venus_vb2_unreserve(inst->core, buf->sq2_charged);
  buf->sq2_charged = 0;
 }
 return va;
}

static void sq2_intbuf_dma_free(struct venus_inst *inst, struct intbuf *buf)
{
 dma_free_attrs(inst->core->dev, buf->size, buf->va, buf->da, buf->attrs);
 if (buf->sq2_charged) {
  sq2_venus_vb2_unreserve(buf->sq2_core, buf->sq2_charged);
  buf->sq2_charged = 0;
 }
}

static void sq2_intbuf_release(struct sq2_venus_retired *entry)
{
 struct intbuf *buf = container_of(entry, struct intbuf, sq2_retired);
 struct device *dev = buf->sq2_dev;
 dma_free_attrs(dev, buf->size, buf->va, buf->da, buf->attrs);
 if (buf->sq2_charged) sq2_venus_vb2_unreserve(buf->sq2_core, buf->sq2_charged);
 kfree(buf);
 put_device(dev);
}

static void sq2_intbuf_retire(struct venus_inst *inst, struct intbuf *buf)
{
 if (buf->list.next) list_del_init(&buf->list);
 else INIT_LIST_HEAD(&buf->list); /* failed SET before list insertion */
 buf->sq2_dev = get_device(inst->core->dev);
 INIT_LIST_HEAD(&buf->sq2_retired.list);
 buf->sq2_retired.release = sq2_intbuf_release;
 sq2_venus_retire(inst->core, &buf->sq2_retired);
}

/* Called after unlinking the HFI instance, before freeing inst. This includes
 * FIRMWARE-owned DPBs skipped by the stock normal free helper. They remain
 * DMA-mapped, with device and core ownership, until confirmed PAS stop.
 */
void venus_helper_sq2_retire_buffers(struct venus_inst *inst)
{
 struct intbuf *buf, *next;
 if (!IS_SQ2_HFI(inst->core)) return;
 list_for_each_entry_safe(buf, next, &inst->dpbbufs, list)
  sq2_intbuf_retire(inst, buf);
 list_for_each_entry_safe(buf, next, &inst->internalbufs, list)
  sq2_intbuf_retire(inst, buf);
}
EXPORT_SYMBOL_GPL(venus_helper_sq2_retire_buffers);
#endif
