/* SPDX-License-Identifier: GPL-2.0-only */
/* Real extracted loader; firmware requests/global allocation are host models. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
typedef uint8_t u8;
struct firmware { size_t size; const u8 *data; };
struct platform_device { int dev; };
struct kgsl_device { struct platform_device *pdev; int *dev; };
struct adreno_device { struct kgsl_device dev; };
struct kgsl_memdesc { void *hostptr; };
struct adreno_firmware {
    unsigned int *fwvirt;
    size_t size;
    unsigned int version;
    struct kgsl_memdesc *memdesc;
};
#define KGSL_DEVICE(a) (&(a)->dev)
#define KGSL_MEMFLAGS_GPUREADONLY 1
#define KGSL_MEMDESC_UCODE 2
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define PTR_ERR_OR_ZERO(p) (IS_ERR(p) ? (int)(intptr_t)(p) : 0)
#define IS_ALIGNED(x, a) (((x) & ((a) - 1)) == 0)
#define dev_err(...) ((void)0)
static inline u32 get_unaligned_le32(const void *p)
{
    const u8 *b = p;
    return b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
}
static struct firmware image;
static int request_error, allocate_error, requests, allocations, releases;
static bool null_image;
static size_t requested_size;
static struct kgsl_memdesc backing;
static int request_firmware(const struct firmware **fw, const char *name, void *dev)
{
    ++requests;
    if (request_error) return request_error;
    *fw = null_image ? NULL : &image;
    return 0;
}
static void release_firmware(const struct firmware *fw) { ++releases; }
static struct kgsl_memdesc *kgsl_allocate_global(struct kgsl_device *dev,
    size_t size, int flags, int readonly, int kind, const char *name)
{
    ++allocations;
    requested_size = size;
    if (allocate_error) return ERR_PTR(allocate_error);
    if (size > 1024 * 1024 || !size) return ERR_PTR(-ENOMEM);
    backing.hostptr = malloc(size);
    if (!backing.hostptr) return ERR_PTR(-ENOMEM);
    memset(backing.hostptr, 0xcd, size);
    return &backing;
}
#include "sqe-functions.h"

static void put_le32(u8 *p, u32 x)
{
    p[0] = x; p[1] = x >> 8; p[2] = x >> 16; p[3] = x >> 24;
}
#define REQUIRE(c) do { if (!(c)) { \
    fprintf(stderr, "case %d failed line %d: %s\n", id, __LINE__, #c); \
    free(backing.hostptr); free(storage); return 1; } } while (0)

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    int id = atoi(argv[1]), ret, expected = 0;
    bool extended = id >= 21 && id <= 41;
    size_t size = id <= 20 ? (size_t)id : extended ? (size_t)(id - 21) : 32;
    u8 header[32] = {0}, *storage, *data;
    struct platform_device pdev = {0};
    struct adreno_device device = {{&pdev, &pdev.dev}};
    struct adreno_firmware fw = {.size = 0x1234, .version = 0x76543210};
    u32 expected_version = extended ? 0x12345abc : 0x12345671;
    if (id == 42 || id == 43) {
        extended = id == 43;
        expected_version = extended ? 0x12345abc : 0x12345671;
    }
    put_le32(header, 0x01020304);
    put_le32(header + 4, extended ? 0x1234567a : 0x12345671);
    put_le32(header + 12, 0x00abc000);
    /* Short firmware allocations end exactly at the declared file boundary. */
    size_t offset = id == 42 || id == 43 ? 1 : 0;
    storage = malloc(size + offset ? size + offset : 1);
    if (!storage) return 2;
    data = storage + offset;
    memcpy(data, header, size);
    image = (struct firmware){size, data};
    if (id <= 41 && (size < 8 || size % 4 || (extended && size < 16))) expected = -EINVAL;
    if (id == 44) { image.data = NULL; expected = -EINVAL; }
    if (id == 45) { null_image = true; expected = -EINVAL; }
    if (id == 46) { request_error = -ENOENT; expected = -ENOENT; }
    if (id == 47) { allocate_error = -ENOMEM; expected = -ENOMEM; }
    if (id == 48) fw.memdesc = &backing;
    if (id == 49) fw.memdesc = ERR_PTR(-ENOMEM);
    if (id == 50) {
        if (argc != 3) { free(storage); return 2; }
        FILE *f = fopen(argv[2], "rb");
        if (!f || fseek(f, 0, SEEK_END)) { free(storage); return 2; }
        long end = ftell(f);
        if (end < 16 || end > 1024 * 1024 || fseek(f, 0, SEEK_SET)) {
            fclose(f); free(storage); return 2;
        }
        free(storage); storage = malloc((size_t)end);
        if (!storage || fread(storage, 1, (size_t)end, f) != (size_t)end) {
            fclose(f); free(storage); return 2;
        }
        fclose(f); data = storage; size = (size_t)end;
        image = (struct firmware){size, data};
        expected_version = get_unaligned_le32(data + 4);
        if ((expected_version & 0xf) == 0xa)
            expected_version = (expected_version & ~0xfff) | ((get_unaligned_le32(data + 12) & 0xfff000) >> 12);
    }
    ret = adreno_get_firmware(&device, "qcom/a680_sqe.fw", &fw);
    printf("case=%d bytes=%zu ret=%d requests=%d allocations=%d releases=%d size=%zu version=%08x\n",
           id, image.size, ret, requests, allocations, releases, fw.size, fw.version);
    REQUIRE(ret == expected);
    if (id == 48) {
        REQUIRE(requests == 0 && allocations == 0 && releases == 0);
        REQUIRE(fw.size == 0x1234 && fw.version == 0x76543210);
    } else if (expected) {
        REQUIRE(requests == 1 && releases == (id == 46 ? 0 : 1));
        REQUIRE(allocations == (id == 47 ? 1 : 0));
        REQUIRE(fw.size == 0x1234 && fw.version == 0x76543210);
        REQUIRE(fw.memdesc == (id == 47 ? ERR_PTR(-ENOMEM) : NULL));
    } else {
        REQUIRE(requests == 1 && allocations == 1 && releases == 1);
        REQUIRE(requested_size == size - 4 && fw.size == (size - 4) / 4);
        REQUIRE(fw.version == expected_version && fw.memdesc == &backing);
        REQUIRE(memcmp(backing.hostptr, data + 4, size - 4) == 0);
    }
    free(backing.hostptr); free(storage);
    return 0;
}
