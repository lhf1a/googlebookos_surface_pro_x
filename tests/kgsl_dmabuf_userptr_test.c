#define KGSL_MEMDESC_MAP_UNCERTAIN (1U << 10)
/* Actual user-page import/destruction; external VM/GUP/SG operations are mocked. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t u64;
#define PAGE_SIZE 4096UL
#define PAGE_SHIFT 12
#define GFP_KERNEL 0
#define FOLL_WRITE 1
#define FOLL_LONGTERM 2
#define KGSL_MEMFLAGS_GPUREADONLY 4
#define KGSL_MEMDESC_MAPPED 8
#define KGSL_CACHE_OP_FLUSH 1
struct page { int pins, dirty; };
struct scatterlist { struct page *page; unsigned length; struct scatterlist *next; };
struct sg_table { struct scatterlist *sgl; unsigned nents, orig_nents; };
struct kgsl_memdesc { u64 size, flags; unsigned priv; struct sg_table *sgt; };
struct file { void *f_op; };
struct vm_area_struct { unsigned long vm_start, vm_end; struct file *vm_file; };
struct mm_struct { int unused; };
struct task_struct { struct mm_struct *mm; };
static struct mm_struct mm;
static struct task_struct task = { &mm }, *current = &task;
static int kgsl_fops;
static struct file our_file = { &kgsl_fops };
static struct vm_area_struct vmas[2];
static struct page test_pages[4];
static struct scatterlist sg[2];
static int fail_array, fail_sgt, fail_sg, cache_error, locks, live_arrays, live_sgts, live_sg;
static int pin_calls, unpins, dirty_pages, sg_calls, cache_calls;
static unsigned pin_flags;
static long pin_result;
#define sg_page(s) ((s)->page)
#define for_each_sgtable_sg(t,s,i) \
    for ((i)=0,(s)=(t)->sgl; (i)<(int)(t)->orig_nents; (i)++,(s)=(s)->next)
static void *kvcalloc(size_t n, size_t size, int flags)
{
    assert(n == 4 && !live_arrays);
    if (fail_array) return NULL;
    ++live_arrays;
    return calloc(n, size);
}
static void kvfree(void *p) { if (p) { assert(live_arrays == 1); --live_arrays; free(p); } }
static void *kmalloc(size_t size, int flags)
{
    assert(!live_sgts);
    if (fail_sgt) return NULL;
    ++live_sgts;
    return calloc(1, size);
}
static void kfree(void *p) { if (p) { assert(live_sgts == 1 && !live_sg); --live_sgts; free(p); } }
static void mmap_read_lock(struct mm_struct *m) { assert(m == &mm && !locks); ++locks; }
static void mmap_read_unlock(struct mm_struct *m) { assert(m == &mm && locks == 1); --locks; }
static struct vm_area_struct *find_vma(struct mm_struct *m, unsigned long addr)
{
    assert(m == &mm && locks == 1);
    for (int i = 0; i < 2; ++i) if (vmas[i].vm_end > addr) return &vmas[i];
    return NULL;
}
static long pin_user_pages(unsigned long addr, unsigned long count, unsigned flags, struct page **pages)
{
    assert(locks == 1 && count == 4 && addr == 0x10000 && (flags & FOLL_LONGTERM));
    ++pin_calls; pin_flags = flags;
    for (long i = 0; i < pin_result; ++i) { pages[i] = &test_pages[i]; ++pages[i]->pins; }
    return pin_result;
}
static void unpin_user_pages(struct page **pages, unsigned long count)
{
    assert(!locks && count > 0 && count <= 4);
    for (unsigned long i = 0; i < count; ++i) { assert(pages[i]->pins == 1); --pages[i]->pins; ++unpins; }
}
static void unpin_user_page_range_dirty_lock(struct page *p, unsigned long n, bool dirty)
{
    for (unsigned long i = 0; i < n; ++i) {
        assert(p[i].pins == 1); --p[i].pins; ++unpins;
        if (dirty) { p[i].dirty = 1; ++dirty_pages; }
    }
}
static int sg_alloc_table_from_pages(struct sg_table *table, struct page **pages,
                                    unsigned long n, unsigned offset, u64 size, int gfp)
{
    assert(!locks && n == 4 && !offset && size == PAGE_SIZE * 4 && !live_sg);
    for (int i = 0; i < 4; ++i) assert(pages[i] == &test_pages[i] && test_pages[i].pins == 1);
    ++sg_calls;
    if (fail_sg) return -ENOMEM;
    sg[0] = (struct scatterlist){ test_pages, PAGE_SIZE * 2, &sg[1] };
    sg[1] = (struct scatterlist){ test_pages + 2, PAGE_SIZE * 2, NULL };
    *table = (struct sg_table){ sg, 2, 2 };
    ++live_sg;
    return 0;
}
static void sg_free_table(struct sg_table *table) { assert(table && live_sg == 1); --live_sg; }
static int kgsl_cache_range_op(struct kgsl_memdesc *m, u64 offset, u64 length, int op)
{
    assert(m->sgt && !offset && length == m->size && op == KGSL_CACHE_OP_FLUSH);
    ++cache_calls;
    return cache_error;
}
#include "kgsl-userptr-functions.h"
static struct kgsl_memdesc reset(void)
{
    assert(!locks && !live_arrays && !live_sgts && !live_sg);
    for (int i = 0; i < 4; ++i) assert(!test_pages[i].pins);
    memset(test_pages, 0, sizeof(test_pages));
    vmas[0] = (struct vm_area_struct){ 0x10000, 0x12000, NULL };
    vmas[1] = (struct vm_area_struct){ 0x12000, 0x14000, NULL };
    fail_array = fail_sgt = fail_sg = cache_error = 0;
    pin_calls = unpins = dirty_pages = sg_calls = cache_calls = 0;
    pin_result = 4; pin_flags = 0;
    return (struct kgsl_memdesc){ .size = PAGE_SIZE * 4 };
}
int main(void)
{
    unsigned cases = 0;
    for (int scenario = 0; scenario < 11; ++scenario) {
        struct kgsl_memdesc mem = reset();
        unsigned long addr = 0x10000;
        int expected = -ENOMEM;
        if (scenario == 0) fail_array = 1;
        if (scenario == 1) fail_sgt = 1;
        if (scenario == 2) { vmas[0].vm_start += PAGE_SIZE; expected = -EFAULT; }
        if (scenario == 3) { vmas[1].vm_start += PAGE_SIZE; expected = -EFAULT; }
        if (scenario == 4) { vmas[1].vm_end = 0x13000; expected = -EFAULT; }
        if (scenario == 5) { vmas[0].vm_file = &our_file; expected = -EFAULT; }
        if (scenario == 6) { addr = ULONG_MAX - PAGE_SIZE + 1; expected = -EFAULT; }
        if (scenario == 7) { pin_result = -EIO; expected = -EIO; }
        if (scenario == 8) { pin_result = 0; expected = -EINVAL; }
        if (scenario == 9) { pin_result = 2; expected = -EINVAL; }
        if (scenario == 10) fail_sg = 1;
        assert(memdesc_sg_virt(&mem, addr) == expected);
        assert(!mem.sgt && !locks && !live_arrays && !live_sgts && !live_sg && !dirty_pages);
        assert(unpins == (scenario == 9 ? 2 : scenario == 10 ? 4 : 0));
        ++cases;
    }
    struct kgsl_memdesc mem = reset();
    cache_error = -EIO;
    assert(memdesc_sg_virt(&mem, 0x10000) == -EIO);
    assert(unpins == 4 && !mem.sgt && !live_sg && !live_sgts && !live_arrays && !dirty_pages);
    ++cases;
    mem = reset(); mem.size = 0;
    assert(memdesc_sg_virt(&mem, 0x10000) == -EINVAL);
    assert(!pin_calls && !live_arrays && !live_sgts);
    ++cases;
    for (int readonly = 0; readonly < 2; ++readonly) {
        mem = reset();
        if (readonly) mem.flags = KGSL_MEMFLAGS_GPUREADONLY;
        assert(memdesc_sg_virt(&mem, 0x10000) == 0);
        assert(!live_arrays && live_sg == 1 && live_sgts == 1 && pin_calls == 1 && cache_calls == 1);
        assert(pin_flags == (FOLL_LONGTERM | (readonly ? 0 : FOLL_WRITE)));
        /* Simulate SG coalescing by another DMA consumer: original count remains authoritative. */
        mem.sgt->nents = 1;
        mem.priv = KGSL_MEMDESC_MAPPED;
        kgsl_destroy_anon(&mem);
        assert(!unpins && live_sg && live_sgts);
        mem.priv = KGSL_MEMDESC_MAP_UNCERTAIN;
        kgsl_destroy_anon(&mem);
        assert(!unpins && live_sg && live_sgts);
        mem.priv = 0;
        kgsl_destroy_anon(&mem);
        assert(!mem.sgt && unpins == 4 && dirty_pages == (readonly ? 0 : 4));
        assert(!live_sg && !live_sgts && !locks);
        kgsl_destroy_anon(&mem);
        assert(unpins == 4);
        ++cases;
    }
    printf("PASS: %u userptr cases (long-term pins, VMA gaps/overflow, failure cleanup, dirty unpin)\n", cases);
    return 0;
}
