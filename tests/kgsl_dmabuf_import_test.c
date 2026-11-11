#define KGSL_MEMDESC_MAP_UNCERTAIN (1U << 10)
/* The functions under test are extracted unchanged from the prepared driver.
 * Mocks model ownership only; they do not emulate IOMMU or DMA coherency. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t u64;
#define PAGE_SIZE 4096
#define GFP_KERNEL 0
#define DMA_BIDIRECTIONAL 0
#define KGSL_MEMFLAGS_SECURE (1ULL << 32)
#define KGSL_MEMFLAGS_USE_CPU_MAP (1ULL << 33)
#define KGSL_MEMFLAGS_IOCOHERENT (1ULL << 34)
#define KGSL_MEMFLAGS_USERMEM_ION (1ULL << 35)
#define KGSL_CACHEMODE_MASK (3ULL << 26)
#define KGSL_CACHEMODE_SHIFT 26
#define KGSL_CACHEMODE_WRITEBACK 3
#define KGSL_CACHEMODE_WRITECOMBINE 0
#define KGSL_MEMDESC_LAZY_ALLOCATION 1
#define KGSL_MEMDESC_USE_SHMEM 2
#define KGSL_MEMDESC_MAPPED 4
#define KGSL_MEMDESC_SECURE 8
#define IS_ALIGNED(n, a) (!((n) & ((a) - 1)))
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define container_of(p, type, member) ((type *)((char *)(p) - offsetof(type, member)))
struct page { int value; };
struct scatterlist { struct page *page; unsigned offset, length; struct scatterlist *next; };
struct sg_table { struct scatterlist *sgl; unsigned nents, orig_nents; };
#define sg_page(s) ((s)->page)
#define for_each_sgtable_sg(t, s, i) \
    for ((i) = 0, (s) = (t)->sgl; (i) < (int)(t)->orig_nents; (i)++, (s) = (s)->next)
struct dma_buf_ops { int begin_cpu_access, end_cpu_access; };
static const struct dma_buf_ops buf_ops = {1, 1};
struct dma_buf { u64 size; int refs; const char *exp_name; const struct dma_buf_ops *ops; };
struct dma_buf_attachment { struct dma_buf *buf; };
struct kgsl_pagetable { int id; void *mmu; };
struct kgsl_device { void *dev; };
struct kgsl_memdesc { u64 flags, size, gpuaddr; unsigned priv; struct sg_table *sgt;
    struct kgsl_pagetable *pagetable; void *ops; };
struct kgsl_mem_entry { struct kgsl_memdesc memdesc; void *priv_data; };
struct kgsl_dma_buf_meta { struct dma_buf *dmabuf; struct dma_buf_attachment *attach;
    struct kgsl_mem_entry *entry; struct sg_table *table; };
struct list_head { int unused; };
static int kgsl_dmabuf_ops;
static int fail_alloc, fail_attach, fail_map;
static int allocated, attached, mapped, listed;
static struct dma_buf_attachment attachment;
static struct sg_table table;
static struct page pages[2];
static struct scatterlist sg[2];
static struct dma_buf buffer;
static struct kgsl_pagetable pagetable;
static struct kgsl_device test_device;
static char events[64];
static unsigned event_count, cases;
static void event(char c) { assert(event_count + 1 < sizeof(events)); events[event_count++] = c; }
static void *kzalloc(size_t size, int flags) {
    event('a'); if (fail_alloc) return NULL;
    void *p = calloc(1, size); assert(p); allocated++; return p;
}
static void kfree(void *p) { assert(p && allocated == 1); event('f'); allocated--; free(p); }
static struct dma_buf_attachment *dma_buf_attach(struct dma_buf *buf, void *dev) {
    event('t'); assert(buf == &buffer && buf->refs == 1 && !attached);
    if (fail_attach) return ERR_PTR(-EBUSY);
    attached++; attachment.buf = buf; return &attachment;
}
static struct sg_table *dma_buf_map_attachment_unlocked(struct dma_buf_attachment *a, int dir) {
    event('m'); assert(a == &attachment && attached == 1 && !mapped);
    if (fail_map == 1) return ERR_PTR(-EIO);
    if (fail_map == 2) return NULL;
    mapped++; return &table;
}
static void dma_buf_unmap_attachment_unlocked(struct dma_buf_attachment *a, struct sg_table *t, int dir) {
    assert(a == &attachment && t == &table && attached == 1 && mapped == 1 && !listed);
    event('u'); mapped--;
}
static void dma_buf_detach(struct dma_buf *b, struct dma_buf_attachment *a) {
    assert(b == &buffer && a == &attachment && attached == 1 && !mapped);
    event('d'); attached--;
}
static void dma_buf_put(struct dma_buf *b) {
    assert(b == &buffer && b->refs == 1 && !attached); event('p'); b->refs--;
}
static void add_dmabuf_list(struct kgsl_dma_buf_meta *meta) {
    assert(mapped == 1 && !listed && meta->entry->priv_data == meta);
    assert(meta->dmabuf == &buffer && meta->table == &table && meta->attach == &attachment);
    event('l'); listed++;
}
static void remove_dmabuf_list(struct kgsl_dma_buf_meta *meta) {
    assert(listed == 1 && mapped == 1); event('r'); listed--;
}
#include "kgsl-functions.h"
#ifdef KGSL_XIAOMI
#define destroy_import(memdesc) kgsl_destroy_ion(memdesc)
#else
#define destroy_import(memdesc) kgsl_destroy_ion(memdesc, NULL)
#endif

#ifndef KGSL_XIAOMI
/* Mapping helpers record exact physical segments and rollback extents. */
#define KGSL_MEM_ENTRY_KERNEL 0
#define KGSL_MMU_DEVICE(mmu) (&test_device)
#define for_each_sg(list, s, count, i) \
    for ((i) = 0, (s) = (list); (i) < (count); (i)++, (s) = (s)->next)
