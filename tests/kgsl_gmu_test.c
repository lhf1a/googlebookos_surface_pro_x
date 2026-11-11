/* Actual driver functions with DMA/IOMMU ownership fault injection. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t dma_addr_t;
#define PAGE_SIZE 4096U
#define U32_MAX UINT32_MAX
#define GFP_KERNEL 0x10
#define PAGE_ALIGN(x) (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define IS_ALIGNED(x, a) (!((x) & ((a) - 1)))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define max_t(t, a, b) ((t)(a) > (t)(b) ? (t)(a) : (t)(b))
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define IOMMU_READ 1
#define IOMMU_WRITE 2
#define IOMMU_PRIV 4
#define IOMMU_DOMAIN_IDENTITY 4
#define BIT(n) (1U << (n))
#include "kgsl-gmu-constants.h"
#define KGSL_MEMFLAGS_GPUREADONLY BIT(24)
#define KGSL_MEMFLAGS_SECURE BIT(25)
#define dev_err(...) ((void)0)
#define NUM_HFI_MEMKINDS 1
static const char *memkind_strings[] = { "TEST" };
#define GMU_CACHE 0
#define GMU_NONCACHED_KERNEL 1
#define GMU_MEM_TYPE_MAX 2
struct device { int dummy; };
struct platform_device { struct device dev; };
struct iommu_domain { unsigned type; };
struct scatterlist { unsigned length; u64 phys; bool bus; struct scatterlist *next; };
struct sg_table { struct scatterlist *sgl; unsigned nents, orig_nents; };
#define for_each_sg(list, s, count, i) \
    for ((i) = 0, (s) = (list); (i) < (count); (i)++, (s) = (s)->next)
#define sg_phys(s) ((s)->phys)
#define sg_dma_is_bus_address(s) ((s)->bus)
struct gmu_vma_entry { u32 start, size, next_va; };
struct gmu_memdesc { void *hostptr; u32 gmuaddr; dma_addr_t dma_handle; u32 size; };
struct qmp { int dummy; };
struct a6xx_gmu_device {
    int power_error;
    struct qmp *qmp;
    struct iommu_domain *domain;
    struct platform_device *pdev;
    struct gmu_vma_entry vma[GMU_MEM_TYPE_MAX];
    unsigned global_entries;
    struct gmu_memdesc gmu_globals[3];
};
struct adreno_device { struct a6xx_gmu_device *gmu; bool acd_enabled; };
struct kgsl_device { struct adreno_device *adreno; };
#define ADRENO_DEVICE(d) ((d)->adreno)
#define to_a6xx_gmu(a) ((a)->gmu)
struct kgsl_memdesc;
struct kgsl_memdesc_ops { void (*free)(struct kgsl_memdesc *); };
struct kgsl_pagetable_ops { void (*put_gpuaddr)(struct kgsl_memdesc *); };
struct kgsl_pagetable { struct kgsl_pagetable_ops *pt_ops; };
struct kgsl_memdesc {
    u64 size, gpuaddr; u32 priv; void *pages; struct sg_table *sgt; bool secure;
    struct kgsl_pagetable *pagetable; const struct kgsl_memdesc_ops *ops;
};
#define PT_OP_VALID(p, op) ((p) && (p)->pt_ops && (p)->pt_ops->op)
static bool kgsl_memdesc_is_global(const struct kgsl_memdesc *m) { return m->priv & KGSL_MEMDESC_GLOBAL; }
static int kgsl_mmu_unmap(struct kgsl_pagetable *p, struct kgsl_memdesc *m) { assert(0); return -EIO; }
static bool kgsl_memdesc_is_secured(const struct kgsl_memdesc *m) { return m->secure; }
struct hfi_mem_alloc_desc {
    u32 gmu_addr, size, mem_kind, gmu_mem_handle, host_mem_handle, flags;
    u64 gpu_addr;
};
struct mem_alloc_entry { struct kgsl_memdesc *gpu_md; struct hfi_mem_alloc_desc desc; };
struct a6xx_hwsched_hfi { struct mem_alloc_entry mem_alloc_table[32]; unsigned mem_alloc_entries; };
static struct a6xx_hwsched_hfi test_hfi;
#define to_a6xx_hwsched_hfi(a) (&test_hfi)
static struct a6xx_gmu_device gmu;
static struct adreno_device adreno = { .gmu = &gmu };
static struct kgsl_device global_device = { &adreno };
#define KGSL_DEVICE(a) (&global_device)
static struct platform_device pdev;
static struct iommu_domain domain, default_domain;
static struct scatterlist segments[2];
static struct sg_table table;
static unsigned char backing[PAGE_SIZE * 2];
static ssize_t map_result;
static int map_calls, unmap_calls, alloc_calls, free_calls, sg_calls, sg_frees;
static int temporary_allocs, temporary_frees, alloc_error, sg_error, temp_error;
static int domain_error, attach_error, domain_allocs, attaches, detaches, domain_frees, handlers;
static int enable_error, enable_calls, detach_error;
static bool attached;
static unsigned allocation_size, map_attrs;
static u64 map_addr, unmap_addr, unmap_size;
static const dma_addr_t dma_handle = 0xfedc0000; /* deliberately not physical */
static ssize_t iommu_map_sg(struct iommu_domain *d, u64 addr, struct scatterlist *s,
                          unsigned nents, int attrs, int gfp)
{
    assert(d == &domain && s == segments && nents == 2 && gfp == GFP_KERNEL);
    assert(s[0].phys == 0x12340000 && s[1].phys == 0x45670000);
    ++map_calls; map_addr = addr; map_attrs = attrs;
    return map_result;
}
static int drain_error, drain_calls;
static ssize_t unmap_result;
static int arm_smmu_flush_tlb_checked(struct iommu_domain *d) { assert(d == &domain && unmap_calls); ++drain_calls; return drain_error; }
static size_t iommu_unmap(struct iommu_domain *d, u64 addr, size_t size)
{
    assert(d == &domain && !free_calls);
    ++unmap_calls; unmap_addr = addr; unmap_size = size;
    return unmap_result < 0 ? size : (size_t)unmap_result;
}
static void *dma_alloc_attrs(struct device *d, size_t size, dma_addr_t *handle,
                             int gfp, unsigned long attrs)
{
    assert(d == &pdev.dev && size == sizeof(backing) && gfp == GFP_KERNEL && !attrs);
    ++alloc_calls; allocation_size = size; *handle = dma_handle;
    return alloc_error ? NULL : backing;
}
static void dma_free_attrs(struct device *d, size_t size, void *cpu,
                           dma_addr_t handle, unsigned long attrs)
{
    assert(d == &pdev.dev && cpu == backing && handle == dma_handle && !attrs);
    assert(size == allocation_size && !free_calls && !alloc_error);
    assert(!map_calls || (drain_calls && !drain_error));
    ++free_calls;
}
static int dma_get_sgtable_attrs(struct device *d, struct sg_table *sgt, void *cpu,
                                dma_addr_t handle, size_t size, unsigned long attrs)
{
    assert(d == &pdev.dev && cpu == backing && handle == dma_handle && !attrs);
    assert(size == sizeof(backing) && !memcmp(backing, (unsigned char[sizeof(backing)]){0}, size));
    ++sg_calls;
    if (!sg_error) *sgt = table;
    return sg_error;
}
static void sg_free_table(struct sg_table *s) { assert(s->sgl == segments && !sg_error); ++sg_frees; }
static struct sg_table *kgsl_alloc_sgt_from_pages(struct kgsl_memdesc *md)
{
    assert(md->pages); ++temporary_allocs;
    return temp_error ? ERR_PTR(temp_error) : &table;
}
static void kgsl_free_sgt(struct sg_table *s) { assert(s == &table && !temp_error); ++temporary_frees; }
static struct iommu_domain *iommu_get_domain_for_dev(struct device *d) { return attached ? &domain : &default_domain; }
static int arm_smmu_enable_checked_unmap(struct iommu_domain *d) { assert(d == &domain && attached); ++enable_calls; return enable_error; }
static struct iommu_domain *iommu_paging_domain_alloc(struct device *d)
{
    assert(d == &pdev.dev); ++domain_allocs;
    return domain_error ? ERR_PTR(domain_error) : &domain;
}
static int a6xx_gmu_iommu_fault_handler(struct iommu_domain *d, struct device *dev,
                                        unsigned long addr, int flags, void *token) { return 0; }
