// SPDX-License-Identifier: GPL-2.0-only
/* Private HFI transport foundation; DMA probe never starts the VPU. */
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include "sq2-iris1-hfi.h"
#include "sq2-hfi-ring.h"

#define REGION_SIZE (3 * SZ_1M)
#define QUEUE_BYTES (1024 * 50 * 16)
#define QUEUE_WORDS (QUEUE_BYTES / 4)
struct qtable {
 u32 version, size, qhdr0, qhdr_size, num_q, active_q;
 u64 device_cookie;
 char name[256];
};
struct qheader {
 u32 status, start, type, words, packet_size, drops;
 u32 rx_wm, tx_wm, rx_req, tx_req, rx_irq, tx_irq, read, write;
};
#define TABLE_BYTES (sizeof(struct qtable) + 3 * sizeof(struct qheader))
#define SFR_OFFSET ALIGN(TABLE_BYTES + 3 * QUEUE_BYTES, SZ_4K)
static const u32 mailbox_offsets[] = {
 0xd2064, 0xd2068, 0xd2054, 0xd2050, 0xd205c, 0xd2060,
 0xd2034, 0xd2038, 0xd203c, 0xd2048, 0xd2058,
};
struct sq2_hfi_transport {
 struct device *dev;
 struct iommu_domain *domain;
 void *cpu;
 dma_addr_t dma;
 u64 saved_limit, saved_mask, saved_coherent;
 struct qheader *q[3];
 u32 *ring[3];
 void __iomem *base;
 bool configured;
 unsigned int trace_delay_ms;
};
static struct sq2_hfi_transport *quarantined;
static_assert(sizeof(struct qtable) == 288);
static_assert(sizeof(struct qheader) == 56);
static_assert(SFR_OFFSET + SZ_4K <= REGION_SIZE);

static int count_peer(struct device *dev, void *data)
{
 ++*(unsigned int *)data;
 return 0;
}
int sq2_hfi_prepare(struct device *dev, struct sq2_hfi_transport **out)
{
 struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
 struct iommu_fwspec *spec = dev_iommu_fwspec_get(dev);
 struct iommu_group *group;
 unsigned int peers = 0, pages = 0;
 const size_t size = REGION_SIZE;
 struct sq2_hfi_transport *ctx;
 struct qtable *table;
 phys_addr_t first = 0, last = 0;
 size_t off;
 int ret;

 if (!out || *out || quarantined || !domain || !(domain->type & __IOMMU_DOMAIN_DMA_API) || !spec ||
     spec->num_ids != 1 || spec->ids[0] != 0x00601300)
  return -ENODEV;
 group = iommu_group_get(dev);
 if (!group) return -ENODEV;
 ret = iommu_group_for_each_dev(group, &peers, count_peer);
 iommu_group_put(group);
 if (ret || peers != 1) return -EBUSY;
 ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
 if (!ctx) return -ENOMEM;
 ctx->dev = dev; ctx->domain = domain;
 ctx->saved_limit = dev->bus_dma_limit;
 ctx->saved_mask = dma_get_mask(dev);
 ctx->saved_coherent = dev->coherent_dma_mask;
 ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
 if (ret) goto restore;
 dev->bus_dma_limit = min_t(u64, ctx->saved_limit ?: U64_MAX, 0xdfffffff);
 ctx->cpu = dma_alloc_coherent(dev, size, &ctx->dma, GFP_KERNEL | __GFP_NOWARN);
 if (!ctx->cpu) { ret = -ENOMEM; goto restore; }
 ret = -ERANGE;
 if (ctx->dma < 0x25800000 || ctx->dma + size > 0xe0000000ULL || !IS_ALIGNED(ctx->dma, SZ_1M))
  goto free;
 for (off = 0; off < size; off += PAGE_SIZE) {
  phys_addr_t phys = iommu_iova_to_phys(domain, ctx->dma + off);
  if (!phys || !IS_ALIGNED(phys, PAGE_SIZE)) goto free;
  if (!off) first = phys;
  last = phys;
  pages++;
 }
 memset(ctx->cpu, 0xa5, size);
 ret = memchr_inv(ctx->cpu, 0xa5, size) ? -EIO : 0;
 memset(ctx->cpu, 0, size);
 if (memchr_inv(ctx->cpu, 0, size)) ret = -EIO;
 pr_info("SQ2_HFI_DMA sid=%x domain_type=%x peers=%u iova=%pad bytes=%zu pages=%u first=%pa last=%pa cpu_fill_ret=%d NO_VPU_DMA\n",
         spec->ids[0], domain->type, peers, &ctx->dma, size, pages, &first, &last, ret);
 if (ret) goto free;
 table = ctx->cpu;
 table->size = TABLE_BYTES;
 table->qhdr0 = sizeof(*table);
 table->qhdr_size = sizeof(struct qheader);
 table->num_q = table->active_q = 3;
 strscpy(table->name, "sq2_hfi_private", sizeof(table->name));
 for (off = 0; off < 3; off++) {
  struct qheader *q = ctx->cpu + sizeof(*table) + off * sizeof(*q);
  ctx->q[off] = q;
  ctx->ring[off] = ctx->cpu + TABLE_BYTES + off * QUEUE_BYTES;
  q->status = 1;
  q->start = ctx->dma + TABLE_BYTES + off * QUEUE_BYTES;
  q->type = 0x01010000 | off;
  q->words = QUEUE_WORDS;
  q->rx_wm = q->tx_wm = 1;
  q->rx_req = off == 2 ? 0 : 1;
 }
 *(u32 *)(ctx->cpu + SFR_OFFSET) = SZ_4K;
 dma_wmb();
 *out = ctx;
 return 0;
free:
 dma_free_coherent(dev, size, ctx->cpu, ctx->dma);
restore:
 dev->bus_dma_limit = ctx->saved_limit;
 dma_set_mask(dev, ctx->saved_mask);
 dma_set_coherent_mask(dev, ctx->saved_coherent);
 kfree(ctx);
 return ret;
}

