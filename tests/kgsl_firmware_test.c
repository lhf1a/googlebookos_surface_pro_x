/* Execute the driver's parser and copies; DMA ownership and MMIO are models. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint64_t dma_addr_t;
#define PAGE_SIZE 4096U
#define PAGE_ALIGN(v) (((v) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define U32_MAX UINT32_MAX
#define SZ_16K 16384U
#define SZ_256K 262144U
#define SZ_16M 16777216U
#define SZ_256M 268435456U
#define SZ_512M 536870912U
#define IS_ALIGNED(v, a) (!((v) & ((a) - 1)))
#define ERR_PTR(v) ((void *)(intptr_t)(v))
#define PTR_ERR(v) ((int)(intptr_t)(v))
#define IS_ERR(v) ((uintptr_t)(v) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(v) (!(v) || IS_ERR(v))
#define le32_to_cpu(v) (v)
#define __iomem
#define dev_err(...) ((void)0)
#include "firmware-types.h"
struct firmware { size_t size; const u8 *data; };
struct device { int unused; };
struct platform_device { struct device dev; };
struct a6xx_gmu_device {
    const struct firmware *fw_image;
    struct platform_device *pdev;
    struct gmu_vma_entry vma[GMU_MEM_TYPE_MAX];
    void *domain;
    bool native_domains, cx_held, gx_held, clks_on;
    int power_error;
    struct gmu_memdesc gmu_globals[16];
    unsigned global_entries;
    bool preallocations;
};
struct adreno_a6xx_core { const char *gmufw_name; };
struct kgsl_device { struct { void *reg_virt; u32 gmu2gpu_offset; unsigned long flags; } gmu_core; };
struct adreno_device { struct a6xx_gmu_device gmu; struct kgsl_device dev; bool legacy, new_family; };
#define KGSL_DEVICE(a) (&(a)->dev)
#define to_a6xx_gmu(a) (&(a)->gmu)
#define to_a6xx_core(a) (&core)
#define adreno_is_a650_family(a) ((a)->new_family)
#define test_bit(bit, flags) ((*(flags) >> (bit)) & 1UL)
#define adreno_is_a630(a) ((a)->legacy)
#define adreno_is_a615_family(a) false
static struct adreno_a6xx_core core;
static struct adreno_device adreno;
static struct platform_device pdev;
#define vmas adreno.gmu.vma
static u8 mmio[0x30000];
static struct firmware supplied;
static unsigned cases, allocs, alloc_fail, requests, io_writes, ram_writes, barriers;
static int request_error;
static void wmb(void) { barriers++; }
static void memcpy_toio(void *dst, const void *src, size_t size)
{
    assert((uintptr_t)dst >= (uintptr_t)mmio);
    assert((uintptr_t)dst + size <= (uintptr_t)mmio + sizeof(mmio));
    io_writes++;
    memcpy(dst, src, size);
}
static void gmu_core_blkwrite(struct kgsl_device *d, u32 offset, const void *src, size_t size)
{
    memcpy_toio((u8 *)d->gmu_core.reg_virt + ((offset - d->gmu_core.gmu2gpu_offset) << 2), src, size);
}
static int request_firmware(const struct firmware **fw, const char *name, struct device *dev)
{
    assert(dev == &pdev.dev && !strcmp(name, "qcom/a680_gmu.bin"));
    requests++;
    if (request_error) return request_error;
    *fw = &supplied;
    return 0;
}
static struct gmu_memdesc *reserve_gmu_kernel_block(struct a6xx_gmu_device *gmu,
                                                    u32 addr, u32 size, u32 id)
{
    u32 rounded = PAGE_ALIGN(size);
    struct gmu_memdesc *md;
    allocs++;
    if (alloc_fail == allocs) return ERR_PTR(-ENOMEM);
    assert(IS_ALIGNED(addr, PAGE_SIZE) && rounded && gmu->global_entries < 16);
    assert(addr >= vmas[id].start && (u64)addr + rounded <= (u64)vmas[id].start + vmas[id].size);
    for (unsigned i = 0; i < gmu->global_entries; i++) {
        md = &gmu->gmu_globals[i];
        if (addr < (u64)md->gmuaddr + md->size && md->gmuaddr < (u64)addr + rounded)
            return ERR_PTR(-EEXIST);
    }
    md = &gmu->gmu_globals[gmu->global_entries++];
    *md = (struct gmu_memdesc){ .hostptr = malloc(rounded), .gmuaddr = addr, .size = rounded };
    assert(md->hostptr);
    memset(md->hostptr, 0xa5, rounded);
    return md;
}
static void *observed_memcpy(void *dst, const void *src, size_t size)
{
    for (unsigned i = 0; i < adreno.gmu.global_entries; i++) {
        struct gmu_memdesc *md = &adreno.gmu.gmu_globals[i];
        if ((uintptr_t)dst >= (uintptr_t)md->hostptr &&
            (uintptr_t)dst < (uintptr_t)md->hostptr + md->size) {
            assert(size <= (uintptr_t)md->hostptr + md->size - (uintptr_t)dst);
            ram_writes++;
        }
    }
    return memcpy(dst, src, size);
}
#define memcpy observed_memcpy
#include "firmware-functions.h"
#undef memcpy
static void reset(const u8 *data, size_t size)
{
    for (unsigned i = 0; i < adreno.gmu.global_entries; i++)
        free(adreno.gmu.gmu_globals[i].hostptr);
    memset(&adreno, 0, sizeof(adreno));
    memset(vmas, 0x5a, sizeof(vmas));
    assert(!a6xx_gmu_vma_init(&adreno));
    memset(mmio, 0xa5, sizeof(mmio));
    core.gmufw_name = "qcom/a680_gmu.bin";
    supplied = (struct firmware){size, data};
    adreno.gmu.pdev = &pdev;
    adreno.dev.gmu_core.reg_virt = mmio;
    adreno.dev.gmu_core.gmu2gpu_offset = 0x1a800;
    allocs = alloc_fail = requests = io_writes = ram_writes = barriers = 0;
    request_error = 0;
}
static size_t append(u8 *out, size_t off, u32 addr, u32 size, u32 type, u32 value)
{
    struct gmu_block_header blk = {addr, size, type, value};
    memcpy(out + off, &blk, sizeof(blk));
    for (u32 i = 0; i < size; i++) out[off + sizeof(blk) + i] = (u8)(i + 17);
    return off + sizeof(blk) + size;
}
static void reject(const u8 *data, size_t size)
{
    reset(data, size);
    assert(a6xx_gmu_parse_fw(&adreno) == -EINVAL);
    assert(!allocs && !io_writes && !ram_writes && !barriers);
    assert(a6xx_gmu_load_fw(&adreno) == -EINVAL);
    assert(!allocs && !io_writes && !ram_writes && !barriers);
    cases++;
}
static void vma_ownership(void)
{
    struct adreno_device second = {0}, snapshot;
    reset(NULL, 0);
    assert(!memcmp(vmas, a6xx_gmu_vma_legacy, sizeof(vmas)));
    assert(!vmas[GMU_NONCACHED_USER].size && !vmas[GMU_NONCACHED_USER].next_va);
    assert(!a6xx_gmu_vma_init(&second));
    vmas[GMU_NONCACHED_KERNEL].next_va += 4096;
    vmas[GMU_ICACHE].next_va += 8192;
    assert(second.gmu.vma[GMU_NONCACHED_KERNEL].next_va == 0x60000000);
    assert(second.gmu.vma[GMU_ICACHE].next_va == 0x4000);
    assert(a6xx_gmu_vma_legacy[GMU_NONCACHED_KERNEL].next_va == 0x60000000);
    assert(a6xx_gmu_vma_legacy[GMU_ICACHE].next_va == 0x4000); cases++;
    second.new_family = true;
    assert(!a6xx_gmu_vma_init(&second));
    assert(!memcmp(second.gmu.vma, a6xx_gmu_vma, sizeof(second.gmu.vma)));
    assert(second.gmu.vma[GMU_DTCM].start == SZ_256M + SZ_16K);
    assert(vmas[GMU_DTCM].start == 0x40000);
    assert(!second.gmu.vma[GMU_DCACHE].size && !second.gmu.vma[GMU_NONCACHED_USER].size); cases++;
    /* No owners: a failed bind may initialize its own cursors again. */
    assert(!a6xx_gmu_vma_init(&adreno));
    assert(!memcmp(vmas, a6xx_gmu_vma_legacy, sizeof(vmas))); cases++;
    for (unsigned owner = 0; owner < 10; owner++) {
        reset(NULL, 0);
        memset(vmas, 0x5a, sizeof(vmas));
        switch (owner) {
        case 0: adreno.gmu.domain = (void *)1; break;
        case 1: adreno.gmu.global_entries = 1; break;
        case 2: adreno.gmu.fw_image = &supplied; break;
        case 3: adreno.gmu.preallocations = true; break;
        case 4: adreno.gmu.native_domains = true; break;
        case 5: adreno.gmu.cx_held = true; break;
        case 6: adreno.gmu.gx_held = true; break;
        case 7: adreno.gmu.clks_on = true; break;
        case 8: adreno.gmu.power_error = -EIO; break;
        case 9: adreno.dev.gmu_core.flags = 1UL << GMU_ENABLED; break;
        }
        memcpy(&snapshot, &adreno, sizeof(snapshot));
        assert(a6xx_gmu_vma_init(&adreno) == -EBUSY);
        assert(!memcmp(&adreno, &snapshot, sizeof(adreno))); cases++;
    }
    reset(NULL, 0);
}
static void basic(void)
{
    u8 b[256] = {0}; size_t n;
    n = append(b, 0, 0, 4, 0xefbeadde, 0xefbeadde);
    reset(b, n);
    assert(!a6xx_gmu_parse_fw(&adreno) && !a6xx_gmu_load_fw(&adreno));
    assert(!allocs && io_writes == 1 && barriers == 1 && !ram_writes && requests == 1);
    assert(!memcmp(mmio + 0x3000, b + 16, 4)); cases++;
    reject(NULL, 0);
    for (size_t i = 1; i < 16; i++) reject(b, i);
    reject(b, 19); /* Short first payload. */
    reject(b, 21); /* Trailing incomplete header: no earlier write. */
    n = append(b, 0, 0x4000, 0, 1, 4096);
    n = append(b, n, 0, 4, 0, 0); reject(b, n + 1); /* No allocation before full scan. */
    n = append(b, 0, 0, 0, 1, 16); reject(b, n); /* Metadata only. */
    n = append(b, 0, 0, 4, 0, 0); n = append(b, n, 0, 0, 2, 1); reject(b, n);
    n = append(b, 0, 0xfffffffc, 8, 0, 0); reject(b, n);
    n = append(b, 0, 0x3ffc, 8, 0, 0); reject(b, n);
    n = append(b, 0, 1, 4, 0, 0); reject(b, n);
    n = append(b, 0, 0, 4, 0, 0); ((struct gmu_block_header *)b)->size = UINT32_MAX; reject(b, n);
    n = append(b, 0, 0x4001, 0, 1, 4096); n = append(b, n, 0, 4, 0, 0); reject(b, n);
    n = append(b, 0, 0x4000, 0, 1, 0); n = append(b, n, 0, 4, 0, 0); reject(b, n);
    n = append(b, 0, 0x4000, 0, 1, UINT32_MAX); n = append(b, n, 0, 4, 0, 0); reject(b, n);
    n = append(b, 0, 0, 0, 99, UINT32_MAX); n = append(b, n, 0, 4, 0xabcdef01, 0);
    reset(b, n); assert(!a6xx_gmu_parse_fw(&adreno) && !a6xx_gmu_load_fw(&adreno)); cases++;
    /* Unaligned next header is legal; the TCM destination remains aligned. */
    n = append(b, 0, 0, 1, 0, 0); n = append(b, n, 4, 4, 0, 0);
    reset(b, n); assert(!a6xx_gmu_parse_fw(&adreno) && !a6xx_gmu_load_fw(&adreno));
    assert(io_writes == 2 && !memcmp(mmio + 0x3004, b + 33, 4)); cases++;
    /* A later non-TCM payload must be backed before the first TCM copy. */
    n = append(b, 0, 0, 4, 0, 0); n = append(b, n, 0x4000, 4, 0, 0);
    reset(b, n); assert(!a6xx_gmu_parse_fw(&adreno));
    assert(a6xx_gmu_load_fw(&adreno) == -EINVAL && !io_writes && !ram_writes); cases++;
    /* Validate the page-rounded extent as well as the requested extent. */
    n = append(b, 0, 0x4000, 0, 1, 0x1001); n = append(b, n, 0, 4, 0, 0);
    reset(b, n); vmas[GMU_ICACHE].size = 0x1001;
    assert(a6xx_gmu_parse_fw(&adreno) == -EINVAL && !allocs); cases++;
    reset(b, n); request_error = -ENOENT;
    assert(a6xx_gmu_parse_fw(&adreno) == -ENOENT && !allocs && requests == 1); cases++;
    reset(b, n); core.gmufw_name = NULL;
    assert(a6xx_gmu_parse_fw(&adreno) == -EINVAL && !requests); cases++;
    reset(b, n); assert(a6xx_gmu_load_fw(&adreno) == -EINVAL); cases++;
    reset(NULL, 1); assert(a6xx_gmu_parse_fw(&adreno) == -EINVAL); cases++;
    reset(b, n); supplied.size = 0; adreno.gmu.fw_image = &supplied;
    assert(a6xx_gmu_load_fw(&adreno) == -EINVAL && !io_writes); cases++;
    reset(b, n);
    assert(find_vma_block(&adreno.gmu, 0xfffffffc, 8) == -ENOENT);
    assert(find_vma_block(&adreno.gmu, 0, 0) == -EINVAL);
    adreno.gmu.global_entries = 1;
    adreno.gmu.gmu_globals[0] = (struct gmu_memdesc){.size=16};
    assert(!find_gmu_memdesc(&adreno.gmu, 0xfffffffc, 8));
    assert(!find_gmu_memdesc(&adreno.gmu, 0, 0)); cases++;
    reset(b, n); adreno.legacy = true;
    assert(!a6xx_gmu_parse_fw(&adreno) && !a6xx_gmu_load_fw(&adreno));
    assert(io_writes == 1 && !allocs && barriers == 1); cases++;
    reset(b, 0); adreno.legacy = true;
    assert(a6xx_gmu_parse_fw(&adreno) == -EINVAL && a6xx_gmu_load_fw(&adreno) == -EINVAL); cases++;
    reset(b, MAX_GMUFW_SIZE + 1); adreno.legacy = true;
    assert(a6xx_gmu_parse_fw(&adreno) == -EINVAL && a6xx_gmu_load_fw(&adreno) == -EINVAL); cases++;
}
static void real_image(const char *path)
{
    FILE *f = fopen(path, "rb"); long size; u8 *data;
    assert(f && !fseek(f, 0, SEEK_END) && (size = ftell(f)) > 0);
    rewind(f); data = malloc(size); assert(data && fread(data, 1, size, f) == (size_t)size); fclose(f);
    reset(data, size); assert(!a6xx_gmu_parse_fw(&adreno));
    assert(allocs == 6 && adreno.gmu.global_entries == 6 && requests == 1);
    assert(!a6xx_gmu_load_fw(&adreno) && io_writes == 5 && ram_writes == 4 && barriers == 1);
    for (size_t off = 0; off < (size_t)size;) {
        struct gmu_block_header b; memcpy(&b, data + off, sizeof(b)); off += sizeof(b);
        if (b.size) {
            void *dst;
            if (b.addr < 0x4000) dst = mmio + 0x3000 + b.addr;
            else if (b.addr >= 0x40000 && b.addr < 0x44000) dst = mmio + 0x7000 + b.addr - 0x40000;
            else {
                struct gmu_memdesc *md = find_gmu_memdesc(&adreno.gmu, b.addr, b.size);
                assert(md); dst = (u8 *)md->hostptr + b.addr - md->gmuaddr;
            }
            assert(!memcmp(dst, data + off, b.size));
        }
        off += b.size;
    }
    cases++;
    assert(!a6xx_gmu_parse_fw(&adreno) && allocs == 6 && requests == 1); cases++;
    assert(!a6xx_gmu_load_fw(&adreno) && io_writes == 10 && ram_writes == 8); cases++;
    for (unsigned fail = 1; fail <= 6; fail++) {
        reset(data, size); alloc_fail = fail;
        assert(a6xx_gmu_parse_fw(&adreno) == -ENOMEM && adreno.gmu.global_entries == fail - 1);
        assert(!io_writes && !ram_writes && !barriers);
        alloc_fail = 0;
        assert(!a6xx_gmu_parse_fw(&adreno) && adreno.gmu.global_entries == 6 && requests == 1);
        assert(!a6xx_gmu_load_fw(&adreno)); cases++;
    }
    reset(data, size); assert(!a6xx_gmu_parse_fw(&adreno));
    void *saved = adreno.gmu.gmu_globals[0].hostptr;
    adreno.gmu.gmu_globals[0].hostptr = NULL;
    assert(a6xx_gmu_load_fw(&adreno) == -EINVAL && !io_writes); cases++;
    adreno.gmu.gmu_globals[0].hostptr = ERR_PTR(-ENOMEM);
    assert(a6xx_gmu_load_fw(&adreno) == -EINVAL && !io_writes); cases++;
    adreno.gmu.gmu_globals[0].hostptr = saved;
    reset(data, size); adreno.gmu.power_error = -EUCLEAN;
    assert(a6xx_gmu_parse_fw(&adreno) == -EUCLEAN && !requests && !allocs); cases++;
    reset(data, size); assert(!a6xx_gmu_parse_fw(&adreno));
    adreno.gmu.power_error = -EUCLEAN;
    assert(a6xx_gmu_load_fw(&adreno) == -EUCLEAN && !io_writes && !ram_writes && !barriers); cases++;
    /* The last byte is missing: even the valid metadata must not allocate. */
    reject(data, size - 1);
    reset(NULL, 0); free(data);
}
int main(int argc, char **argv)
{
    vma_ownership();
    basic();
    if (argc == 2) real_image(argv[1]);
    reset(NULL, 0);
    printf("PASS: %u firmware scenarios (actual parser/load, modeled allocations/MMIO%s)\n",
           cases, argc == 2 ? ", captured A680 payload byte comparison" : "");
    return 0;
}
