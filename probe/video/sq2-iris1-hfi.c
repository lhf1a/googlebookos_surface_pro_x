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
static const u32 saved_offsets[] = {
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
 u32 saved[ARRAY_SIZE(saved_offsets)];
 bool configured;
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

static void diagnose(struct sq2_hfi_transport *ctx, const char *stage)
{
 static const u32 offsets[] = {
  /* Do not read TZ-owned e1020..e1034 protection thresholds or reset.
   * Secure PAS boot delegates those to firmware. A diagnostic threshold
   * read after e3000 was followed by board loss; recovery requires reboot.
   */
  0xe2014, 0xe000c, 0xe0010, 0xd2048, 0xd2050,
  0xd2054, 0xd2058, 0xd2064, 0xd2068, 0xdf008, 0xdf010,
 };
 unsigned int i;
 for (i = 0; i < ARRAY_SIZE(offsets); i++) {
  u32 value;
  pr_info("SQ2_HFI_DIAG_BEGIN stage=%s offset=%x\n", stage, offsets[i]);
  value = readl(ctx->base + offsets[i]);
  pr_info("SQ2_HFI_DIAG stage=%s offset=%x value=%08x\n", stage, offsets[i], value);
 }
 dma_rmb();
 pr_info("SQ2_HFI_SFR stage=%s size=%u first=%08x msg_w=%u dbg_w=%u\n", stage,
         *(u32 *)(ctx->cpu + SFR_OFFSET), *(u32 *)(ctx->cpu + SFR_OFFSET + 4),
         READ_ONCE(ctx->q[1]->write), READ_ONCE(ctx->q[2]->write));
}
int sq2_hfi_run(struct sq2_hfi_transport *ctx, void __iomem *base, bool protect_iova)
{
 u32 status;
 bool done = false;
 int ret, i;
 ctx->base = base;
 if (protect_iova) {
  /* Exact sdmshrike namespace from pinned nonsecure and secure_non_pixel
   * virtual-addr-pools. This programs VPU virtual thresholds, not RAM ownership.
   */
  pr_info("SQ2_HFI_PROTECT_BEGIN\n");
  ret = qcom_scm_mem_protect_video_var(0, 0x25800000, 0x1000000, 0x24800000);
  pr_info("SQ2_HFI_PROTECT cp=0..25800000 nonpixel=1000000+24800000 ret=%d\n", ret);
  if (ret) return ret;
 }
 diagnose(ctx, "before");
 for (i = 0; i < ARRAY_SIZE(saved_offsets); i++)
  ctx->saved[i] = readl(base + saved_offsets[i]);
 ctx->configured = true;
 writel(ctx->dma, base + 0xd2064);
 writel(REGION_SIZE, base + 0xd2068);
 writel(ctx->dma, base + 0xd2054);
 writel(1, base + 0xd2050);
 writel(ctx->dma + SFR_OFFSET, base + 0xd205c);
 writel(0, base + 0xd2060);
 writel(ctx->dma, base + 0xd2034);
 writel(ctx->dma, base + 0xd2038);
 writel(REGION_SIZE, base + 0xd203c);
 dma_wmb();
 pr_info("SQ2_HFI_BOOT begin iova=%pad uc_bytes=%u table_bytes=%zu header=%zu IRQ_MASK_UNCHANGED\n",
         &ctx->dma, REGION_SIZE, (size_t)TABLE_BYTES, sizeof(struct qtable));
 writel(1, base + 0xd2048);
 writel(1, base + 0xd2058);
 ret = readl_poll_timeout(base + 0xd204c, status, status != 0, 1000, 1000000);
 pr_info("SQ2_HFI_BOOT status=%08x ret=%d\n", status, ret);
 diagnose(ctx, "boot_result");
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
  for (i = 0; i < ARRAY_SIZE(saved_offsets); i++)
   writel(ctx->saved[i], ctx->base + saved_offsets[i]);
  writel(1, ctx->base + 0xd201c);
  writel(readl(ctx->base + 0xe000c), ctx->base + 0xe0014);
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