#define dev_err(dev, fmt, ...) ((void)(dev))
static unsigned map_calls, guard_calls, unmap_calls, fail_at;
static size_t partial_bytes, rollback_size;
static u64 rollback_addr, mapped_addr[2];
static int mapping_errno, guard_errno;
static unsigned int kgsl_iommu_get_protection_flags(struct kgsl_pagetable *pt, struct kgsl_memdesc *m) {
    return 3;
}
static int kgsl_memdesc_usermem_type(struct kgsl_memdesc *m) {
    return !!(m->flags & KGSL_MEMFLAGS_USERMEM_ION);
}
static bool kgsl_memdesc_is_global(struct kgsl_memdesc *m) { return false; }
static bool kgsl_memdesc_is_secured(struct kgsl_memdesc *m) { return !!(m->flags & KGSL_MEMFLAGS_SECURE); }
static int _iommu_map_scatterlist(struct kgsl_pagetable *pt, u64 addr,
                                struct scatterlist *s, unsigned flags, size_t *bytes) {
    assert(pt == &pagetable && flags == 3 && map_calls < 2);
    assert(s == &sg[map_calls]); mapped_addr[map_calls] = addr; map_calls++;
    if (map_calls == fail_at) { *bytes = partial_bytes; return mapping_errno; }
    *bytes = s->length; return 0;
}
static int _iommu_unmap(struct kgsl_pagetable *pt, u64 addr, u64 bytes) {
    assert(pt == &pagetable && !unmap_calls); unmap_calls++;
    rollback_addr = addr; rollback_size = bytes; return 0;
}
static int _iommu_map_guard_page(struct kgsl_pagetable *pt, struct kgsl_memdesc *m, u64 addr, unsigned flags) {
    assert(addr == m->gpuaddr + m->size && flags == 3); guard_calls++; return guard_errno;
}
static void sg_free_table(struct sg_table *t) { assert(!"must not free importer-owned SG table"); }
#include "kgsl-map-functions.h"

static void reset_map(void) {
    map_calls = guard_calls = unmap_calls = fail_at = 0;
    partial_bytes = rollback_size = 0; rollback_addr = 0;
    mapping_errno = guard_errno = 0;
    memset(mapped_addr, 0, sizeof(mapped_addr));
}

#endif

