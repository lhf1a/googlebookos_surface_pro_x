/* Actual KGSL cache dispatch and bulk ioctl, with exporter failure injection.
 * This validates ordering/ownership, not physical cache or fence execution. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t u64;
#define CONFIG_DMA_SHARED_BUFFER 1
#define PAGE_SIZE 4096
#define GFP_KERNEL 0
#define KGSL_MEMFLAGS_SECURE (1ULL << 32)
#define KGSL_CACHEMODE_SHIFT 26
#define KGSL_CACHEMODE_MASK (3ULL << KGSL_CACHEMODE_SHIFT)
#define KGSL_CACHEMODE_WRITETHROUGH 2
#define KGSL_CACHEMODE_WRITEBACK 3
#define KGSL_GPUMEM_CACHE_CLEAN 1U
#define KGSL_GPUMEM_CACHE_INV 2U
#define KGSL_GPUMEM_CACHE_FLUSH 3U
#define KGSL_GPUMEM_CACHE_RANGE (1U << 31)
#define KGSL_CACHE_OP_CLEAN 0
#define KGSL_CACHE_OP_INV 1
#define KGSL_CACHE_OP_FLUSH 2
#define KGSL_MEM_ENTRY_ION 1
#define DMA_TO_DEVICE 1
#define DMA_FROM_DEVICE 2
struct dma_buf { unsigned id; };
struct kgsl_dma_buf_meta { struct dma_buf *dmabuf; };
struct kgsl_memdesc { u64 size, flags; unsigned type; };
struct kgsl_mem_entry { struct kgsl_memdesc memdesc; void *priv_data; int refs; };
struct kgsl_process_private { int unused; };
struct kgsl_device_private { struct kgsl_process_private *process_priv; };
struct kgsl_gpumem_sync_cache_bulk { unsigned *id_list, count, op; };
static struct kgsl_mem_entry entries[3];
static struct dma_buf buffers[3];
static struct kgsl_dma_buf_meta metadata[3];
static unsigned cases, calls, native_calls, allocated, alloc_calls, fail_alloc, fail_copy;
static int end_error, begin_error;
static char order[32];
static void record(char c) { assert(calls + 1 < sizeof(order)); order[calls++] = c; }
static int dma_buf_end_cpu_access(struct dma_buf *buf, int direction) {
    assert(direction == DMA_TO_DEVICE && buf->id < 3); record('E'); return end_error;
}
static int dma_buf_begin_cpu_access(struct dma_buf *buf, int direction) {
    assert(direction == DMA_FROM_DEVICE && buf->id < 3); record('B'); return begin_error;
}
static unsigned kgsl_memdesc_usermem_type(struct kgsl_memdesc *m) { return m->type; }
static unsigned kgsl_memdesc_get_cachemode(struct kgsl_memdesc *m) {
    return (m->flags & KGSL_CACHEMODE_MASK) >> KGSL_CACHEMODE_SHIFT;
}
static bool kgsl_cachemode_is_cached(u64 flags) {
    return (flags & KGSL_CACHEMODE_MASK) >= ((u64)KGSL_CACHEMODE_WRITETHROUGH << KGSL_CACHEMODE_SHIFT);
}
static int kgsl_cache_range_op(struct kgsl_memdesc *m, u64 offset, u64 length, unsigned op) {
    assert(m == &entries[2].memdesc && !offset && length == PAGE_SIZE && op == KGSL_CACHE_OP_CLEAN);
    native_calls++; return 0;
}
#define trace_kgsl_mem_sync_cache(...) ((void)0)
#define trace_kgsl_mem_sync_full_cache(...) ((void)0)
/* Exact ARM64 branch in the donor; whole CPU-cache flush is unavailable. */
static bool check_full_flush(size_t size, int op) { return false; }
static void *kcalloc(size_t count, size_t size, int flags) {
    alloc_calls++; if (alloc_calls == fail_alloc) return NULL;
    void *p = calloc(count, size); assert(p); allocated++; return p;
}
static void kfree(void *p) { if (p) { assert(allocated); allocated--; free(p); } }
static int copy_from_user(void *dst, const void *src, size_t len) {
    if (fail_copy) return 1;
    memcpy(dst, src, len); return 0;
}
static void sort(void *base, size_t count, size_t size, int (*cmp)(const void *, const void *), void *swap) {
    qsort(base, count, size, cmp);
}
static struct kgsl_mem_entry *kgsl_sharedmem_find_id(struct kgsl_process_private *p, unsigned id) {
    if (!id || id > 3) return NULL;
    entries[id - 1].refs++; return &entries[id - 1];
}
static void kgsl_mem_entry_put(struct kgsl_mem_entry *e) { assert(e && e->refs == 2); e->refs--; }
#include "kgsl-cache-functions.h"