static void iommu_set_fault_handler(struct iommu_domain *d,
    int (*handler)(struct iommu_domain *, struct device *, unsigned long, int, void *), void *token)
{
    assert(d == &domain && handler == a6xx_gmu_iommu_fault_handler && token == &gmu);
    ++handlers;
}
static int iommu_attach_device(struct iommu_domain *d, struct device *dev)
{
    assert(d == &domain && dev == &pdev.dev && handlers == 1); ++attaches; attached = !attach_error; return attach_error;
}
static void iommu_detach_device(struct iommu_domain *d, struct device *dev)
{
    assert(d == &domain && dev == &pdev.dev); ++detaches; if (!detach_error) attached = false;
}
static void iommu_domain_free(struct iommu_domain *d) { assert(d == &domain); ++domain_frees; }
struct a6xx_acd_change { bool enabled; int error; };
static struct qmp qmp_handle;
static int qmp_get_error, qmp_send_error, qmp_gets, qmp_sends, cycle_error, cycles;
static bool qmp_value;
static struct qmp *qmp_get(struct device *d)
{
    assert(d == &pdev.dev); ++qmp_gets;
    return qmp_get_error ? ERR_PTR(qmp_get_error) : &qmp_handle;
}
static int qmp_send(struct qmp *q, const char *fmt, int value)
{
    assert(q == &qmp_handle && !strcmp(fmt, "{class: gpu, res: acd, val: %d}"));
    ++qmp_sends; qmp_value = value; return qmp_send_error;
}
static int adreno_power_cycle(struct adreno_device *a,
    void (*callback)(struct adreno_device *, void *), void *priv)
{
    assert(a == &adreno); ++cycles;
    if (!cycle_error) callback(a, priv);
    return cycle_error;
}
static struct kgsl_memdesc global_md;
static int global_allocs, global_error, backing_frees;
static void free_global_backing(struct kgsl_memdesc *md) { assert(md == &global_md); ++backing_frees; }
static const struct kgsl_memdesc_ops global_ops = { free_global_backing };
static struct kgsl_memdesc *kgsl_allocate_global(struct kgsl_device *d, u32 size,
    unsigned padding, u64 flags, u32 priv, const char *name)
{
    assert(d == &global_device && size == sizeof(backing) && !padding);
    assert(!(flags & KGSL_MEMFLAGS_SECURE) && !strcmp(name, "TEST"));
    ++global_allocs;
    if (global_error) return ERR_PTR(global_error);
    global_md = (struct kgsl_memdesc){.size=size, .gpuaddr=0x123400000ULL,
        .priv=priv | KGSL_MEMDESC_GLOBAL, .sgt=&table, .ops=&global_ops};
    return &global_md;
}
#include "kgsl-gmu-functions.h"
static struct mem_alloc_entry reset(void)
{
    static struct kgsl_memdesc gpu;
    gmu = (struct a6xx_gmu_device){ .domain = &domain, .pdev = &pdev,
        .vma = {{0x4000, 0x10000, 0x4000}, {0x60000000, 0x20000000, 0x60000000}} };
    segments[0] = (struct scatterlist){PAGE_SIZE, 0x12340000, false, &segments[1]};
    segments[1] = (struct scatterlist){PAGE_SIZE, 0x45670000, false, NULL};
    table = (struct sg_table){segments, 1, 2};
    gpu = (struct kgsl_memdesc){ .size = PAGE_SIZE * 2, .sgt = &table };
    memset(backing, 0xa5, sizeof(backing));
    map_result = sizeof(backing);
    drain_error = drain_calls = 0; unmap_result = -1;
    map_calls = unmap_calls = alloc_calls = free_calls = sg_calls = sg_frees = 0;
    temporary_allocs = temporary_frees = alloc_error = sg_error = temp_error = 0;
    domain_error = attach_error = domain_allocs = attaches = detaches = domain_frees = handlers = 0;
    default_domain.type = IOMMU_DOMAIN_IDENTITY;
    enable_error = enable_calls = detach_error = 0; attached = false;
    allocation_size = 0; map_addr = unmap_addr = unmap_size = 0;
    qmp_get_error = qmp_send_error = qmp_gets = qmp_sends = cycle_error = cycles = 0;
    adreno.acd_enabled = false; qmp_value = false;
    memset(&test_hfi, 0, sizeof(test_hfi));
    memset(&global_md, 0, sizeof(global_md));
    global_allocs = global_error = backing_frees = 0;
    return (struct mem_alloc_entry){ .gpu_md=&gpu, .desc={.gmu_addr=0xdeadbeef, .size=PAGE_SIZE * 2} };
}
static unsigned import_ownership(void)
{
    unsigned cases = 0;
    const ssize_t results[] = {-ENOMEM, -EIO, 0, PAGE_SIZE, PAGE_SIZE * 3};
    for (unsigned i = 0; i < ARRAY_SIZE(results); i++) {
        reset(); map_result = results[i];
        int expected = map_result < 0 ? map_result : -EIO;
        struct hfi_mem_alloc_desc desc = {.size=sizeof(backing), .mem_kind=0,
            .gmu_mem_handle=27, .flags=MEMFLAG_GFX_PRIV | MEMFLAG_GFX_WRITEABLE};
        struct hfi_mem_alloc_desc before = desc;
        assert(process_mem_alloc(&adreno, &desc) == expected);
        assert(!memcmp(&desc, &before, sizeof(desc))); /* No success handle/address. */
        assert(test_hfi.mem_alloc_entries == 1 && global_allocs == 1 && map_calls == 1);
        struct mem_alloc_entry *entry = &test_hfi.mem_alloc_table[0];
        assert(entry->gpu_md == &global_md && entry->desc.gmu_mem_handle == 27);
        assert(entry->desc.gmu_addr == 0x60000000 && gmu.vma[1].next_va == 0x60002000);
        assert(global_md.priv & KGSL_MEMDESC_PRIVILEGED);
        assert(global_md.priv & KGSL_MEMDESC_MAP_UNCERTAIN);
        assert(gmu.power_error == expected && !unmap_calls);
        struct kgsl_memdesc saved = global_md;
        assert(kgsl_sharedmem_free(&global_md) == -EUCLEAN && !backing_frees);
        assert(!memcmp(&saved, &global_md, sizeof(saved)));
        assert(process_mem_alloc(&adreno, &desc) == expected);
        assert(test_hfi.mem_alloc_entries == 1 && global_allocs == 1 && map_calls == 1);
        /* Fault injection only: clearing the global latch must not validate an uncertain entry. */
        gmu.power_error = 0;
        assert(process_mem_alloc(&adreno, &desc) == -EUCLEAN);
        assert(gmu_import_buffer(&adreno, entry, 0) == -EUCLEAN);
        assert(!memcmp(&desc, &before, sizeof(desc)) && global_allocs == 1 && map_calls == 1);
        cases++;
    }
    for (unsigned scenario = 0; scenario < 5; scenario++) {
        reset();
        struct hfi_mem_alloc_desc desc = {.size=sizeof(backing), .gmu_mem_handle=27};
        int expected = 0;
        if (scenario == 1) { global_error = -ENOMEM; expected = -ENOMEM; }
        if (scenario == 2) { segments[0].phys++; expected = -EINVAL; }
        if (scenario == 3) { desc.flags = MEMFLAG_GFX_SECURE; expected = -EOPNOTSUPP; }
        if (scenario == 4) { gmu.power_error = -ETIMEDOUT; expected = -ETIMEDOUT; }
        assert(process_mem_alloc(&adreno, &desc) == expected);
        if (!expected) {
            assert(desc.host_mem_handle == 27 && desc.gpu_addr == global_md.gpuaddr);
            assert(desc.gmu_addr == 0x60000000 && test_hfi.mem_alloc_entries == 1);
            assert(!process_mem_alloc(&adreno, &desc) && global_allocs == 1 && map_calls == 1);
        } else {
            assert(!test_hfi.mem_alloc_entries && !desc.host_mem_handle && !map_calls);
            assert(!(global_md.priv & KGSL_MEMDESC_MAP_UNCERTAIN));
            if (scenario == 2) assert(!kgsl_sharedmem_free(&global_md) && backing_frees == 1);
        }
        cases++;
    }
    for (unsigned scenario = 0; scenario < 5; scenario++) {
        reset();
        struct hfi_mem_alloc_desc desc = {.size=sizeof(backing), .gmu_mem_handle=27};
        assert(!process_mem_alloc(&adreno, &desc));
        assert(global_md.priv & KGSL_MEMDESC_GMU_MAPPED);
        assert(kgsl_sharedmem_free(&global_md) == -EBUSY && !backing_frees);
        int expected = 0;
        if (scenario == 1) { unmap_result = PAGE_SIZE; expected = -EUCLEAN; }
        if (scenario == 2) { drain_error = -ETIMEDOUT; expected = -ETIMEDOUT; }
        if (scenario == 3) { drain_error = -EACCES; expected = -EACCES; }
        if (scenario == 4) { drain_error = -EOPNOTSUPP; expected = -EOPNOTSUPP; }
        assert(a6xx_hwsched_free_gmu_imports(&adreno) == expected);
        assert(unmap_calls == 1 && drain_calls == (scenario != 1));
        if (expected) {
            assert(test_hfi.mem_alloc_entries == 1 && test_hfi.mem_alloc_table[0].gpu_md == &global_md);
            assert(global_md.priv & KGSL_MEMDESC_GMU_MAPPED);
            assert(global_md.priv & KGSL_MEMDESC_MAP_UNCERTAIN);
            assert(kgsl_sharedmem_free(&global_md) == -EUCLEAN && !backing_frees);
            assert(a6xx_hwsched_free_gmu_imports(&adreno) == expected && unmap_calls == 1);
        } else {
            assert(!(global_md.priv & KGSL_MEMDESC_GMU_MAPPED) && !test_hfi.mem_alloc_entries);
            assert(!kgsl_sharedmem_free(&global_md) && backing_frees == 1);
            assert(!a6xx_hwsched_free_gmu_imports(&adreno) && unmap_calls == 1);
        }
        cases++;
    }
    reset(); gmu.power_error = -ETIMEDOUT;
    assert(a6xx_gmu_map_sg(&gmu, 0x60000000, &table, sizeof(backing), 0) == -ETIMEDOUT);
    assert(PTR_ERR(reserve_gmu_kernel_block(&gmu, 0, sizeof(backing), 1)) == -ETIMEDOUT);
    assert(!map_calls && !alloc_calls); cases++;
    return cases;
}
int main(void)
{
    unsigned cases = import_ownership();
    for (int native = 0; native < 2; ++native) {
        for (int scenario = 0; scenario < 9; ++scenario) {
            struct mem_alloc_entry entry = reset();
            int expected = 0;
            u32 flags = MEMFLAG_GMU_PRIV | MEMFLAG_GMU_WRITEABLE;
            if (native) entry.gpu_md->pages = segments;
            if (scenario == 1) { map_result = -ENOMEM; expected = -ENOMEM; }
            if (scenario == 2) { map_result = -EIO; expected = -EIO; }
            if (scenario == 3) { map_result = 0; expected = -EIO; }
            if (scenario == 4) { map_result = PAGE_SIZE; expected = -EIO; }
            if (scenario == 5) { map_result = PAGE_SIZE * 3; expected = -EIO; }
            if (scenario == 6) flags |= MEMFLAG_GMU_CACHEABLE;
            if (scenario == 7) flags = 0;
            if (scenario == 8) { segments[1].length = PAGE_SIZE * 2; expected = -EINVAL; }
            assert(gmu_import_buffer(&adreno, &entry, flags) == expected);
            unsigned index = scenario == 6 ? GMU_CACHE : GMU_NONCACHED_KERNEL;
            u32 start = gmu.vma[index].start;
            assert(entry.desc.gmu_addr == (scenario == 8 ? 0xdeadbeef : start));
            assert(gmu.vma[index].next_va == start + (scenario == 8 ? 0 : PAGE_SIZE * 2));
            bool uncertain = scenario >= 1 && scenario <= 5;
            assert(!!(entry.gpu_md->priv & KGSL_MEMDESC_MAP_UNCERTAIN) == uncertain);
            assert(gmu.power_error == (uncertain ? expected : 0));
            assert(map_calls == (scenario != 8));
            assert(temporary_allocs == native && temporary_frees == native);
            assert(!unmap_calls);
            if (map_calls) assert(map_attrs == (scenario == 7 ? IOMMU_READ : 7));
            ++cases;
        }
    }
    for (int scenario = 0; scenario < 14; ++scenario) {
        struct mem_alloc_entry entry = reset();
        int expected = -EINVAL;
        if (scenario == 0) entry.gpu_md->sgt = NULL;
        if (scenario == 1) table.sgl = NULL;
        if (scenario == 2) table.orig_nents = 0;
        if (scenario == 3) segments[0].length = 0;
        if (scenario == 4) segments[0].phys++;
        if (scenario == 5) segments[0].bus = true;
        if (scenario == 6) entry.desc.size--;
        if (scenario == 7) { gmu.vma[1].next_va = 0xfffff000; expected = -ENOMEM; }
        if (scenario == 8) { gmu.vma[1].next_va--; expected = -ENOMEM; }
        if (scenario == 9) { gmu.vma[1].size = PAGE_SIZE; expected = -ENOMEM; }
        if (scenario == 10) { entry.gpu_md->pages = segments; temp_error = -EFAULT; expected = -EFAULT; }
        if (scenario == 11) gmu.domain = NULL;
        u32 flags = 0;
        if (scenario == 12) { flags = MEMFLAG_GFX_SECURE; expected = -EOPNOTSUPP; }
        if (scenario == 13) { entry.gpu_md->secure = true; expected = -EOPNOTSUPP; }
        assert(gmu_import_buffer(&adreno, &entry, flags) == expected);
        assert(!map_calls && !unmap_calls && !temporary_frees && entry.desc.gmu_addr == 0xdeadbeef);
        ++cases;
    }
    for (int scenario = 0; scenario < 12; ++scenario) {
        reset();
        u32 size = PAGE_SIZE + 1, addr = 0, id = GMU_NONCACHED_KERNEL;
        int expected = 0;
        if (scenario == 1) { alloc_error = 1; expected = -ENOMEM; }
        if (scenario == 2) { sg_error = -EFAULT; expected = -EFAULT; }
        if (scenario == 3) { map_result = -EIO; expected = -EIO; }
        if (scenario == 4) { map_result = PAGE_SIZE; expected = -EIO; }
        if (scenario == 5) { size = U32_MAX; expected = -EINVAL; }
        if (scenario == 6) { size = 0; expected = -EINVAL; }
        if (scenario == 7) { addr = 0x60000001; expected = -EINVAL; }
        if (scenario == 8) { id = GMU_MEM_TYPE_MAX; expected = -EINVAL; }
        if (scenario == 9) { gmu.global_entries = ARRAY_SIZE(gmu.gmu_globals); expected = -ENOMEM; }
        if (scenario == 10) { gmu.global_entries = 1; gmu.gmu_globals[0] =
            (struct gmu_memdesc){ .gmuaddr = 0x60001000, .size = PAGE_SIZE }; expected = -EEXIST; }
        if (scenario == 11) { addr = 0x60004000; }
        unsigned count = gmu.global_entries;
        struct gmu_memdesc *md = reserve_gmu_kernel_block(&gmu, addr, size, id);
        if (expected) {
            assert(IS_ERR(md) && PTR_ERR(md) == expected);
            bool uncertain = scenario == 3 || scenario == 4;
            assert(gmu.global_entries == count + uncertain);
            assert(gmu.vma[1].next_va == 0x60000000 + (uncertain ? sizeof(backing) : 0));
            assert(gmu.gmu_globals[count < 3 ? count : 2].hostptr == (uncertain ? backing : NULL));
            assert(gmu.power_error == (uncertain ? expected : 0));
            assert(free_calls == (scenario == 2));
            assert(sg_frees == uncertain && !unmap_calls);
            if (uncertain) {
                struct gmu_memdesc saved = gmu.gmu_globals[count];
                assert(saved.gmuaddr == 0x60000000 && saved.size == sizeof(backing));
                assert(saved.dma_handle == dma_handle);
                assert(a6xx_free_gmu_globals(&gmu) == expected);
                assert(!free_calls && !detaches && !domain_frees && !unmap_calls);
                assert(PTR_ERR(reserve_gmu_kernel_block(&gmu, 0, size, id)) == expected);
                assert(alloc_calls == 1 && map_calls == 1 && gmu.global_entries == count + 1);
                assert(!memcmp(&saved, &gmu.gmu_globals[count], sizeof(saved)));
            }
        } else {
            assert(!IS_ERR(md) && md->hostptr == backing && md->size == sizeof(backing));
            assert(md->dma_handle == dma_handle && gmu.global_entries == 1 && sg_frees == 1);
            assert(md->gmuaddr == (addr ? addr : 0x60000000));
            assert(gmu.vma[1].next_va == md->gmuaddr + md->size);
            assert(map_addr == md->gmuaddr && map_attrs == 7);
            assert(!a6xx_free_gmu_globals(&gmu));
            assert(unmap_calls == 1 && free_calls == 1 && detaches == 1 && domain_frees == 1);
            assert(!gmu.domain && !gmu.global_entries && !md->hostptr);
            assert(!a6xx_free_gmu_globals(&gmu));
            assert(free_calls == 1 && domain_frees == 1);
        }
        ++cases;
    }
    for (int scenario = 0; scenario < 6; ++scenario) {
        reset(); gmu.domain = NULL;
        int expected = 0;
        if (scenario == 1) { domain_error = -ENOMEM; expected = -ENOMEM; }
        if (scenario == 2) { attach_error = -EBUSY; expected = -EBUSY; }
        if (scenario == 3) { default_domain.type = 0; expected = -EOPNOTSUPP; }
        if (scenario == 4) { enable_error = -EOPNOTSUPP; expected = enable_error; }
        if (scenario == 5) { enable_error = -EACCES; detach_error = 1; expected = enable_error; }
        assert(a6xx_gmu_iommu_init(&gmu) == expected);
        assert(gmu.domain == ((!expected || scenario == 5) ? &domain : NULL));
        assert(domain_allocs == (scenario != 3));
        assert(attaches == (scenario == 0 || scenario == 2 || scenario >= 4));
        assert(enable_calls == (scenario == 0 || scenario >= 4));
        assert(detaches == (scenario >= 4));
        assert(gmu.power_error == (scenario == 5 ? expected : 0));
        assert(domain_frees == (scenario == 2 || scenario == 4));
        ++cases;
    }
    for (int scenario = 0; scenario < 3; ++scenario) {
        reset();
        int expected = scenario == 1 ? -ENODEV : scenario == 2 ? -517 : 0;
        qmp_get_error = expected;
        assert(a6xx_gmu_aop_qmp_init(&adreno, &gmu) == expected);
        assert(qmp_gets == 1 && adreno.acd_enabled == !expected);
        assert(gmu.qmp == (expected ? NULL : &qmp_handle));
        ++cases;
    }
    for (int scenario = 0; scenario < 6; ++scenario) {
        reset();
        struct kgsl_device device = { &adreno };
        gmu.qmp = &qmp_handle;
        int expected = 0;
        bool desired = true;
        if (scenario == 1) { qmp_send_error = -ETIMEDOUT; expected = -ETIMEDOUT; }
        if (scenario == 2) { cycle_error = -EBUSY; expected = -EBUSY; }
        if (scenario == 3) { gmu.qmp = NULL; expected = -ENODEV; }
        if (scenario == 4) adreno.acd_enabled = true;
        if (scenario == 5) { adreno.acd_enabled = true; desired = false; }
        assert(a6xx_gmu_acd_set(&device, desired) == expected);
        assert(adreno.acd_enabled == (!expected ? desired : false));
        assert(cycles == (scenario != 3 && scenario != 4));
        assert(qmp_sends == (scenario == 0 || scenario == 1 || scenario == 5));
        if (qmp_sends) assert(qmp_value == desired);
        ++cases;
    }
    reset();
    assert(!a6xx_gmu_aop_send_acd_state(&gmu, false));
    assert(a6xx_gmu_aop_send_acd_state(&gmu, true) == -ENODEV && !qmp_sends);
    ++cases;
    printf("PASS: %u GMU cases (physical SG, errno, exact map, bounds, publication, DMA cleanup, domain attach, QMP state/error propagation)\n", cases);
    return 0;
}
