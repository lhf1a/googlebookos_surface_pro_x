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

#ifdef SQ2_HFI_DECODE
#include "sq2-hfi-dec.h"
struct dec_state;
static void dec_handle(struct dec_state *d, const u32 *pkt, int words);
#endif

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
#ifdef SQ2_HFI_DECODE
 bool decode, decoding;
 struct dec_state *dec;
#endif
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
                    bool compact_table, bool empty_ping, bool capture_caps, bool session,
                    bool decode)
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
#ifdef SQ2_HFI_DECODE
 ctx->decode = decode;
#else
 if (decode) { kfree(ctx->pkt); kfree(ctx); return -EOPNOTSUPP; }
#endif
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
static int wait_message_for(struct sq2_hfi_transport *ctx, u32 type, unsigned int iters)
{
 unsigned int i, q;
 int words;
 for (i = 0; i < iters; i++) {
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
#ifdef SQ2_HFI_DECODE
     if (ctx->decoding && words > 3 && ctx->pkt[3] == 0x2) return -EIO;
     if (ctx->decoding) dec_handle(ctx->dec, ctx->pkt, words);
#endif
     continue;
    }
#ifdef SQ2_HFI_DECODE
    if (ctx->decoding && q == 1) dec_handle(ctx->dec, ctx->pkt, words);
#endif
    if (q == 1 && ctx->pkt[1] == type) return words;
   }
  }
  usleep_range(1000, 1500);
 }
 return -ETIMEDOUT;
}
static int wait_message(struct sq2_hfi_transport *ctx, u32 type)
{
 return wait_message_for(ctx, type, 2000);
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

#ifdef SQ2_HFI_DECODE
/* Private one-shot decode of the pinned 320x240 H264 vector, following the
 * pinned 6.3 Venus HFI 4xx decoder sequence (vdec_output_conf, intbufs,
 * load/start, ETB, sequence event + continue, DPB/OPB FTB, stop/release).
 * Linear NV12 is taken from OUTPUT2; OUTPUT carries firmware-only UBWC DPB.
 * Every decode buffer is coherent DMA in this device's IOMMU domain and is
 * freed only by sq2_hfi_finish, i.e. after a confirmed PAS stop.
 */
#include "sq2-h264-vector.h"
#define DEC_MAX_BUFS 96
#define DEC_MAX_BYTES (192 * SZ_1M)
#define BT_INPUT 1
#define BT_OUTPUT 2
#define BT_OUTPUT2 3
struct dec_buf {
 void *cpu;
 dma_addr_t dma;
 u32 size, type, tag;
};
struct dec_state {
 struct sq2_hfi_transport *ctx;
 struct dec_buf buf[DEC_MAX_BUFS];
 unsigned int nbuf;
 size_t total;
 struct sq2_bufreq req[11];
 int nreq;
 u32 seq_event, seq_w, seq_h;
 unsigned int ebd, ebd_err, fbd_dpb, fbd_out, fbd_empty, fbd_err;
 unsigned int verified, mismatched, eos_seen;
 u32 frame_ok;
 u32 y_stride, y_scan;
 bool requeue;
};
static struct dec_buf *dec_alloc(struct dec_state *d, u32 size, u32 type, u32 tag)
{
 struct sq2_hfi_transport *ctx = d->ctx;
 struct dec_buf *b;
 if (!size || size > DEC_MAX_BYTES - PAGE_SIZE) return NULL;
 size = PAGE_ALIGN(size);
 if (d->nbuf >= DEC_MAX_BUFS || !size || d->total + size > DEC_MAX_BYTES) return NULL;
 b = &d->buf[d->nbuf];
 b->cpu = dma_alloc_coherent(ctx->dev, size, &b->dma, GFP_KERNEL | __GFP_NOWARN);
 if (!b->cpu) return NULL;
 b->size = size; b->type = type; b->tag = tag;
 d->nbuf++;
 d->total += size;
 /* Same intersection as the queue region; the firmware address is 32-bit. */
 if (b->dma < 0x25800000 || b->dma + size > 0xc0000000ULL) {
  pr_err("SQ2_HFI_DEC buffer outside IOVA window %pad\n", &b->dma);
  return NULL;
 }
 memset(b->cpu, 0, size);
 return b;
}
static void dec_free(struct dec_state *d)
{
 while (d->nbuf) {
  struct dec_buf *b = &d->buf[--d->nbuf];
  dma_free_coherent(d->ctx->dev, b->size, b->cpu, b->dma);
 }
}
static struct dec_buf *dec_find(struct dec_state *d, u32 dma)
{
 unsigned int i;
 for (i = 0; i < d->nbuf; i++)
  if (d->buf[i].dma == dma) return &d->buf[i];
 return NULL;
}
static int dec_ftb(struct dec_state *d, const struct dec_buf *b)
{
 const u32 ftb[] = { 44, 0x211005, SESSION_COOKIE, b->type == BT_OUTPUT2 ? 1 : 0,
                     0, b->size, 0, b->tag, b->dma, 0, 0 };
 return send_command(d->ctx, ftb, ARRAY_SIZE(ftb));
}
static void dec_verify(struct dec_state *d, const u32 *p, struct dec_buf *b)
{
 u32 ts = p[7], filled = p[13], offset = p[14], crc_y, crc_uv;
 u32 frame = ts / 33333;
 const u8 *y;
 if (!filled) { d->fbd_empty++; return; }
 dma_rmb();
 if (frame >= SQ2_VEC_FRAMES || ts % 33333 ||
     sq2_plane_crc(b->cpu, b->size, offset, d->y_stride, SQ2_VEC_W, SQ2_VEC_H, &crc_y) ||
     sq2_plane_crc(b->cpu, b->size, (size_t)offset + d->y_stride * d->y_scan, d->y_stride,
                   SQ2_VEC_W, SQ2_VEC_H / 2, &crc_uv)) {
  pr_info("SQ2_HFI_DEC_FRAME unverifiable ts=%u filled=%u offset=%u\n", ts, filled, offset);
  d->mismatched++;
  return;
 }
 y = (const u8 *)b->cpu + offset;
 pr_info("SQ2_HFI_DEC_FRAME frame=%u crc_y=%08x/%08x crc_uv=%08x/%08x y0=%*phN w=%u h=%u pic=%x\n",
         frame, crc_y, sq2_vec_crc_y[frame], crc_uv, sq2_vec_crc_uv[frame], 16, y,
         p[15], p[16], p[22]);
 if (crc_y == sq2_vec_crc_y[frame] && crc_uv == sq2_vec_crc_uv[frame] &&
     !(d->frame_ok & BIT(frame))) {
  d->frame_ok |= BIT(frame);
  d->verified++;
 } else if (crc_y != sq2_vec_crc_y[frame] || crc_uv != sq2_vec_crc_uv[frame]) {
  d->mismatched++;
 }
}
static void dec_handle(struct dec_state *d, const u32 *p, int words)
{
 struct dec_buf *b;
 if (words >= 3 && p[1] >= 0x220000 && p[1] < 0x230000 && p[2] != SESSION_COOKIE) {
  pr_info("SQ2_HFI_DEC foreign session=%x type=%x\n", p[2], p[1]);
  return;
 }
 switch (p[1]) {
 case 0x221009:
  d->nreq = sq2_parse_bufreq(p, words, d->req, ARRAY_SIZE(d->req));
  break;
 case 0x221007:
  if (words < 9) break;
  d->ebd++;
  if (p[3]) d->ebd_err++;
  pr_info("SQ2_HFI_DEC_EBD tag=%u error=%x filled=%u addr=%08x\n", p[6], p[3], p[5], p[7]);
  break;
 case 0x221008:
  if (words < 25) break;
  b = dec_find(d, p[23]);
  pr_info("SQ2_HFI_DEC_FBD stream=%u error=%x flags=%x filled=%u offset=%u %ux%u ts=%u tag=%u addr=%08x known=%d\n",
          p[3], p[5], p[8], p[13], p[14], p[15], p[16], p[7], p[21], p[23], !!b);
  if (p[5]) d->fbd_err++;
  if (p[8] & 0x1) d->eos_seen++;
  if (!b) break;
  if (b->type == BT_OUTPUT2) {
   d->fbd_out++;
   if (!p[5]) dec_verify(d, p, b);
  } else {
   d->fbd_dpb++;
  }
  if (d->requeue && !(p[8] & 0x1) && dec_ftb(d, b))
   pr_info("SQ2_HFI_DEC requeue failed tag=%u\n", b->tag);
  break;
 case 0x21001:
  if (words >= 6 && p[3] == 0x1000003) {
   unsigned int i, n = p[5];
   const u32 *e = p + 6;
   d->seq_event = p[4];
   for (i = 0; i < n && e + 4 <= p + words; i++) {
    if (e[0] == 0x1001) { d->seq_w = e[2]; d->seq_h = e[3]; e += 4; }
    else if (e[0] == 0x202001 && e + 9 <= p + words) e += 9;
    else break;
   }
   pr_info("SQ2_HFI_DEC_SEQ_CHANGED data1=%x props=%u size=%ux%u\n", p[4], n, d->seq_w, d->seq_h);
  }
  break;
 default:
  break;
 }
}
static int dec_cmd(struct sq2_hfi_transport *ctx, u32 cmd, u32 done, const char *name)
{
 const u32 pkt[] = { 12, cmd, SESSION_COOKIE };
 int words, ret = send_command(ctx, pkt, ARRAY_SIZE(pkt));
 if (ret || !done) {
  pr_info("SQ2_HFI_DEC_%s sent ret=%d\n", name, ret);
  return ret;
 }
 words = wait_message(ctx, done);
 pr_info("SQ2_HFI_DEC_%s words=%d error=%x\n", name, words, words >= 4 ? ctx->pkt[3] : ~0U);
 if (words < 0) return words;
 return words >= 4 && !ctx->pkt[3] ? 0 : -EIO;
}
static int dec_prop(struct sq2_hfi_transport *ctx, u32 ptype, const u32 *v, u32 n)
{
 u32 pkt[12] = { 20 + 4 * n, 0x11001, SESSION_COOKIE, 1, ptype };
 if (n > 7) return -EINVAL;
 memcpy(pkt + 5, v, n * 4);
 return send_command(ctx, pkt, 5 + n);
}
static int dec_get_bufreq(struct sq2_hfi_transport *ctx, const char *stage)
{
 const u32 get[] = { 20, 0x211009, SESSION_COOKIE, 1, 0x202001 };
 struct dec_state *d = ctx->dec;
 int i, words, ret = send_command(ctx, get, ARRAY_SIZE(get));
 if (ret) return ret;
 d->nreq = -1;
 words = wait_message(ctx, 0x221009);
 if (words < 0) return words;
 if (d->nreq <= 0) return -EPROTO;
 for (i = 0; i < d->nreq; i++)
  pr_info("SQ2_HFI_DEC_BUFREQ stage=%s type=%u size=%u region=%u hold=%u min=%u actual=%u contig=%u align=%u\n",
          stage, d->req[i].type, d->req[i].size, d->req[i].region_size, d->req[i].hold_count,
          d->req[i].count_min, d->req[i].count_actual, d->req[i].contiguous, d->req[i].alignment);
 return 0;
}
static const struct sq2_bufreq *dec_req(struct dec_state *d, u32 type)
{
 int i;
 for (i = 0; i < d->nreq; i++)
  if (d->req[i].type == type) return &d->req[i];
 return NULL;
}
/* HFI 4xx stores the firmware minimum in hold_count (HFI_BUFREQ_COUNT_MIN). */
static u32 dec_count(const struct sq2_bufreq *r, u32 floor)
{
 u32 n = r ? max3(r->hold_count, r->count_actual, floor) : floor;
 return min_t(u32, n, 24);
}
static int h264_decode(struct sq2_hfi_transport *ctx)
{
 const u32 init[] = { 20, 0x10007, SESSION_COOKIE, 2, SQ2_HFI_CODEC_H264 };
 const u32 end[] = { 12, 0x10008, SESSION_COOKIE };
 static const u32 internal[] = { 6, 7, 8, 4, 5 };
 struct dec_state *d;
 const struct sq2_bufreq *r;
 u32 opb_size, dpb_size, in_size, n_in, n_dpb, n_opb, v[4];
 struct dec_buf *in[SQ2_VEC_FRAMES + 1];
 unsigned int i, j, waits;
 int words, ret, teardown;

 if (!(ctx->dec_codecs & SQ2_HFI_CODEC_H264)) return -EOPNOTSUPP;
 d = kzalloc(sizeof(*d), GFP_KERNEL);
 if (!d) return -ENOMEM;
 d->ctx = ctx;
 ctx->dec = d;
 ctx->decoding = true;
 pr_info("SQ2_HFI_DEC vector=%s %ux%u frames=%u bytes=%zu\n", SQ2_VEC_SHA256,
         SQ2_VEC_W, SQ2_VEC_H, SQ2_VEC_FRAMES, sizeof(sq2_vec_stream));
 ret = send_command(ctx, init, ARRAY_SIZE(init));
 if (ret) return ret;
 words = wait_message(ctx, 0x20006);
 pr_info("SQ2_HFI_DEC_SESSION_INIT words=%d error=%x\n", words, words >= 4 ? ctx->pkt[3] : ~0U);
 if (words < 4 || ctx->pkt[2] != SESSION_COOKIE || ctx->pkt[3]) return words < 0 ? words : -EIO;
 ctx->session_open = true;

 /* vdec_session_init / vdec_output_conf order, with the real vector size. */
 opb_size = sq2_nv12_size(SQ2_VEC_W, SQ2_VEC_H, &d->y_stride, &d->y_scan);
 dpb_size = sq2_nv12_ubwc_size(SQ2_VEC_W, SQ2_VEC_H);
 v[0] = BT_INPUT; v[1] = SQ2_VEC_W; v[2] = SQ2_VEC_H;
 ret = dec_prop(ctx, 0x1001, v, 3);
 v[0] = 1; /* VIDC_CORE_ID_1 */
 ret = ret ?: dec_prop(ctx, 0x2002, v, 1);
 v[0] = 1; /* VIDC_WORK_MODE_1: <= 720p progressive */
 ret = ret ?: dec_prop(ctx, 0x1015, v, 1);
 v[0] = BT_OUTPUT2; v[1] = 0x2; /* NV12 */
 ret = ret ?: dec_prop(ctx, 0x1003, v, 2);
 v[0] = BT_OUTPUT; v[1] = 0;
 ret = ret ?: dec_prop(ctx, 0x1003001, v, 2);
 v[0] = BT_OUTPUT2; v[1] = 1;
 ret = ret ?: dec_prop(ctx, 0x1003001, v, 2);
 v[0] = BT_OUTPUT; v[1] = 0x8002; /* NV12_UBWC */
 ret = ret ?: dec_prop(ctx, 0x1003, v, 2);
 v[0] = BT_OUTPUT2; v[1] = SQ2_VEC_W; v[2] = SQ2_VEC_H;
 ret = ret ?: dec_prop(ctx, 0x1001, v, 3);
 pr_info("SQ2_HFI_DEC_PROPS ret=%d opb=%u dpb=%u stride=%u scan=%u\n", ret, opb_size, dpb_size,
         d->y_stride, d->y_scan);
 if (ret) goto out;
 ret = dec_get_bufreq(ctx, "configured");
 if (ret) goto out;
 r = dec_req(d, BT_OUTPUT2);
 if (r && r->size > opb_size) opb_size = r->size;
 r = dec_req(d, BT_OUTPUT);
 if (r && r->size > dpb_size) dpb_size = r->size;
 v[0] = BT_OUTPUT2; v[1] = opb_size;
 ret = dec_prop(ctx, 0x20100c, v, 2);
 v[0] = BT_OUTPUT; v[1] = dpb_size;
 ret = ret ?: dec_prop(ctx, 0x20100c, v, 2);
 n_in = dec_count(dec_req(d, BT_INPUT), SQ2_VEC_FRAMES + 1);
 n_dpb = dec_count(dec_req(d, BT_OUTPUT), 1);
 n_opb = dec_count(dec_req(d, BT_OUTPUT2), SQ2_VEC_FRAMES + 2);
 v[0] = BT_INPUT; v[1] = v[2] = n_in;
 ret = ret ?: dec_prop(ctx, 0x201001, v, 3);
 v[0] = BT_OUTPUT; v[1] = v[2] = n_dpb;
 ret = ret ?: dec_prop(ctx, 0x201001, v, 3);
 v[0] = BT_OUTPUT2; v[1] = v[2] = n_opb;
 ret = ret ?: dec_prop(ctx, 0x201001, v, 3);
 pr_info("SQ2_HFI_DEC_COUNTS ret=%d input=%u dpb=%u opb=%u opb_size=%u dpb_size=%u\n",
         ret, n_in, n_dpb, n_opb, opb_size, dpb_size);
 if (ret) goto out;
 ret = dec_get_bufreq(ctx, "counted");
 if (ret) goto out;

 /* Internal scratch/persist buffers (venus_helper_intbufs_alloc, 4xx ids). */
 for (i = 0; i < ARRAY_SIZE(internal); i++) {
  r = dec_req(d, internal[i]);
  if (!r || !r->size) continue;
  for (j = 0; j < min_t(u32, r->count_actual, 8); j++) {
   struct dec_buf *b = dec_alloc(d, r->size, internal[i], 0);
   u32 set[] = { 36, 0x11002, SESSION_COOKIE, internal[i], 0, 0, 0, 1, 0 };
   if (!b) { ret = -ENOMEM; goto out; }
   set[4] = set[6] = b->size; set[8] = b->dma;
   ret = send_command(ctx, set, ARRAY_SIZE(set));
   pr_info("SQ2_HFI_DEC_SET_BUFFER type=%u size=%u addr=%pad ret=%d\n", internal[i], b->size, &b->dma, ret);
   if (ret) goto out;
  }
 }
 r = dec_req(d, BT_INPUT);
 in_size = max_t(u32, r ? r->size : 0, SZ_64K);
 for (i = 0; i <= SQ2_VEC_FRAMES; i++) {
  in[i] = dec_alloc(d, in_size, BT_INPUT, i);
  if (!in[i]) { ret = -ENOMEM; goto out; }
 }
 for (i = 0; i < n_dpb; i++)
  if (!dec_alloc(d, dpb_size, BT_OUTPUT, 100 + i)) { ret = -ENOMEM; goto out; }
 for (i = 0; i < n_opb; i++)
  if (!dec_alloc(d, opb_size, BT_OUTPUT2, 200 + i)) { ret = -ENOMEM; goto out; }
 pr_info("SQ2_HFI_DEC_ALLOC buffers=%u bytes=%zu input_size=%u\n", d->nbuf, d->total, in_size);

 ret = dec_cmd(ctx, 0x211001, 0x221001, "LOAD_RESOURCES");
 if (ret) goto out;
 ret = dec_cmd(ctx, 0x211002, 0x221002, "START");
 if (ret) goto out;
 /* One access unit per input buffer, then an empty EOS buffer. */
 for (i = 0; i <= SQ2_VEC_FRAMES; i++) {
  u32 len = i < SQ2_VEC_FRAMES ? sq2_vec_au[i + 1] - sq2_vec_au[i] : 0;
  u32 etb[] = { 60, 0x211004, SESSION_COOKIE, 0, i * 33333, len ? 0 : 0x1, 0, 0, 0,
                in[i]->size, len, i, in[i]->dma, 0, 0 };
  if (len) memcpy(in[i]->cpu, sq2_vec_stream + sq2_vec_au[i], len);
  dma_wmb();
  ret = send_command(ctx, etb, ARRAY_SIZE(etb));
  if (ret) goto out;
 }
 pr_info("SQ2_HFI_DEC_ETB_SENT buffers=%u\n", SQ2_VEC_FRAMES + 1);
 /* vdec_start_capture: firmware reports the parsed sequence, host continues
  * and then provides DPB (OUTPUT) and linear (OUTPUT2) buffers.
  */
 for (waits = 0; waits < 100 && !d->seq_event; waits++) {
  words = wait_message_for(ctx, ~0U, 10);
  if (words != -ETIMEDOUT) { ret = words < 0 ? words : 0; if (ret) goto out; }
 }
 pr_info("SQ2_HFI_DEC_SEQ wait=%u event=%x ebd=%u\n", waits, d->seq_event, d->ebd);
 if (d->seq_event) {
  ret = dec_cmd(ctx, 0x21100d, 0, "CONTINUE");
  if (ret) goto out;
 }
 d->requeue = true;
 for (i = 0; i < d->nbuf; i++) {
  if (d->buf[i].type != BT_OUTPUT && d->buf[i].type != BT_OUTPUT2) continue;
  ret = dec_ftb(d, &d->buf[i]);
  if (ret) goto out;
 }
 pr_info("SQ2_HFI_DEC_FTB_SENT dpb=%u opb=%u\n", n_dpb, n_opb);
 for (waits = 0; waits < 300 && !d->eos_seen &&
      hweight32(d->frame_ok) < SQ2_VEC_FRAMES; waits++) {
  words = wait_message_for(ctx, ~0U, 10);
  if (words != -ETIMEDOUT) { ret = words < 0 ? words : 0; if (ret) goto out; }
 }
 d->requeue = false;
 ret = hweight32(d->frame_ok) == SQ2_VEC_FRAMES && !d->mismatched ? 0 : -EBADMSG;
out:
 d->requeue = false;
 pr_info("SQ2_HFI_DEC_RESULT ret=%d verified=%u/%u mismatched=%u ebd=%u ebd_err=%u fbd_out=%u fbd_dpb=%u fbd_empty=%u fbd_err=%u eos=%u seq=%x %ux%u\n",
         ret, hweight32(d->frame_ok), SQ2_VEC_FRAMES, d->mismatched, d->ebd, d->ebd_err,
         d->fbd_out, d->fbd_dpb, d->fbd_empty, d->fbd_err, d->eos_seen, d->seq_event,
         d->seq_w, d->seq_h);
 /* Best-effort orderly teardown; PAS shutdown follows regardless. */
 teardown = dec_cmd(ctx, 0x211003, 0x221003, "STOP");
 /* An unresponsive firmware is stopped by PAS shutdown; do not spend the
  * bounded collector window on further per-buffer timeouts.
  */
 if (teardown == -ETIMEDOUT || teardown == -EPROTO) goto skip_release;
 teardown |= dec_cmd(ctx, 0x21100c, 0x22100a, "RELEASE_RESOURCES");
 for (i = 0; i < d->nbuf; i++) {
  const struct dec_buf *b = &d->buf[i];
  u32 rel[] = { 36, 0x21100b, SESSION_COOKIE, b->type, b->size, 0, 1, 1, b->dma };
  if (b->type < 4 || b->type > 8) continue;
  if (send_command(ctx, rel, ARRAY_SIZE(rel)) || wait_message(ctx, 0x22100c) < 0) {
   teardown = -EIO;
   goto skip_release;
  }
 }
 if (!send_command(ctx, end, ARRAY_SIZE(end)) && wait_message(ctx, 0x20007) >= 4 &&
     !ctx->pkt[3])
  ctx->session_open = false;
skip_release:
 pr_info("SQ2_HFI_DEC_TEARDOWN ret=%d session_open=%d\n", teardown, ctx->session_open);
 ctx->decoding = false;
 return ret;
}
#endif

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
#ifdef SQ2_HFI_DECODE
  if (!ret && ctx->session && ctx->decode) ret = h264_decode(ctx);
  else
#endif
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
#ifdef SQ2_HFI_DECODE
 /* Only reached after a confirmed PAS stop (or before firmware ran). */
 if (ctx->dec) {
  dec_free(ctx->dec);
  kfree(ctx->dec);
 }
#endif
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
 int ret = sq2_hfi_prepare(dev, &ctx, false, false, false, false, false);
 return ret ? ret : sq2_hfi_finish(ctx);
}