static void reset(void) {
    assert(!allocated); calls = native_calls = alloc_calls = fail_alloc = fail_copy = 0;
    end_error = begin_error = 0; memset(order, 0, sizeof(order));
    for (unsigned i = 0; i < 3; i++) {
        buffers[i].id = i; metadata[i].dmabuf = &buffers[i];
        entries[i] = (struct kgsl_mem_entry){
            .memdesc = {.size = PAGE_SIZE, .type = i == 2 ? 0 : KGSL_MEM_ENTRY_ION,
                .flags = i ? ((u64)KGSL_CACHEMODE_WRITEBACK << KGSL_CACHEMODE_SHIFT) : 0},
            .priv_data = &metadata[i], .refs = 1};
    }
}
static void single(unsigned id, u64 offset, u64 size, unsigned op, int expected, const char *sequence) {
    assert(_kgsl_gpumem_sync_cache(&entries[id], offset, size, op) == expected);
    assert(!strcmp(order, sequence));
    assert(entries[id].priv_data == &metadata[id] && entries[id].refs == 1); cases++;
}
static void bulk(unsigned op, int expected, const char *sequence, unsigned native) {
    unsigned ids[] = {3, 1, 2, 1, 0, 99};
    struct kgsl_gpumem_sync_cache_bulk request = {ids, 6, op};
    struct kgsl_device_private dev = {0};
    assert(kgsl_ioctl_gpumem_sync_cache_bulk(&dev, 0, &request) == expected);
    assert(!strcmp(order, sequence) && native_calls == native && !allocated);
    for (unsigned i = 0; i < 3; i++) assert(entries[i].refs == 1);
    cases++;
}
int main(void) {
    for (unsigned id = 0; id < 2; id++) {
        reset(); single(id, 0, PAGE_SIZE, KGSL_GPUMEM_CACHE_CLEAN, 0, "E");
        reset(); single(id, 0, PAGE_SIZE, KGSL_GPUMEM_CACHE_INV, 0, "B");
        reset(); single(id, 0, PAGE_SIZE, KGSL_GPUMEM_CACHE_FLUSH, 0, "EB");
        reset(); single(id, UINT64_MAX, UINT64_MAX, KGSL_GPUMEM_CACHE_FLUSH, 0, "EB");
        reset(); single(id, 0, PAGE_SIZE, KGSL_GPUMEM_CACHE_FLUSH | KGSL_GPUMEM_CACHE_RANGE, 0, "EB");
        reset(); end_error = -EIO; single(id, 0, PAGE_SIZE, KGSL_GPUMEM_CACHE_CLEAN, -EIO, "E");
        reset(); end_error = -EIO; single(id, 0, PAGE_SIZE, KGSL_GPUMEM_CACHE_FLUSH, -EIO, "E");
        reset(); begin_error = -EINTR; single(id, 0, PAGE_SIZE, KGSL_GPUMEM_CACHE_INV, -EINTR, "B");
        reset(); begin_error = -EINTR; single(id, 0, PAGE_SIZE, KGSL_GPUMEM_CACHE_FLUSH, -EINTR, "EB");
        reset(); single(id, 0, PAGE_SIZE, 0, -EINVAL, "");
        reset(); single(id, 0, PAGE_SIZE - 1, KGSL_GPUMEM_CACHE_INV | KGSL_GPUMEM_CACHE_RANGE, -EOPNOTSUPP, "");
        reset(); single(id, 1, PAGE_SIZE - 1, KGSL_GPUMEM_CACHE_CLEAN | KGSL_GPUMEM_CACHE_RANGE, -EOPNOTSUPP, "");
        reset(); single(id, 0, 0, KGSL_GPUMEM_CACHE_INV | KGSL_GPUMEM_CACHE_RANGE, -EINVAL, "");
        reset(); single(id, PAGE_SIZE, 1, KGSL_GPUMEM_CACHE_INV | KGSL_GPUMEM_CACHE_RANGE, -ERANGE, "");
        reset(); single(id, UINT64_MAX, PAGE_SIZE, KGSL_GPUMEM_CACHE_INV | KGSL_GPUMEM_CACHE_RANGE, -ERANGE, "");
        reset(); single(id, 1, UINT64_MAX, KGSL_GPUMEM_CACHE_INV | KGSL_GPUMEM_CACHE_RANGE, -ERANGE, "");
    }
    reset(); entries[0].priv_data = NULL;
    assert(_kgsl_gpumem_sync_cache(&entries[0], 0, PAGE_SIZE, KGSL_GPUMEM_CACHE_CLEAN) == -EINVAL && !calls); cases++;
    reset(); metadata[0].dmabuf = NULL;
    assert(_kgsl_gpumem_sync_cache(&entries[0], 0, PAGE_SIZE, KGSL_GPUMEM_CACHE_CLEAN) == -EINVAL && !calls); cases++;
    reset(); single(2, 0, PAGE_SIZE, KGSL_GPUMEM_CACHE_CLEAN, 0, ""); assert(native_calls == 1);
    reset(); bulk(KGSL_GPUMEM_CACHE_CLEAN, 0, "EE", 1);
    reset(); bulk(KGSL_GPUMEM_CACHE_CLEAN | KGSL_GPUMEM_CACHE_RANGE, 0, "EE", 1);
    reset(); end_error = -EIO; bulk(KGSL_GPUMEM_CACHE_CLEAN, -EIO, "E", 0);
    reset(); begin_error = -EINTR; bulk(KGSL_GPUMEM_CACHE_INV, -EINTR, "B", 0);
    reset(); fail_alloc = 1; bulk(KGSL_GPUMEM_CACHE_CLEAN, -ENOMEM, "", 0);
    reset(); fail_alloc = 2; bulk(KGSL_GPUMEM_CACHE_CLEAN, -ENOMEM, "", 0);
    reset(); fail_copy = 1; bulk(KGSL_GPUMEM_CACHE_CLEAN, -EFAULT, "", 0);
    printf("PASS: %u cache cases (WB/WC ordering, exporter errors, range rejection, native route, bulk ref cleanup)\n", cases);
    return 0;
}
