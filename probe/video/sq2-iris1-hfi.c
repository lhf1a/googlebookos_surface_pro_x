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
#include <linux/of.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include "sq2-iris1-hfi.h"
#include "sq2-hfi-ring.h"
#include "sq2-hfi-caps.h"

#define PKT_WORDS (SQ2_HFI_MAX_PACKET_BYTES / 4)
#define SESSION_COOKIE 0x53513201
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
 bool empty_ping;
 bool windows_reference, ctrl_started, version_written;
 bool capture_caps, session, session_open;
 u32 dec_codecs;
 u32 *pkt;
 size_t bytes;
 unsigned int mailbox_count;
 u32 header_bytes, table_bytes;
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
int sq2_hfi_prepare(struct device *dev, struct sq2_hfi_transport **out,
                    bool compact_table, bool empty_ping, bool capture_caps, bool session)
{
 struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
 struct iommu_fwspec *spec = dev_iommu_fwspec_get(dev);
 struct iommu_group *group;
 unsigned int peers = 0, pages = 0;
 bool windows_reference = of_property_read_bool(dev->of_node, "microsoft,windows-hfi-reference");
 const size_t size = windows_reference ? 16 * SZ_1M : REGION_SIZE;
 struct sq2_hfi_transport *ctx;
 struct qtable *table;
 phys_addr_t first = 0, last = 0;
 size_t off;
 int ret;

 if (session && !capture_caps) return -EINVAL;
 if (!out || *out || quarantined || !domain || !(domain->type & __IOMMU_DOMAIN_DMA_API) || !spec ||
     (windows_reference ? (spec->num_ids != 3 || spec->ids[0] != 0x2300 ||
                            spec->ids[1] != 0x2320 || spec->ids[2] != 0x2340) :
                          (spec->num_ids != 1 || spec->ids[0] != 0x00601300)))
  return -ENODEV;
 group = iommu_group_get(dev);
 if (!group) return -ENODEV;
 ret = iommu_group_for_each_dev(group, &peers, count_peer);
 iommu_group_put(group);
 if (ret || peers != 1) return -EBUSY;
 ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
 if (!ctx) return -ENOMEM;
 ctx->pkt = kzalloc(SQ2_HFI_MAX_PACKET_BYTES, GFP_KERNEL);
 if (!ctx->pkt) { kfree(ctx); return -ENOMEM; }
 ctx->capture_caps = capture_caps;
 ctx->session = session;
 ctx->dev = dev; ctx->domain = domain;
 ctx->windows_reference = windows_reference;
 ctx->bytes = size;
 compact_table |= windows_reference;
 ctx->empty_ping = empty_ping;
 /* The original Venus protocol has six u32 fields (24 bytes). The pinned
  * later downstream driver extends this with an opaque host cookie/name.
  * Compare both documented host layouts; do not assume the SQ2 Windows
  * firmware accepts the later extension, even with qhdr0 advertised.
  */
 ctx->header_bytes = compact_table ? 24 : sizeof(struct qtable);
 ctx->table_bytes = ctx->header_bytes + 3 * sizeof(struct qheader);
 ctx->saved_limit = dev->bus_dma_limit;
 ctx->saved_mask = dma_get_mask(dev);
 ctx->saved_coherent = dev->coherent_dma_mask;
 ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
 if (ret) goto restore;
 dev->bus_dma_limit = min_t(u64, ctx->saved_limit ?: U64_MAX,
                           windows_reference ? 0xbfffffff : 0xdfffffff);
 ctx->cpu = dma_alloc_coherent(dev, size, &ctx->dma, GFP_KERNEL | __GFP_NOWARN);
 if (!ctx->cpu) { ret = -ENOMEM; goto restore; }
 ret = -ERANGE;
 /* Reference allocation lies in the intersection of the existing Android
  * nonsecure pool and the inferred Windows pool. Do not change SCM thresholds.
  */
 if (ctx->dma < 0x25800000 || ctx->dma + size >
     (windows_reference ? 0xc0000000ULL : 0xe0000000ULL) || !IS_ALIGNED(ctx->dma, SZ_1M))
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
 table->size = ctx->table_bytes;
 table->qhdr0 = ctx->header_bytes;
 table->qhdr_size = sizeof(struct qheader);
 table->num_q = table->active_q = 3;
 pr_info("SQ2_HFI_PROFILE windows_reference=%d ids=%u bytes=%zu header=%u table=%u\n",
         windows_reference, spec->num_ids, ctx->bytes, ctx->header_bytes, ctx->table_bytes);
 if (!compact_table) strscpy(table->name, "sq2_hfi_private", sizeof(table->name));
 for (off = 0; off < 3; off++) {
  struct qheader *q = ctx->cpu + ctx->header_bytes + off * sizeof(*q);
  ctx->q[off] = q;
  ctx->ring[off] = ctx->cpu + ctx->table_bytes + off * QUEUE_BYTES;
  q->status = 1;
  q->start = ctx->dma + ctx->table_bytes + off * QUEUE_BYTES;
  q->type = 0x01010000 | off;
  q->words = QUEUE_WORDS;
  q->rx_wm = q->tx_wm = 1;
  q->rx_req = (off == 2 || (!off && empty_ping)) ? 0 : 1;
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
 kfree(ctx->pkt);
 kfree(ctx);
 return ret;
}

/* Copy at most one bounded packet into ctx->pkt. Ring indices and lengths come
 * from firmware and must be checked before any offset or host consumer-index
 * update. Returns copied words, 0 when empty.
 */
static int receive(struct sq2_hfi_transport *ctx, unsigned int index)
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
 ret = sq2_hfi_packet_copy(ring, QUEUE_WORDS, r, &p, ctx->pkt, PKT_WORDS);
 if (ret < 0) return -EPROTO;
 pr_info("SQ2_HFI_RX q=%u bytes=%u type=%x word2=%x r=%u w=%u\n", index, p.bytes, p.type, p.word2, r, w);
 dma_wmb();
 WRITE_ONCE(q->read, p.next);
 return ret;
}
/* Bounded printable excerpt of a SYS_DEBUG packet; never interpreted. */
static void log_debug_packet(const u32 *pkt, int words)
{
 char text[161];
 const u8 *src = (const u8 *)(pkt + 6);
 size_t i, n = words > 6 ? min_t(size_t, (words - 6) * 4, sizeof(text) - 1) : 0;
 for (i = 0; i < n && src[i]; i++) text[i] = src[i] >= 0x20 && src[i] < 0x7f ? src[i] : '.';
 text[i] = 0;
 pr_info("SQ2_HFI_FW_DEBUG type=%x \"%s\"\n", words > 2 ? pkt[2] : 0, text);
}
/* Poll message and debug queues until a queue-1 packet of the wanted type.
 * Everything else is logged and consumed. A firmware SYS_ERROR event aborts.
 */
