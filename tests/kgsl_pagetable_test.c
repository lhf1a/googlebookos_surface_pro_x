/* Production io-pgtable backend; page table provider, PM and references injected. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint64_t phys_addr_t;
#define PAGE_SIZE 4096UL
#define PAGE_SHIFT 12
#define SZ_4K PAGE_SIZE
#define U8_MAX UINT8_MAX
#define U16_MAX UINT16_MAX
#define GFP_KERNEL 0
#define ARM_64_LPAE_S1 0
#define IO_PGTABLE_QUIRK_ARM_TTBR1 (1UL << 5)
#define IS_ALIGNED(value, alignment) (!((value) & ((alignment) - 1)))
#define IOMMU_WRITE 2
#define IOMMU_READ 1
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
struct mutex { bool held; };
static int held_locks;
static void mutex_init(struct mutex *m) { m->held = false; }
static void mutex_lock(struct mutex *m) { assert(!m->held); m->held = true; held_locks++; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = false; held_locks--; }
static struct mutex *lock_guard(struct mutex *m) { mutex_lock(m); return m; }
static void unlock_guard(struct mutex **m) { mutex_unlock(*m); }
#define guard(name) struct mutex *guard_ptr __attribute__((cleanup(unlock_guard))) = lock_guard
struct kref { union { int count; int refcount; }; };
#ifndef KGSL_PAGETABLE_TEST_FIELDS
#define KGSL_PAGETABLE_TEST_FIELDS
#endif
struct kgsl_pagetable { struct kref refcount; KGSL_PAGETABLE_TEST_FIELDS };
struct device { int refs, pm_refs; };
struct iommu_iotlb_gather;
struct iommu_flush_ops {
    void (*tlb_flush_all)(void *);
    void (*tlb_flush_walk)(unsigned long, size_t, size_t, void *);
};
struct io_pgtable_cfg {
    unsigned long quirks, pgsize_bitmap;
    unsigned ias, oas;
    struct device *iommu_dev;
    const struct iommu_flush_ops *tlb;
    void *alloc, *free;
    struct { u64 ttbr; } arm_lpae_s1_cfg;
};
struct io_pgtable_ops {
    int (*map_pages)(struct io_pgtable_ops *, unsigned long, phys_addr_t, size_t, size_t, int, int, size_t *);
    size_t (*unmap_pages)(struct io_pgtable_ops *, unsigned long, size_t, size_t, struct iommu_iotlb_gather *);
};
#ifndef KGSL_SMMU_TEST_FIELDS
#define KGSL_SMMU_TEST_FIELDS
#endif
struct adreno_smmu_priv {
    KGSL_SMMU_TEST_FIELDS
    const void *cookie;
    const struct io_pgtable_cfg *(*get_ttbr1_cfg)(const void *);
    int (*get_context_bank)(const void *, u32 *, u32 *);
    int (*flush_tlb)(const void *);
};
struct scatterlist { phys_addr_t phys, dma; size_t length; struct scatterlist *next; };
struct sg_table { struct scatterlist *sgl; unsigned nents, orig_nents; };
#define sg_phys(sg) ((sg)->phys)
#define for_each_sg(first, sg, count, i) for ((sg) = (first), (i) = 0; (i) < (count); (i)++, (sg) = (sg)->next)
#include "kgsl_iommu_table.h"

struct fake_ops {
    struct io_pgtable_ops ops;
    struct io_pgtable_cfg cfg;
    void *cookie;
    u64 va[64], pa[64];
    bool present[64];
    int prot[64];
};
static struct kgsl_pagetable parent;
static struct device dev;
static struct io_pgtable_cfg cfg;
static struct adreno_smmu_priv priv;
static int domain_cookie, metadata_error, pm_error, fail_alloc, fail_table, cfg_missing, flush_error;
static int allocations, live_tables, maps, unmaps, flushes, walks, checks;
static int map_fail_call, map_fail_prefix, map_zero_call, unmap_zero_call, max_chunk;
static u32 actual_cb, actual_asid;
static void parent_flush(void *cookie)
{ assert(cookie == &domain_cookie && dev.pm_refs > 0); flushes++; }
static void parent_walk(unsigned long addr, size_t size, size_t granule, void *cookie)
{ assert(addr == PAGE_SIZE && size == PAGE_SIZE && granule == PAGE_SIZE); parent_flush(cookie); walks++; }
static const struct iommu_flush_ops tlb = {parent_flush, parent_walk};
static bool kref_get_unless_zero(struct kref *ref)
{ if (!ref->count) return false; ref->count++; return true; }
static void kgsl_mmu_putpagetable(struct kgsl_pagetable *p)
{ assert(p == &parent && p->refcount.count > 0); p->refcount.count--; }
static struct device *get_device(struct device *d) { assert(d == &dev); d->refs++; return d; }
static void put_device(struct device *d) { assert(d == &dev && d->refs > 1); d->refs--; }
static int pm_runtime_resume_and_get(struct device *d)
{ assert(d == &dev); if (pm_error) return pm_error; d->pm_refs++; return 0; }
static void pm_runtime_put_autosuspend(struct device *d)
{ assert(d == &dev && d->pm_refs > 0); d->pm_refs--; }
static void *kzalloc(size_t bytes, int flags)
{ assert(!flags); if (fail_table) return NULL; return calloc(1, bytes); }
#define kfree free
static const struct io_pgtable_cfg *get_cfg(const void *cookie)
{ assert(cookie == &domain_cookie); return cfg_missing ? NULL : &cfg; }
static int get_bank(const void *cookie, u32 *cb, u32 *asid)
{ assert(cookie == &domain_cookie); *cb = actual_cb; *asid = actual_asid; return metadata_error; }
static int checked_flush(const void *cookie)
{ assert(cookie == &domain_cookie && dev.pm_refs > 0 && held_locks > 0); flushes++; return flush_error; }
static int lookup(struct fake_ops *f, u64 va)
{ for (int i = 0; i < 64; i++) if (f->present[i] && f->va[i] == va) return i; return -1; }
static int map_pages(struct io_pgtable_ops *ops, unsigned long va, phys_addr_t pa,
                     size_t pagesize, size_t count, int prot, int flags, size_t *done)
{
    struct fake_ops *f = (void *)ops;
    assert(dev.pm_refs && pagesize == PAGE_SIZE && count && !flags && !*done);
    assert(((struct kgsl_iommu_table *)f->cookie)->lock.held);
    maps++;
    if (maps == map_zero_call) return 0;
    size_t limit = count;
    if (max_chunk && limit > (size_t)max_chunk) limit = max_chunk;
    if (maps == map_fail_call && limit > (size_t)map_fail_prefix) limit = map_fail_prefix;
    for (size_t j = 0; j < limit; j++) {
        if (lookup(f, va) >= 0) return -EEXIST;
        int slot;
        for (slot = 0; slot < 64 && f->present[slot]; slot++);
        assert(slot < 64);
        f->present[slot] = true; f->va[slot] = va; f->pa[slot] = pa; f->prot[slot] = prot;
        va += PAGE_SIZE; pa += PAGE_SIZE; *done += PAGE_SIZE;
    }
    return maps == map_fail_call ? -ENOSPC : 0;
}
static size_t unmap_pages(struct io_pgtable_ops *ops, unsigned long va,
                         size_t pagesize, size_t count, struct iommu_iotlb_gather *gather)
{
    struct fake_ops *f = (void *)ops;
    size_t done = 0;
    assert(dev.pm_refs && pagesize == PAGE_SIZE && count && !gather);
    assert(((struct kgsl_iommu_table *)f->cookie)->lock.held);
    unmaps++;
    if (unmaps == unmap_zero_call) return 0;
    if (max_chunk && count > (size_t)max_chunk) count = max_chunk;
    for (size_t j = 0; j < count; j++) {
        int slot = lookup(f, va);
        if (slot < 0) break;
        f->present[slot] = false; va += PAGE_SIZE; done += PAGE_SIZE;
    }
    return done;
}
static struct io_pgtable_ops *alloc_io_pgtable_ops(int format, struct io_pgtable_cfg *c, void *cookie)
{
    assert(format == ARM_64_LPAE_S1 && dev.pm_refs);
    assert(c->quirks == 0 && c->pgsize_bitmap == PAGE_SIZE && !c->alloc && !c->free);
    assert(c->tlb && c->tlb != &tlb && cookie);
    if (fail_alloc) return NULL;
    struct fake_ops *f = calloc(1, sizeof(*f)); assert(f);
    c->arm_lpae_s1_cfg.ttbr = 0x200000 + (++allocations * PAGE_SIZE);
    f->cfg = *c; f->cookie = cookie;
    f->ops.map_pages = map_pages; f->ops.unmap_pages = unmap_pages;
    live_tables++;
    return &f->ops;
}
static void free_io_pgtable_ops(struct io_pgtable_ops *ops)
{
    struct fake_ops *f = (void *)ops;
    assert(dev.pm_refs && live_tables > 0);
    for (int i = 0; i < 64; i++) assert(!f->present[i]);
    f->cfg.tlb->tlb_flush_all(f->cookie);
    f->cfg.tlb->tlb_flush_walk(PAGE_SIZE, PAGE_SIZE, PAGE_SIZE, f->cookie);
    free(f); live_tables--;
}
#include "kgsl_iommu_table.c"

static void setup(void)
{
    assert(!live_tables);
    parent.refcount.count = 1; dev = (struct device){.refs=1};
    cfg = (struct io_pgtable_cfg){.quirks=IO_PGTABLE_QUIRK_ARM_TTBR1,
        .pgsize_bitmap=PAGE_SIZE | (1UL << 21), .ias=39, .oas=40, .iommu_dev=&dev, .tlb=&tlb,
        .alloc=&parent, .free=&parent};
    priv = (struct adreno_smmu_priv){.cookie=&domain_cookie, .get_ttbr1_cfg=get_cfg, .get_context_bank=get_bank, .flush_tlb=checked_flush};
    actual_cb = 3; actual_asid = 0x123;
    metadata_error = pm_error = fail_alloc = fail_table = cfg_missing = flush_error = 0;
    maps = unmaps = flushes = walks = 0;
    map_fail_call = map_fail_prefix = map_zero_call = unmap_zero_call = max_chunk = 0;
    checks++;
}
static void balanced(void)
{ assert(parent.refcount.count == 1 && dev.refs == 1 && !dev.pm_refs && !live_tables && !held_locks); }
static struct kgsl_iommu_table *create(void)
{
    struct kgsl_iommu_table *t = kgsl_iommu_table_create(&parent, &priv);
    assert(!IS_ERR(t));
    assert(t->cb_num == 3 && (t->ttbr0 >> 48) == 0x123 && !t->mapped_bytes && !t->poisoned);
    return t;
}
/* Dispose the simulated hardware after testing refusal; no production recovery is implied. */
static void dispose_poisoned_fixture(struct kgsl_iommu_table *t)
{
    assert(t->poisoned && kgsl_iommu_table_destroy(t) == -EBUSY);
    free(t->ops); live_tables--; put_device(t->cfg.iommu_dev); kgsl_mmu_putpagetable(t->parent); free(t);
}
int main(void)
{
    struct scatterlist sg[2] = {{.phys=0x400000, .dma=0xf0000000, .length=2*PAGE_SIZE},
                               {.phys=0x800000, .dma=0xf4000000, .length=3*PAGE_SIZE}};
    sg[0].next = &sg[1];
    struct sg_table sgt = {sg, 1, 2}; /* DMA-coalesced count must not truncate physical SG. */
    const u64 va = 0x4000000000ULL;
    setup();
    struct kgsl_iommu_table *a = create(), *b = create();
    assert(a->ops != b->ops && a->ttbr0 != b->ttbr0 && parent.refcount.count == 3);
    max_chunk = 1;
    assert(!kgsl_iommu_table_map(a, va, &sgt, 5*PAGE_SIZE, IOMMU_READ|IOMMU_WRITE));
    assert(!kgsl_iommu_table_map_guard(b, va, 0xa00000, 2*PAGE_SIZE, IOMMU_READ|IOMMU_WRITE));
    struct fake_ops *fa = (void *)a->ops, *fb = (void *)b->ops;
    assert(fa->pa[lookup(fa, va)] == 0x400000 && fa->pa[lookup(fa, va+2*PAGE_SIZE)] == 0x800000);
    assert(fb->pa[lookup(fb, va)] == 0xa00000 && fb->pa[lookup(fb, va+PAGE_SIZE)] == 0xa00000);
    assert(!(fb->prot[lookup(fb, va)] & IOMMU_WRITE));
    assert(kgsl_iommu_table_destroy(a) == -EBUSY);
    assert(!kgsl_iommu_table_unmap(a, va, 5*PAGE_SIZE));
    assert(lookup(fb, va) >= 0 && !a->mapped_bytes && b->mapped_bytes == 2*PAGE_SIZE);
    assert(!kgsl_iommu_table_destroy(a));
    assert(parent.refcount.count == 2);
    assert(!kgsl_iommu_table_unmap(b, va, 2*PAGE_SIZE));
    assert(!kgsl_iommu_table_destroy(b)); assert(walks == 2); balanced();

    for (int bad = 0; bad < 17; bad++) {
        setup();
        if (bad==0) priv.cookie=NULL;
        if (bad==1) priv.get_context_bank=NULL;
        if (bad==2) metadata_error=-EIO;
        if (bad==3) cfg_missing=1;
        if (bad==4) cfg.quirks=0;
        if (bad==5) cfg.ias=38;
        if (bad==6) cfg.ias=49;
        if (bad==7) cfg.oas=49;
        if (bad==8) cfg.pgsize_bitmap=1UL<<21;
        if (bad==9) cfg.iommu_dev=NULL;
        if (bad==10) cfg.tlb=NULL;
        if (bad==11) actual_cb=256;
        if (bad==12) actual_asid=65536;
        if (bad==13) fail_table=1;
        if (bad==14) fail_alloc=1;
        if (bad==15) pm_error=-ETIMEDOUT;
        if (bad==16) priv.flush_tlb=NULL;
        a=kgsl_iommu_table_create(&parent,&priv); assert(IS_ERR(a)); balanced();
    }
    setup(); parent.refcount.count=0;
    assert(PTR_ERR(kgsl_iommu_table_create(&parent,&priv))==-ENODEV);
    assert(!live_tables && !dev.pm_refs && dev.refs==1); parent.refcount.count=1;
    for (int bad = 0; bad < 8; bad++) {
        setup(); a=create();
        struct scatterlist local[2]={sg[0],sg[1]}; local[0].next=&local[1];
        struct sg_table tmp={local,1,2};
        u64 addr=va; size_t size=5*PAGE_SIZE;
        if (bad==0) addr++;
        if (bad==1) size--;
        if (bad==2) addr=(1ULL<<39)-PAGE_SIZE;
        if (bad==3) addr=UINT64_MAX-PAGE_SIZE+1;
        if (bad==4) local[1].phys++;
        if (bad==5) local[1].phys=(1ULL<<40)-PAGE_SIZE;
        if (bad==6) local[1].length=0;
        if (bad==7) size=6*PAGE_SIZE;
        assert(kgsl_iommu_table_map(a,addr,&tmp,size,3)==-EINVAL);
        assert(!maps && !a->mapped_bytes && !dev.pm_refs);
        assert(!kgsl_iommu_table_destroy(a)); balanced();
    }
    for (int prefix=0; prefix<=3; prefix++) {
        setup(); a=create(); map_fail_call=2; map_fail_prefix=prefix;
        assert(kgsl_iommu_table_map(a,va,&sgt,5*PAGE_SIZE,3)==-ENOSPC);
        assert(!a->mapped_bytes && !a->poisoned && flushes==1);
        assert(!kgsl_iommu_table_destroy(a)); balanced();
    }
    setup(); a=create(); map_zero_call=2;
    assert(kgsl_iommu_table_map(a,va,&sgt,5*PAGE_SIZE,3)==-EIO);
    assert(!a->mapped_bytes && !a->poisoned); assert(!kgsl_iommu_table_destroy(a)); balanced();
    setup(); a=create(); map_fail_call=2; map_fail_prefix=1; unmap_zero_call=1;
    assert(kgsl_iommu_table_map(a,va,&sgt,5*PAGE_SIZE,3)==-EUCLEAN);
    assert(a->poisoned && a->mapped_bytes==3*PAGE_SIZE && !dev.pm_refs);
    assert(kgsl_iommu_table_map_guard(a,va,0x400000,PAGE_SIZE,3)==-EUCLEAN);
    assert(kgsl_iommu_table_unmap(a,va,3*PAGE_SIZE)==-EUCLEAN);
    dispose_poisoned_fixture(a); balanced();
    setup(); a=create(); assert(!kgsl_iommu_table_map(a,va,&sgt,5*PAGE_SIZE,3));
    max_chunk=1; unmap_zero_call=3;
    assert(kgsl_iommu_table_unmap(a,va,5*PAGE_SIZE)==-EUCLEAN);
    assert(a->mapped_bytes==3*PAGE_SIZE && a->poisoned && !dev.pm_refs);
    dispose_poisoned_fixture(a); balanced();
    setup(); a=create(); pm_error=-EIO;
    assert(kgsl_iommu_table_map(a,va,&sgt,5*PAGE_SIZE,3)==-EIO && !maps);
    assert(kgsl_iommu_table_destroy(a)==-EIO && parent.refcount.count==2 && live_tables==1);
    pm_error=0; assert(!kgsl_iommu_table_map(a,va,&sgt,5*PAGE_SIZE,3));
    pm_error=-EAGAIN; assert(kgsl_iommu_table_unmap(a,va,5*PAGE_SIZE)==-EAGAIN);
    assert(a->mapped_bytes==5*PAGE_SIZE && !unmaps);
    pm_error=0; assert(!kgsl_iommu_table_unmap(a,va,5*PAGE_SIZE));
    assert(!kgsl_iommu_table_destroy(a)); balanced();
    setup(); a=create(); max_chunk=1;
    assert(!kgsl_iommu_table_map_guard(a,va+PAGE_SIZE,0x800000,PAGE_SIZE,1));
    assert(kgsl_iommu_table_map_guard(a,va,0x400000,2*PAGE_SIZE,1)==-EEXIST);
    assert(a->mapped_bytes==PAGE_SIZE && !a->poisoned);
    fa=(void *)a->ops; assert(fa->pa[lookup(fa,va+PAGE_SIZE)]==0x800000 && lookup(fa,va)<0);
    assert(!kgsl_iommu_table_unmap(a,va+PAGE_SIZE,PAGE_SIZE));
    assert(!kgsl_iommu_table_destroy(a)); balanced();
    setup(); a=create(); flush_error=-ETIMEDOUT;
    assert(kgsl_iommu_table_destroy(a)==-ETIMEDOUT);
    assert(live_tables==1 && parent.refcount.count==2 && dev.refs==2 && !dev.pm_refs);
    flush_error=0; assert(!kgsl_iommu_table_destroy(a)); balanced();
    setup(); a=create(); assert(!kgsl_iommu_table_map(a,va,&sgt,5*PAGE_SIZE,3));
    flush_error=-ETIMEDOUT;
    assert(kgsl_iommu_table_unmap(a,va,5*PAGE_SIZE)==-EUCLEAN);
    assert(a->poisoned && !a->mapped_bytes && !dev.pm_refs);
    dispose_poisoned_fixture(a); balanced();
    setup(); a=create(); map_fail_call=2; map_fail_prefix=1; flush_error=-ETIMEDOUT;
    assert(kgsl_iommu_table_map(a,va,&sgt,5*PAGE_SIZE,3)==-EUCLEAN);
    assert(a->poisoned && !a->mapped_bytes && !dev.pm_refs);
    dispose_poisoned_fixture(a); balanced();
    printf("PASS: %d native pagetable scenarios (distinct roots/SG/partial rollback/PM/lifetime)\n", checks);
    return 0;
}