/* Read at most one bounded packet. Ring indices and lengths come from firmware
 * and must be checked before any offset or host consumer-index update.
 */
static int receive(struct sq2_hfi_transport *ctx, unsigned int index, bool *done)
{
 struct qheader *q = ctx->q[index];
 u32 r = READ_ONCE(q->read), w = READ_ONCE(q->write);
 struct sq2_hfi_packet p;
 int ret;
 u32 *ring = ctx->ring[index];
 if (r >= QUEUE_WORDS || w >= QUEUE_WORDS) return -EPROTO;
 if (r == w) return 0;
 dma_rmb();
 ret = sq2_hfi_packet_read(ring, QUEUE_WORDS, r, w, &p);
 if (ret < 0) return -EPROTO;
 if (!ret) return 0;
 pr_info("SQ2_HFI_RX q=%u bytes=%u type=%x word2=%x r=%u w=%u\n", index, p.bytes, p.type, p.word2, r, w);
 if (index == 1 && p.type == 0x20001) {
  if (p.bytes < 16 || p.word2) return -EIO;
  pr_info("SQ2_HFI_SYS_INIT_REPLY error=0 properties=%u\n", p.word3);
  *done = true;
 }
 dma_wmb();
 WRITE_ONCE(q->read, p.next);
 return 1;
}