static int wait_message(struct sq2_hfi_transport *ctx, u32 type)
{
 unsigned int i, q;
 int words;
 for (i = 0; i < 2000; i++) {
  for (q = 1; q < 3; q++) {
   unsigned int packets = 0;
   while (packets++ < 16 && (words = receive(ctx, q)) != 0) {
    if (words < 0) return words;
    if (ctx->pkt[1] == 0x20004) {
     log_debug_packet(ctx->pkt, words);
     continue;
    }
    if (ctx->pkt[1] == 0x21001) {
     pr_info("SQ2_HFI_EVENT session=%x event=%x data1=%x data2=%x\n",
             words > 2 ? ctx->pkt[2] : 0, words > 3 ? ctx->pkt[3] : 0,
             words > 4 ? ctx->pkt[4] : 0, words > 5 ? ctx->pkt[5] : 0);
     if (words > 3 && ctx->pkt[3] == 0x1) return -EIO;
     continue;
    }
    if (q == 1 && ctx->pkt[1] == type) return words;
   }
  }
  usleep_range(1000, 1500);
 }
 return -ETIMEDOUT;
}
static int send_command(struct sq2_hfi_transport *ctx, const u32 *src, u32 words)
{
 u32 r = READ_ONCE(ctx->q[0]->read), w = READ_ONCE(ctx->q[0]->write), next;
 if (sq2_hfi_packet_write(ctx->ring[0], QUEUE_WORDS, r, w, src, words, &next))
  return -EPROTO;
 dma_wmb();
 WRITE_ONCE(ctx->q[0]->write, next);
 dma_wmb();
 writel(BIT(15), ctx->base + 0xdf018);
 return 0;
}
/* Raw body for offline review plus the upstream-equivalent word-scan summary. */
static int capture_caps(struct sq2_hfi_transport *ctx, int words)
{
 struct sq2_hfi_caps c;
 int i;
 for (i = 0; i < words; i += 8)
  pr_info("SQ2_HFI_CAPS_RAW off=%03d %08x %08x %08x %08x %08x %08x %08x %08x\n", i,
          ctx->pkt[i], i + 1 < words ? ctx->pkt[i + 1] : 0, i + 2 < words ? ctx->pkt[i + 2] : 0,
          i + 3 < words ? ctx->pkt[i + 3] : 0, i + 4 < words ? ctx->pkt[i + 4] : 0,
          i + 5 < words ? ctx->pkt[i + 5] : 0, i + 6 < words ? ctx->pkt[i + 6] : 0,
          i + 7 < words ? ctx->pkt[i + 7] : 0);
 if (sq2_hfi_caps_parse(ctx->pkt, words, &c)) return -EPROTO;
 pr_info("SQ2_HFI_CAPS error=%x properties=%u body_words=%u dec=%08x enc=%08x max_sessions=%u codec_supported=%u codec_mask=%u h264_dec_masks=%u raw_formats=%u capabilities=%u profile_levels=%u alloc_modes=%u truncated=%u WORD_SCAN\n",
         c.error, c.num_properties, c.body_words, c.dec_codecs, c.enc_codecs,
         c.max_sessions, c.codec_supported, c.codec_mask, c.h264_decoder_masks,
         c.raw_formats, c.capabilities, c.profile_levels, c.alloc_modes, c.truncated);
 ctx->dec_codecs = c.dec_codecs;
 return c.error ? -EIO : 0;
}
/* One H264 decoder session: SESSION_INIT then SESSION_END, no buffers. The
 * domain/codec values are the pinned 6.3 VIDC_SESSION_TYPE_DEC and
 * HFI_VIDEO_CODEC_H264; the session id is a fixed private host cookie.
 */