static struct kgsl_mem_entry reset(void) {
    assert(!allocated && !attached && !mapped && !listed);
    memset(events, 0, sizeof(events)); event_count = 0;
    fail_alloc = fail_attach = fail_map = 0;
    buffer = (struct dma_buf){ .size = PAGE_SIZE * 2, .refs = 1, .exp_name = "system-uncached", .ops = &buf_ops };
    sg[0] = (struct scatterlist){ .page = &pages[0], .length = PAGE_SIZE, .next = &sg[1] };
    sg[1] = (struct scatterlist){ .page = &pages[1], .length = PAGE_SIZE };
    /* DMA mapping coalesced entries. Physical traversal must use orig_nents. */
    table = (struct sg_table){ .sgl = sg, .nents = 1, .orig_nents = 2 };
    return (struct kgsl_mem_entry){ .memdesc = {
        .flags = KGSL_MEMFLAGS_USE_CPU_MAP | KGSL_MEMFLAGS_IOCOHERENT | 128,
        .size = 123, .priv = 128
#ifndef KGSL_XIAOMI
        | KGSL_MEMDESC_LAZY_ALLOCATION | KGSL_MEMDESC_USE_SHMEM
#endif
    }};
}
static void failure(struct kgsl_mem_entry *entry, int expected, const char *order) {
    struct kgsl_mem_entry before = *entry;
    assert(kgsl_setup_dma_buf(&test_device, &pagetable, entry, &buffer) == expected);
    assert(!memcmp(entry, &before, sizeof(before)));
    assert(!allocated && !attached && !mapped && !listed);
    assert(buffer.refs == 1); /* caller must retain and release its reference */
    assert(!strcmp(events, order));
    buffer.refs--; cases++;
}
int main(void) {
    struct kgsl_mem_entry e;
    e = reset(); e.memdesc.flags |= KGSL_MEMFLAGS_SECURE; failure(&e, -EOPNOTSUPP, "");
    #ifdef KGSL_XIAOMI
    e = reset(); e.memdesc.priv |= KGSL_MEMDESC_SECURE; failure(&e, -EOPNOTSUPP, "");
    #endif
    e = reset(); buffer.size = 0; failure(&e, -EINVAL, "");
    e = reset(); buffer.size--; failure(&e, -EINVAL, "");
    e = reset(); buffer.exp_name = "unknown"; failure(&e, -EOPNOTSUPP, "");
    e = reset(); buffer.exp_name = NULL; failure(&e, -EOPNOTSUPP, "");
    struct dma_buf_ops incomplete_ops = {1, 0};
    e = reset(); buffer.ops = &incomplete_ops; failure(&e, -EOPNOTSUPP, "");
    e = reset(); fail_alloc = 1; failure(&e, -ENOMEM, "a");
    e = reset(); fail_attach = 1; failure(&e, -EBUSY, "atf");
    e = reset(); fail_map = 1; failure(&e, -EIO, "atmdf");
    e = reset(); fail_map = 2; failure(&e, -EINVAL, "atmdf");
    e = reset(); table.sgl = NULL; failure(&e, -EINVAL, "atmudf");
    e = reset(); table.orig_nents = 0; failure(&e, -EINVAL, "atmudf");
    e = reset(); sg[1].page = NULL; failure(&e, -EINVAL, "atmudf");
    e = reset(); sg[1].offset = 1; failure(&e, -EINVAL, "atmudf");
    e = reset(); sg[1].length = 0; failure(&e, -EINVAL, "atmudf");
    e = reset(); sg[1].length--; failure(&e, -EINVAL, "atmudf");
    e = reset(); sg[1].length += PAGE_SIZE; failure(&e, -EINVAL, "atmudf");
    e = reset(); buffer.size += PAGE_SIZE; failure(&e, -EINVAL, "atmudf");
    e = reset(); buffer.size = PAGE_SIZE; failure(&e, -EINVAL, "atmudf");
    for (int i = 0; i < 64; i++) {
        e = reset();
        if (i & 1) buffer.exp_name = "system";
        assert(kgsl_setup_dma_buf(&test_device, &pagetable, &e, &buffer) == 0);
        assert(!strcmp(events, "atml"));
        assert(e.memdesc.size == buffer.size && e.memdesc.sgt == &table);
        assert(e.memdesc.pagetable == &pagetable && e.memdesc.ops == &kgsl_dmabuf_ops);
        assert(e.memdesc.flags == (KGSL_MEMFLAGS_USERMEM_ION | 128 |
            ((i & 1) ? ((u64)KGSL_CACHEMODE_WRITEBACK << KGSL_CACHEMODE_SHIFT) : 0)));
        assert(e.memdesc.priv == 128 && buffer.refs == 1);
        e.memdesc.priv |= KGSL_MEMDESC_MAPPED;
        destroy_import(&e.memdesc);
        assert(!strcmp(events, "atml") && e.priv_data && mapped && buffer.refs == 1);
        e.memdesc.priv &= ~KGSL_MEMDESC_MAPPED;
#ifdef KGSL_XIAOMI
        e.memdesc.priv |= KGSL_MEMDESC_MAP_UNCERTAIN;
        destroy_import(&e.memdesc);
        assert(!strcmp(events, "atml") && e.priv_data && mapped && buffer.refs == 1);
        e.memdesc.priv &= ~KGSL_MEMDESC_MAP_UNCERTAIN;
#endif
        destroy_import(&e.memdesc);
        assert(!strcmp(events, "atmlrudpf"));
        assert(!e.priv_data && !e.memdesc.sgt && !allocated && !mapped && !attached && !listed);
        assert(buffer.refs == 0);
        destroy_import(&e.memdesc);
        assert(!strcmp(events, "atmlrudpf")); cases++;
    }
    #ifndef KGSL_XIAOMI
    /* Coalesced DMA table has 1 DMA entry but 2 physical entries. */
    for (int scenario = 0; scenario < 7; scenario++) {
        e = reset(); reset_map();
        /* Two-page entries allow a failure halfway through either segment. */
        sg[0].length = sg[1].length = PAGE_SIZE * 2;
        buffer.size = PAGE_SIZE * 4;
        assert(kgsl_setup_dma_buf(&test_device, &pagetable, &e, &buffer) == 0);
        e.memdesc.gpuaddr = 0x10000000;
        if (scenario == 1 || scenario == 2) {
            fail_at = (unsigned)scenario; partial_bytes = PAGE_SIZE; mapping_errno = -ENOMEM;
        } else if (scenario == 3) {
            fail_at = 2; partial_bytes = 0; mapping_errno = -EIO;
        } else if (scenario == 4) {
            fail_at = 2; partial_bytes = PAGE_SIZE; /* short mapping reported as success */
        } else if (scenario == 5) {
            fail_at = 2; /* zero bytes reported as success */
        } else if (scenario == 6) {
            guard_errno = -ENOSPC;
        }
        int ret = kgsl_iommu_map(&pagetable, &e.memdesc);
        if (scenario == 0) {
            assert(ret == 0 && map_calls == 2 && guard_calls == 1 && !unmap_calls);
            assert(mapped_addr[0] == e.memdesc.gpuaddr);
            assert(mapped_addr[1] == e.memdesc.gpuaddr + PAGE_SIZE * 2);
        } else {
            assert(ret < 0 && unmap_calls == 1 && rollback_addr == e.memdesc.gpuaddr);
            size_t expected = scenario == 1 ? PAGE_SIZE :
                (scenario == 2 || scenario == 4) ? PAGE_SIZE * 3 :
                scenario == 6 ? PAGE_SIZE * 4 : PAGE_SIZE * 2;
            assert(rollback_size == expected);
            assert(guard_calls == (scenario == 6 ? 1U : 0U));
        }
        assert(e.memdesc.sgt == &table && mapped == 1 && buffer.refs == 1);
        destroy_import(&e.memdesc);
        assert(!allocated && !attached && !mapped && !listed && !buffer.refs); cases++;
    }
    printf("PASS: %u cases (19 import failures, 64 lifetime cycles, 7 SG mapping/rollback cases)\n", cases);
    #else
    printf("PASS: %u cases (20 import failures, 64 lifetime cycles)\n", cases);
    #endif
    return 0;
}
