/* Actual RPMh functions; fixture comes from target cmd-db, no hardware writes. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef uint32_t __le32;
typedef uint16_t __le16;
#define U32_MAX UINT32_MAX
#define GFP_KERNEL 0
#define SZ_1K 1024
#define __packed __attribute__((packed))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min_t(t, a, b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define BIT(n) (1U << (n))
#define GENMASK(h, l) ((UINT32_MAX >> (31 - (h))) & (UINT32_MAX << (l)))
static u32 u32_encode_bits(u32 value, u32 mask) { return (value << __builtin_ctz(mask)) & mask; }
#define WARN_ON(x) (!!(x))
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define CMD_DB_HW_BCM 5
#define do_div(n, base) do { assert((base) != 0); (n) /= (base); } while (0)
static u16 get_unaligned_le16(const void *p)
{ const u8 *b = p; return b[0] | ((u16)b[1] << 8); }
static u32 get_unaligned_le32(const void *p)
{ const u8 *b = p; return get_unaligned_le16(b) | ((u32)get_unaligned_le16(b + 2) << 16); }
#include "rpmh-types.h"
struct db_entry { const char *name; u32 address; size_t len; u8 bytes[64]; };
#include "rpmh-target.h"
static struct db_entry entries[ARRAY_SIZE(target_entries)];
static int aux_error, slave_error, fail_alloc, allocations, live, legacy_cnoc_calls;
static const char *aux_error_name;
static void *tracked[128];
static struct db_entry *lookup(const char *name)
{
    for (unsigned i = 0; i < ARRAY_SIZE(entries); ++i)
        if (!strcmp(entries[i].name, name)) return &entries[i];
    assert(!"Unknown resource"); return NULL;
}
static const void *cmd_db_read_aux_data(const char *name, size_t *len)
{
    if (aux_error && (!aux_error_name || !strcmp(aux_error_name, name))) return ERR_PTR(aux_error);
    struct db_entry *e = lookup(name); *len = e->len; return e->bytes;
}
static u32 cmd_db_read_addr(const char *name) { return lookup(name)->address; }
static int cmd_db_read_slave_id(const char *name) { return slave_error ? slave_error : CMD_DB_HW_BCM; }
static void *test_alloc(size_t n, size_t size)
{
    ++allocations;
    if (allocations == fail_alloc) return NULL;
    void *p = calloc(n, size); assert(p && allocations < 128); tracked[allocations] = p; ++live; return p;
}
#define kzalloc(size, flags) test_alloc(1, size)
#define kcalloc(n, size, flags) test_alloc(n, size)
static void kfree(void *p)
{
    if (!p) return;
    for (unsigned i = 0; i < ARRAY_SIZE(tracked); ++i) if (tracked[i] == p) {
        tracked[i] = NULL; --live; free(p); return;
    }
    assert(!"unowned or double free");
}
struct platform_device { int unused; };
struct kgsl_pwrctrl { u32 *ddr_table; int ddr_table_count; };
struct kgsl_device { struct platform_device *pdev; struct kgsl_pwrctrl pwrctrl; };
struct a6xx_gmu_device { struct { struct hfi_bwtable_cmd bw_table; } hfi; };
struct adreno_device { struct kgsl_device device; struct a6xx_gmu_device gmu; bool a680; };
#define KGSL_DEVICE(a) (&(a)->device)
#define to_a6xx_gmu(a) (&(a)->gmu)
#define adreno_is_a680(a) ((a)->a680)
static __attribute__((unused)) u32 *kgsl_bus_get_table(struct platform_device *p,
                                                       const char *name, int *count)
{
    ++legacy_cnoc_calls;
    *count = 0;
    return ERR_PTR(-ENOENT);
}
#include "rpmh-functions.h"
static void reset(void)
{
    assert(!live); memcpy(entries, target_entries, sizeof(entries));
    aux_error = slave_error = fail_alloc = allocations = live = legacy_cnoc_calls = 0;
    aux_error_name = NULL;
    memset(tracked, 0, sizeof(tracked));
}
int main(void)
{
    unsigned cases = 0;
    for (int scenario = 0; scenario < 9; ++scenario) {
        reset(); struct rpmh_arc_vals arc = { 123, (void *)1 };
        struct db_entry *e = lookup("gfx.lvl"); int expected = 0;
        if (scenario == 1) { aux_error = -517; expected = -517; }
        if (scenario == 2) { e->len = 0; expected = -EINVAL; }
        if (scenario == 3) { e->len = 3; expected = -EINVAL; }
        if (scenario == 4) { e->len = 514; expected = -EINVAL; }
        if (scenario == 5) { e->bytes[0] = 1; expected = -EINVAL; }
        if (scenario == 6) { e->bytes[4] = 1; expected = -EINVAL; }
        if (scenario == 7) { e->bytes[e->len - 2] = 1; expected = -EINVAL; }
        if (scenario == 8) { memset(e->bytes, 0, sizeof(e->bytes)); }
        assert(rpmh_arc_cmds(&arc, "gfx.lvl") == expected);
        if (expected) assert(!arc.num && !arc.val);
        else assert(arc.num == (scenario == 8 ? 1 : 11));
        ++cases;
    }
    reset();
    struct rpmh_arc_vals gx, cx, mx;
    assert(!rpmh_arc_cmds(&gx, "gfx.lvl") && !rpmh_arc_cmds(&cx, "cx.lvl") && !rpmh_arc_cmds(&mx, "mx.lvl"));
    u16 levels[] = {0, 48, 64, 128, 256, 416, 432}; u32 volts[ARRAY_SIZE(levels)];
    assert(!setup_volt_dependency_tbl(volts, &gx, &mx, levels, ARRAY_SIZE(levels)));
    const u32 expected_volts[] = {0, 0x00300202, 0x00400203, 0x00800204, 0x01000406, 0x01a00709, 0x01b0070a};
    assert(!memcmp(volts, expected_volts, sizeof(volts))); ++cases;
    u16 gmu_levels[] = {0, 48};
    assert(!setup_volt_dependency_tbl(volts, &cx, &mx, gmu_levels, 2));
    assert(volts[0] == 0 && volts[1] == 0x00400202); ++cases;
    u16 invalid = 500;
    assert(setup_volt_dependency_tbl(volts, &gx, &mx, &invalid, 1) == -EINVAL); ++cases;
    mx.num = 0; assert(setup_volt_dependency_tbl(volts, &gx, &mx, levels, 1) == -EINVAL); ++cases;
    for (int scenario = 0; scenario < 17; ++scenario) {
        reset(); struct bcm bcms[3]; memcpy(bcms, sc8180x_ddr_bcms, sizeof(bcms));
        u32 bw[] = {0, 500000, 1000000}; int expected = 0;
        if (scenario == 1) { aux_error = -517; expected = -517; }
        if (scenario == 2) { lookup("MC0")->len = 7; expected = -EINVAL; }
        if (scenario == 3) { lookup("MC0")->address = 0; expected = -EINVAL; }
        if (scenario == 4) { slave_error = 3; expected = -EINVAL; }
        if (scenario == 5) { memset(lookup("MC0")->bytes, 0, 4); expected = -EINVAL; }
        if (scenario == 6) { memset(lookup("MC0")->bytes + 4, 0, 2); expected = -EINVAL; }
        if (scenario == 7) { bcms[0].channels = 0; expected = -EINVAL; }
        if (scenario == 8) { bcms[0].buswidth = 0; expected = -EINVAL; }
        if (scenario == 9) { bcms[0].channels = U32_MAX; expected = -EINVAL; }
        if (scenario >= 10) { fail_alloc = scenario - 9; expected = scenario == 16 ? 0 : -ENOMEM; }
        struct rpmh_bw_votes *v = build_rpmh_bw_votes(bcms, 3, bw, 3);
        if (expected) assert(IS_ERR(v) && PTR_ERR(v) == expected);
        else {
            assert(!IS_ERR(v) && v->num_cmds == 3 && v->num_levels == 3 && v->wait_bitmask == 7);
            assert(v->addrs[0] == lookup("MC0")->address && v->addrs[1] == lookup("SH0")->address);
            /* Hand-calculated against this captured SC8180X cmd-db: 4,090,000 & 9,000,000 units. */
            assert(v->cmds[0][0] == 0x40000000 && v->cmds[0][1] == 0x40000000);
            assert(v->cmds[1][0] == 0x600003d1 && v->cmds[1][1] == 0x600001bc && v->cmds[1][2] == 0x60000008);
            assert(v->cmds[2][0] == 0x600007a3 && v->cmds[2][1] == 0x60000378);
            free_rpmh_bw_votes(v);
        }
        assert(!live); ++cases;
    }
    reset();
    struct bcm bcms[3]; memcpy(bcms, sc8180x_ddr_bcms, sizeof(bcms));
    u32 bw[] = {0, 500000, 1000000};
    struct rpmh_bw_votes *v = build_rpmh_bw_votes(bcms, 3, bw, 3); assert(!IS_ERR(v));
    struct hfi_bwtable_cmd message; memset(&message, 0xa5, sizeof(message));
    assert(!build_bw_table_cmd(&message, v, NULL));
    assert(message.bw_level_num == 3 && message.ddr_cmds_num == 3 && !message.cnoc_cmds_num);
    assert(message.hdr == CREATE_MSG_HDR(H2F_MSG_BW_VOTE_TBL, sizeof(message), HFI_MSG_CMD)); ++cases;
    struct hfi_bwtable_cmd before = message;
    v->num_levels = MAX_GX_LEVELS + 1;
    assert(build_bw_table_cmd(&message, v, NULL) == -EINVAL && !memcmp(&message, &before, sizeof(message))); ++cases;
    v->num_levels = 3; v->num_cmds = MAX_BW_CMDS + 1;
    assert(build_bw_table_cmd(&message, v, NULL) == -EINVAL); ++cases;
    v->num_cmds = 3;
    assert(build_bw_table_cmd(&message, v, v) == -EINVAL); ++cases;
    free_rpmh_bw_votes(v);
    for (int scenario = 0; scenario < 25; ++scenario) {
        reset(); struct adreno_device adreno = { .a680 = true, .device.pwrctrl = {bw, 3} };
        struct hfi_bwtable_cmd *cmd = &adreno.gmu.hfi.bw_table;
        memset(cmd, 0xa5, sizeof(*cmd));
        struct hfi_bwtable_cmd saved = *cmd;
        int expected = 0;
        if (scenario == 1) { adreno.a680 = false; expected = -EOPNOTSUPP; }
        if (scenario == 2) { aux_error = -517; expected = -517; }
        if (scenario >= 3 && scenario <= 6) {
            static const char *const names[] = {"SH2", "SN15", "SN3", "CN0"};
            aux_error = -517; aux_error_name = names[scenario - 3]; expected = -517;
        }
        if (scenario == 7) { lookup("CN0")->len = 7; expected = -EINVAL; }
        if (scenario == 8) { memset(lookup("SH2")->bytes, 0, 4); expected = -EINVAL; }
        if (scenario == 9) { lookup("CN0")->address = 0; expected = -EINVAL; }
        if (scenario == 10) { memset(lookup("CN0")->bytes + 4, 0, 2); expected = -EINVAL; }
        if (scenario >= 11 && scenario <= 22) {
            fail_alloc = scenario - 10; expected = scenario == 22 ? 0 : -ENOMEM;
        }
        if (scenario == 23) lookup("CN0")->address += 0x100;
        if (scenario == 24) { lookup("CN0")->bytes[0] = 0x10; lookup("CN0")->bytes[1] = 0x27; }
        assert(build_bw_table(&adreno) == expected);
        assert(!legacy_cnoc_calls);
        if (!expected) {
            assert(cmd->cnoc_cmds_num == 4 && cmd->cnoc_wait_bitmask == 13);
            assert(cmd->cnoc_cmd_addrs[0] == lookup("SH2")->address && cmd->cnoc_cmd_addrs[1] == lookup("SN15")->address);
            assert(cmd->cnoc_cmd_addrs[2] == lookup("SN3")->address && cmd->cnoc_cmd_addrs[3] == lookup("CN0")->address);
            const u32 off[] = {0x40000000, 0, 0x40000000, 0x40000000};
            assert(!memcmp(cmd->cnoc_cmd_data[0], off, sizeof(off)));
            /* Source CNOC ON=100 kBps: round up nonzero sub-unit requests to one. */
            assert(cmd->cnoc_cmd_data[1][0] == 0x60000001);
            assert(cmd->cnoc_cmd_data[1][1] == 0x20000001 && cmd->cnoc_cmd_data[1][2] == 0x60000001);
            assert(cmd->cnoc_cmd_data[1][3] == (scenario == 24 ? 0x60000005 : 0x60000002));
            assert(cmd->bw_level_num == 3 && cmd->ddr_cmds_num == 3);
            assert(cmd->ddr_cmd_data[1][0] == 0x600003d1 && cmd->ddr_cmd_data[1][2] == 0x60000008);
        } else assert(!memcmp(cmd, &saved, sizeof(*cmd)));
        assert(!live); ++cases;
    }
    printf("PASS: %u RPMh cases (target ARC/BCM vectors, metadata errors, allocation unwind, HFI bounds, topology)\n", cases);
    return 0;
}
