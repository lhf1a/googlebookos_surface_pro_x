/* SPDX-License-Identifier: GPL-2.0-only */
/* Included in vdec.c. Backing allocations and imported mappings survive VB2
 * teardown until their bound session END or PAS stop is confirmed. Objects
 * reused by a new session are rebound at HFI submission, not allocation.
 * Standard platforms retain vb2_dma_contig_memops unchanged.
 */
#ifndef VENUS_SQ2_VB2_H
#define VENUS_SQ2_VB2_H
#ifndef SQ2_VENUS_VB2_HOST_TEST
#include <linux/dma-buf.h>
#endif
#include "sq2-venus-retire.h"
struct sq2_vb2_lease {
 struct sq2_venus_retired retired;
 struct venus_core *core;
 void *backing;
 struct dma_buf *dbuf;
 bool mapped;
 unsigned long charged;
};

static void sq2_vb2_release(struct sq2_venus_retired *entry)
{
 struct sq2_vb2_lease *lease = container_of(entry, struct sq2_vb2_lease, retired);
 /* These allocator operations do not dereference the old vb2_buffer. */
 if (lease->dbuf) {
  if (lease->mapped) vb2_dma_contig_memops.unmap_dmabuf(lease->backing);
  vb2_dma_contig_memops.detach_dmabuf(lease->backing);
  dma_buf_put(lease->dbuf);
 } else {
  vb2_dma_contig_memops.put(lease->backing);
 }
 sq2_venus_vb2_unreserve(lease->core, lease->charged);
 kfree(lease);
 module_put(THIS_MODULE);
}

static struct sq2_vb2_lease *sq2_vb2_new(struct vb2_buffer *vb, unsigned long bytes)
{
 struct venus_inst *inst = vb2_get_drv_priv(vb->vb2_queue);
 struct sq2_vb2_lease *lease;
 int ret;
 if (!try_module_get(THIS_MODULE)) return ERR_PTR(-ENODEV);
 lease = kzalloc(sizeof(*lease), GFP_KERNEL);
 if (!lease) { module_put(THIS_MODULE); return ERR_PTR(-ENOMEM); }
 lease->core = inst->core;
 ret = sq2_venus_vb2_reserve(inst->core, bytes);
 if (ret) { kfree(lease); module_put(THIS_MODULE); return ERR_PTR(ret); }
 lease->charged = bytes;
 INIT_LIST_HEAD(&lease->retired.list);
 lease->retired.release = sq2_vb2_release;
 return lease;
}

static void sq2_vb2_failed(struct sq2_vb2_lease *lease)
{
 sq2_venus_vb2_unreserve(lease->core, lease->charged);
 kfree(lease);
 module_put(THIS_MODULE);
}

static void *sq2_vb2_alloc(struct vb2_buffer *vb, struct device *dev, unsigned long size)
{
 struct sq2_vb2_lease *lease = sq2_vb2_new(vb, size);
 void *backing;
 if (IS_ERR(lease)) return lease;
 backing = vb2_dma_contig_memops.alloc(vb, dev, size);
 if (IS_ERR_OR_NULL(backing)) { sq2_vb2_failed(lease); return backing; }
 lease->backing = backing;
 return lease;
}

static void *sq2_vb2_attach(struct vb2_buffer *vb, struct device *dev,
                           struct dma_buf *dbuf, unsigned long size)
{
 /* The attachment maps the exporter's full sg_table, not just size bytes. */
 struct sq2_vb2_lease *lease = sq2_vb2_new(vb, dbuf->size);
 void *backing;
 if (IS_ERR(lease)) return lease;
 /* VB2 drops its reference after detach, even when our release is deferred. */
 get_dma_buf(dbuf);
 backing = vb2_dma_contig_memops.attach_dmabuf(vb, dev, dbuf, size);
 if (IS_ERR_OR_NULL(backing)) {
  dma_buf_put(dbuf); sq2_vb2_failed(lease); return backing;
 }
 lease->backing = backing;
 lease->dbuf = dbuf;
 return lease;
}

static int sq2_vb2_map(void *mem)
{
 struct sq2_vb2_lease *lease = mem;
 int ret;
 if (lease->mapped) return 0;
 ret = vb2_dma_contig_memops.map_dmabuf(lease->backing);
 if (!ret) lease->mapped = true;
 return ret;
}

static void sq2_vb2_unmap(void *mem)
{
 /* Keep the same mapping even across dqbuf/requeue. Detach transfers it to
  * the core retirement list; confirmed session END or PAS stop releases it. */
 (void)mem;
}

static void sq2_vb2_retire(void *mem)
{
 struct sq2_vb2_lease *lease = mem;
 sq2_venus_retire(lease->core, &lease->retired);
}

static void sq2_vb2_prepare(void *mem)
{ struct sq2_vb2_lease *l = mem; vb2_dma_contig_memops.prepare(l->backing); }
static void sq2_vb2_finish(void *mem)
{ struct sq2_vb2_lease *l = mem; vb2_dma_contig_memops.finish(l->backing); }
static void *sq2_vb2_cookie(struct vb2_buffer *vb, void *mem)
{ struct sq2_vb2_lease *l = mem; return vb2_dma_contig_memops.cookie(vb, l->backing); }
static void *sq2_vb2_vaddr(struct vb2_buffer *vb, void *mem)
{ struct sq2_vb2_lease *l = mem; return vb2_dma_contig_memops.vaddr(vb, l->backing); }
static unsigned int sq2_vb2_users(void *mem)
{ struct sq2_vb2_lease *l = mem; return vb2_dma_contig_memops.num_users(l->backing); }
static int sq2_vb2_mmap(void *mem, struct vm_area_struct *vma)
{ struct sq2_vb2_lease *l = mem; return vb2_dma_contig_memops.mmap(l->backing, vma); }
static struct dma_buf *sq2_vb2_export(struct vb2_buffer *vb, void *mem, unsigned long flags)
{ struct sq2_vb2_lease *l = mem; return vb2_dma_contig_memops.get_dmabuf(vb, l->backing, flags); }

static const struct vb2_mem_ops sq2_vb2_memops = {
 .alloc = sq2_vb2_alloc, .put = sq2_vb2_retire, .get_dmabuf = sq2_vb2_export,
 .attach_dmabuf = sq2_vb2_attach, .detach_dmabuf = sq2_vb2_retire,
 .map_dmabuf = sq2_vb2_map, .unmap_dmabuf = sq2_vb2_unmap,
 .prepare = sq2_vb2_prepare, .finish = sq2_vb2_finish,
 .cookie = sq2_vb2_cookie, .vaddr = sq2_vb2_vaddr,
 .num_users = sq2_vb2_users, .mmap = sq2_vb2_mmap,
};
#endif