static void queue_status(struct sq2_hfi_transport *ctx, const char *stage)
{
 /* CPU-owned queue memory only. No extra MMIO diagnostics: neither the
  * TZ thresholds nor the VPU-private interrupt controller may be readable
  * by the application CPU after PAS authentication. Do not expand reads
  * from register definitions alone; they may be used only by non-TZ boot.
  */
 dma_rmb();
 pr_info("SQ2_HFI_SFR stage=%s size=%u first=%08x msg_w=%u dbg_w=%u\n", stage,
         *(u32 *)(ctx->cpu + SFR_OFFSET), *(u32 *)(ctx->cpu + SFR_OFFSET + 4),
         READ_ONCE(ctx->q[1]->write), READ_ONCE(ctx->q[2]->write));
}
static void hfi_trace_barrier(struct sq2_hfi_transport *ctx)
{
 if (ctx->trace_delay_ms) msleep(ctx->trace_delay_ms);
}
int sq2_hfi_run(struct sq2_hfi_transport *ctx, void __iomem *base, bool protect_iova,
                unsigned int trace_delay_ms)
{
 const u32 mailbox_values[] = {
  ctx->dma, REGION_SIZE, ctx->dma, 1, ctx->dma + SFR_OFFSET, 0,
  ctx->dma, ctx->dma, REGION_SIZE,
 };
 u32 status;
 bool done = false;
 int ret, i;
 static_assert(ARRAY_SIZE(mailbox_values) + 2 == ARRAY_SIZE(mailbox_offsets));
 if (trace_delay_ms > 100) return -EINVAL;
 ctx->trace_delay_ms = trace_delay_ms;
 ctx->base = base;
 if (protect_iova) {
  /* Exact sdmshrike namespace from pinned nonsecure and secure_non_pixel
   * virtual-addr-pools. This programs VPU virtual thresholds, not RAM ownership.
   */
  pr_info("SQ2_HFI_PROTECT_BEGIN\n");
  hfi_trace_barrier(ctx);
  ret = qcom_scm_mem_protect_video_var(0, 0x25800000, 0x1000000, 0x24800000);
  pr_info("SQ2_HFI_PROTECT cp=0..25800000 nonpixel=1000000+24800000 ret=%d\n", ret);
  if (ret) return ret;
  hfi_trace_barrier(ctx);
 }
 queue_status(ctx, "before");
 /* The downstream AP path writes these mailboxes but does not read them.
  * Do not read write-only or protected mailbox registers to save state.
  * This private exclusive owner clears its pointers after confirmed stop.
  */
 ctx->configured = true;
 pr_info("SQ2_HFI_MAILBOX_PUBLISH_BEGIN\n");
 hfi_trace_barrier(ctx);
 for (i = 0; i < ARRAY_SIZE(mailbox_values); i++) {
  pr_info("SQ2_HFI_MAILBOX_WRITE_BEGIN offset=%x value=%08x\n", mailbox_offsets[i], mailbox_values[i]);
  hfi_trace_barrier(ctx);
  writel(mailbox_values[i], base + mailbox_offsets[i]);
  pr_info("SQ2_HFI_MAILBOX_WRITE_END offset=%x\n", mailbox_offsets[i]);
  hfi_trace_barrier(ctx);
 }
 dma_wmb();
 pr_info("SQ2_HFI_BOOT begin iova=%pad uc_bytes=%u table_bytes=%zu header=%zu IRQ_MASK_UNCHANGED\n",
         &ctx->dma, REGION_SIZE, (size_t)TABLE_BYTES, sizeof(struct qtable));
 hfi_trace_barrier(ctx);
 writel(1, base + 0xd2048);
 writel(1, base + 0xd2058);
 ret = readl_poll_timeout(base + 0xd204c, status, status != 0, 1000, 1000000);
 pr_info("SQ2_HFI_BOOT status=%08x ret=%d\n", status, ret);
 queue_status(ctx, "boot_result");
 if (ret) return ret;
 if (status & 0xfe) return -EIO;
 ctx->ring[0][0] = 12;
 ctx->ring[0][1] = 0x10001;
 ctx->ring[0][2] = 1; /* HFI_VIDEO_ARCH_OX */
 dma_wmb();
 WRITE_ONCE(ctx->q[0]->write, 3);
 dma_wmb();
 writel(BIT(15), base + 0xdf018);
 pr_info("SQ2_HFI_SYS_INIT_SENT words=3\n");
 for (i = 0; i < 2000 && !done; i++) {
  unsigned int q;
  for (q = 1; q < 3; q++) {
   unsigned int packets = 0;
   do {
    ret = receive(ctx, q, &done);
    if (ret < 0) return ret;
   } while (ret > 0 && !done && ++packets < 16);
  }
  if (!done) usleep_range(1000, 1500);
 }
 pr_info("SQ2_HFI_DONE reply=%d cmd_r=%u cmd_w=%u msg_r=%u msg_w=%u status=%08x\n", done,
         READ_ONCE(ctx->q[0]->read), READ_ONCE(ctx->q[0]->write),
         READ_ONCE(ctx->q[1]->read), READ_ONCE(ctx->q[1]->write), readl(base + 0xd204c));
 return done ? 0 : -ETIMEDOUT;
}
int sq2_hfi_finish(struct sq2_hfi_transport *ctx)
{
 int i, ret = 0;
 if (!ctx) return 0;
 if (ctx->configured) {
  for (i = 0; i < ARRAY_SIZE(mailbox_offsets); i++)
   writel(0, ctx->base + mailbox_offsets[i]);
  writel(1, ctx->base + 0xd201c);
  writel(0x14, ctx->base + 0xe0014); /* Owned CPU/watchdog pending bits. */
 }
 dma_free_coherent(ctx->dev, REGION_SIZE, ctx->cpu, ctx->dma);
 if (iommu_iova_to_phys(ctx->domain, ctx->dma)) ret = -EIO;
 pr_info("SQ2_HFI_DMA freed first_iova_unmapped=%d ret=%d\n",
         !iommu_iova_to_phys(ctx->domain, ctx->dma), ret);
 ctx->dev->bus_dma_limit = ctx->saved_limit;
 dma_set_mask(ctx->dev, ctx->saved_mask);
 dma_set_coherent_mask(ctx->dev, ctx->saved_coherent);
 kfree(ctx);
 return ret;
}
void sq2_hfi_quarantine(struct sq2_hfi_transport *ctx)
{
 quarantined = ctx;
 pr_err("SQ2_HFI_QUARANTINE queue allocation/mapping retained until reboot\n");
}
int sq2_hfi_dma_probe(struct device *dev)
{
 struct sq2_hfi_transport *ctx = NULL;
 int ret = sq2_hfi_prepare(dev, &ctx);
 return ret ? ret : sq2_hfi_finish(ctx);
}