static int h264_session(struct sq2_hfi_transport *ctx)
{
 const u32 init[] = { 20, 0x10007, SESSION_COOKIE, 2, SQ2_HFI_CODEC_H264 };
 const u32 end[] = { 12, 0x10008, SESSION_COOKIE };
 int words, ret;
 if (!(ctx->dec_codecs & SQ2_HFI_CODEC_H264)) {
  pr_info("SQ2_HFI_SESSION skipped dec_codecs=%08x NO_H264\n", ctx->dec_codecs);
  return -EOPNOTSUPP;
 }
 ret = send_command(ctx, init, ARRAY_SIZE(init));
 pr_info("SQ2_HFI_SESSION_INIT_SENT id=%x domain=2 codec=2 ret=%d\n", SESSION_COOKIE, ret);
 if (ret) return ret;
 words = wait_message(ctx, 0x20006);
 if (words < 0) {
  pr_info("SQ2_HFI_SESSION_INIT_RESULT ret=%d\n", words);
  return words;
 }
 if (words < 4 || ctx->pkt[2] != SESSION_COOKIE) return -EPROTO;
 pr_info("SQ2_HFI_SESSION_INIT_DONE id=%x error=%x properties=%u bytes=%u\n",
         ctx->pkt[2], ctx->pkt[3], words > 4 ? ctx->pkt[4] : 0, ctx->pkt[0]);
 if (ctx->pkt[3]) return -EIO;
 ctx->session_open = true;
 ret = send_command(ctx, end, ARRAY_SIZE(end));
 pr_info("SQ2_HFI_SESSION_END_SENT id=%x ret=%d\n", SESSION_COOKIE, ret);
 if (ret) return ret;
 words = wait_message(ctx, 0x20007);
 if (words < 0) {
  pr_info("SQ2_HFI_SESSION_END_RESULT ret=%d\n", words);
  return words;
 }
 if (words < 4 || ctx->pkt[2] != SESSION_COOKIE) return -EPROTO;
 pr_info("SQ2_HFI_SESSION_END_DONE id=%x error=%x\n", ctx->pkt[2], ctx->pkt[3]);
 if (ctx->pkt[3]) return -EIO;
 ctx->session_open = false;
 return 0;
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
static int ping_empty_queue(struct sq2_hfi_transport *ctx)
{
 u32 request;
 unsigned int i;
 /* Host initialized cmd.rx_req to zero and does not modify it afterwards.
  * The pinned firmware's empty-queue path (VA 2f098..2f09c) writes one.
  * Observing this transition is direct shared-queue write evidence. It is
  * stronger than CTRL_STATUS=1, which firmware sets before queue parsing.
  * This diagnostic uses only the documented bit15 doorbell, no MMIO reads.
  */
 pr_info("SQ2_HFI_EMPTY_PING begin host_initial_rx_req=0 NO_PACKET\n");
 usleep_range(20000, 25000);
 writel(BIT(15), ctx->base + 0xdf018);
 for (i = 0; i < 250; i++) {
  dma_rmb();
  request = READ_ONCE(ctx->q[0]->rx_req);
  if (request) {
   u32 read = READ_ONCE(ctx->q[0]->read), write = READ_ONCE(ctx->q[0]->write);
   pr_info("SQ2_HFI_EMPTY_PING rx_req=%u polls=%u cmd_r=%u cmd_w=%u\n", request, i,
           read, write);
   return request == 1 && !read && !write ? 0 : -EPROTO;
  }
  usleep_range(1000, 1500);
 }
 pr_info("SQ2_HFI_EMPTY_PING timeout rx_req=0 NO_OBSERVED_FIRMWARE_QUEUE_WRITE\n");
 return -ETIMEDOUT;
}
int sq2_hfi_run(struct sq2_hfi_transport *ctx, void __iomem *base, bool protect_iova,
                unsigned int trace_delay_ms)
{
 const u32 mailbox_values[] = {
  ctx->dma, ctx->bytes, ctx->dma, 1, ctx->dma + SFR_OFFSET, 0,
  ctx->dma, ctx->dma, ctx->bytes,
 };
 const u32 sys_init[] = { 12, 0x10001, 1 }; /* HFI_VIDEO_ARCH_OX */
 u32 status;
 bool done = false;
 int ret, i, words;
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
 /* The pinned Windows reset/queue functions publish CPU mailboxes, without
  * the downstream DSP mirror. Keep the legacy protocol unchanged by default.
  */
 for (i = 0; i < (ctx->windows_reference ? 6 : ARRAY_SIZE(mailbox_values)); i++) {
  pr_info("SQ2_HFI_MAILBOX_WRITE_BEGIN offset=%x value=%08x\n", mailbox_offsets[i], mailbox_values[i]);
  hfi_trace_barrier(ctx);
  writel(mailbox_values[i], base + mailbox_offsets[i]);
  ctx->mailbox_count++;
  pr_info("SQ2_HFI_MAILBOX_WRITE_END offset=%x\n", mailbox_offsets[i]);
  hfi_trace_barrier(ctx);
 }
 dma_wmb();
 pr_info("SQ2_HFI_BOOT begin iova=%pad uc_bytes=%u table_bytes=%u header=%u IRQ_MASK_UNCHANGED\n",
         &ctx->dma, (u32)ctx->bytes, ctx->table_bytes, ctx->header_bytes);
 hfi_trace_barrier(ctx);
 pr_info("SQ2_HFI_CTRL_INIT_WRITE_BEGIN\n");
 hfi_trace_barrier(ctx);
 ctx->ctrl_started = true;
 writel(1, base + 0xd2048);
 pr_info("SQ2_HFI_CTRL_INIT_WRITE_END\n");
 hfi_trace_barrier(ctx);
 if (!ctx->windows_reference) {
  pr_info("SQ2_HFI_VERSION_WRITE_BEGIN\n");
  hfi_trace_barrier(ctx);
  ctx->version_written = true;
  writel(1, base + 0xd2058);
  pr_info("SQ2_HFI_VERSION_WRITE_END\n");
  hfi_trace_barrier(ctx);
 }
 pr_info("SQ2_HFI_CTRL_STATUS_POLL_BEGIN\n");
 hfi_trace_barrier(ctx);
 ret = readl_poll_timeout(base + 0xd204c, status, status != 0, 1000, 1000000);
 pr_info("SQ2_HFI_BOOT status=%08x ret=%d\n", status, ret);
 queue_status(ctx, "boot_result");
 if (ret) return ret;
 if (status & 0xfe) return -EIO;
 if (ctx->empty_ping) {
  ret = ping_empty_queue(ctx);
  if (ret) return ret;
 }
 ret = send_command(ctx, sys_init, ARRAY_SIZE(sys_init));
 if (ret) return ret;
 pr_info("SQ2_HFI_SYS_INIT_SENT words=3\n");
 words = wait_message(ctx, 0x20001);
 if (words >= 0) {
  done = true;
  if (words < 4 || ctx->pkt[2]) ret = -EIO;
  else pr_info("SQ2_HFI_SYS_INIT_REPLY error=0 properties=%u\n", ctx->pkt[3]);
  if (!ret && ctx->capture_caps) ret = capture_caps(ctx, words);
  if (!ret && ctx->session) ret = h264_session(ctx);
 } else if (words != -ETIMEDOUT) {
  ret = words;
 }
 pr_info("SQ2_HFI_DONE reply=%d ret=%d session_open=%d cmd_r=%u cmd_w=%u msg_r=%u msg_w=%u status=%08x\n",
         done, ret, ctx->session_open,
         READ_ONCE(ctx->q[0]->read), READ_ONCE(ctx->q[0]->write),
         READ_ONCE(ctx->q[1]->read), READ_ONCE(ctx->q[1]->write), readl(base + 0xd204c));
 if (!done && !ret) ret = -ETIMEDOUT;
 return ret;
}
int sq2_hfi_finish(struct sq2_hfi_transport *ctx)
{
 int i, ret = 0;
 if (!ctx) return 0;
 if (ctx->configured) {
  for (i = 0; i < ctx->mailbox_count; i++)
   writel(0, ctx->base + mailbox_offsets[i]);
  if (ctx->ctrl_started) writel(0, ctx->base + 0xd2048);
  if (ctx->version_written) writel(0, ctx->base + 0xd2058);
  writel(1, ctx->base + 0xd201c);
  writel(0x14, ctx->base + 0xe0014); /* Owned CPU/watchdog pending bits. */
 }
 dma_free_coherent(ctx->dev, ctx->bytes, ctx->cpu, ctx->dma);
 if (iommu_iova_to_phys(ctx->domain, ctx->dma)) ret = -EIO;
 pr_info("SQ2_HFI_DMA freed first_iova_unmapped=%d ret=%d\n",
         !iommu_iova_to_phys(ctx->domain, ctx->dma), ret);
 ctx->dev->bus_dma_limit = ctx->saved_limit;
 dma_set_mask(ctx->dev, ctx->saved_mask);
 dma_set_coherent_mask(ctx->dev, ctx->saved_coherent);
 kfree(ctx->pkt);
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
 int ret = sq2_hfi_prepare(dev, &ctx, false, false, false, false);
 return ret ? ret : sq2_hfi_finish(ctx);
}
