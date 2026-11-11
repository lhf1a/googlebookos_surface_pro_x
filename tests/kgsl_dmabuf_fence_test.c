/* Actual KGSL allocation/predicate plus matching mainline init64/comparison. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint64_t u64;
typedef int spinlock_t;
#define GFP_KERNEL 0
#define DMA_FENCE_FLAG_SEQNO64_BIT 2
#define BIT(n) (1UL << (n))
#define lower_32_bits(n) ((uint32_t)(n))
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
struct dma_fence;
struct dma_fence_ops { bool (*signaled)(struct dma_fence *); };
struct dma_fence { u64 context, seqno; unsigned long flags; const struct dma_fence_ops *ops; };
struct list_head { bool initialized; };
struct kgsl_timeline { u64 context, value; spinlock_t lock; int id, refs; };
struct kgsl_timeline_fence { struct dma_fence base; struct kgsl_timeline *timeline; struct list_head node; };
#define to_timeline_fence(p) ((struct kgsl_timeline_fence *)(p))
static int allocated, fail_alloc, fail_ref, added;
static const struct dma_fence_ops timeline_fence_ops;
static bool test_bit(unsigned bit, const unsigned long *flags) { return !!(*flags & BIT(bit)); }
static void __dma_fence_init(struct dma_fence *f, const struct dma_fence_ops *ops,
                              spinlock_t *lock, u64 context, u64 seqno, unsigned long flags)
{ *f = (struct dma_fence){ .context = context, .seqno = seqno, .flags = flags, .ops = ops }; }
#include "kernel-fence-functions.h"
static void *kzalloc(size_t size, int flags)
{ if (fail_alloc) return NULL; ++allocated; return calloc(1, size); }
static void kfree(void *p) { assert(p && allocated == 1); --allocated; free(p); }
static struct kgsl_timeline *kgsl_timeline_get(struct kgsl_timeline *t)
{ if (fail_ref) return NULL; ++t->refs; return t; }
#define INIT_LIST_HEAD(p) ((p)->initialized = true)
static void spin_lock_irq(spinlock_t *lock) { assert(!*lock); *lock = 1; }
static void spin_unlock_irq(spinlock_t *lock) { assert(*lock == 1); *lock = 0; }
static bool dma_fence_is_signaled_locked(struct dma_fence *f)
{ assert(to_timeline_fence(f)->timeline->lock == 1); return f->ops->signaled(f); }
static void kgsl_timeline_add_fence(struct kgsl_timeline *t, struct kgsl_timeline_fence *f)
{ assert(t->lock == 1 && f->node.initialized && !added); ++added; }
static void trace_kgsl_timeline_fence_alloc(int id, u64 seqno) { assert(id == 7); }
#include "kgsl-fence-functions.h"
static const struct dma_fence_ops timeline_fence_ops = { .signaled = timeline_fence_signaled };
int main(void)
{
    const u64 values[][2] = {
        { 0, 1ULL << 32 }, { 0, 1 }, { 1ULL << 33, 1 },
        { UINT64_MAX, UINT64_MAX }, { UINT64_MAX - 1, UINT64_MAX },
        { 1ULL << 63, 0 }, { (1ULL << 32) - 1, 1ULL << 32 }, { 42, 42 },
    };
    unsigned cases = 0;
    for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
        struct kgsl_timeline timeline = { .context = 1234, .value = values[i][0], .id = 7 };
        added = 0;
        struct dma_fence *f = kgsl_timeline_fence_alloc(&timeline, values[i][1]);
        assert(!IS_ERR(f) && f->seqno == values[i][1] && f->context == timeline.context);
        bool done = values[i][0] >= values[i][1];
        assert(f->ops->signaled(f) == done && added == !done);
        assert(test_bit(DMA_FENCE_FLAG_SEQNO64_BIT, &f->flags));
        assert(timeline.refs == 1 && !timeline.lock);
        kfree(to_timeline_fence(f));
        ++cases;
    }
    struct kgsl_timeline timeline = { .context = 1234, .id = 7 };
    fail_alloc = 1;
    assert(PTR_ERR(kgsl_timeline_fence_alloc(&timeline, 1)) == -ENOMEM);
    assert(!allocated && !timeline.refs);
    ++cases;
    fail_alloc = 0; fail_ref = 1;
    assert(PTR_ERR(kgsl_timeline_fence_alloc(&timeline, 1)) == -ENOENT);
    assert(!allocated && !timeline.refs);
    ++cases;
    printf("PASS: %u fence cases (real 6.18 64-bit comparison, no early completion, allocation/reference failure)\n", cases);
    return 0;
}
