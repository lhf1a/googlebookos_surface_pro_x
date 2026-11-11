/* Actual provider functions; PM, locking and register effects are injected. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
typedef uint64_t u64;
#define BIT(n) (1UL << (n))
#define GENMASK(h, l) ((~0UL << (l)) & (~0UL >> (63 - (h))))
#define GENMASK_ULL(h, l) GENMASK(h, l)
#define FIELD_PREP(mask, value) (((u64)(value) << __builtin_ctzll(mask)) & (mask))
#define ARM_SMMU_DOMAIN_S1 0
#define ARM_SMMU_CTX_FMT_AARCH64 3
#define QCOM_DUMMY_VAL (-1)
/* Legacy implementation logs here before returning a PM error. */
#define dev_err(dev, ...) ((void)(dev))
#define ARM_SMMU_CB(s, n) ((s)->numpage + (n))
#include "smmu-constants.h"

struct io_pgtable_cfg {
    unsigned long quirks;
    struct {
        struct { unsigned tg, sh, orgn, irgn, tsz; } tcr;
        u64 ttbr;
    } arm_lpae_s1_cfg;
};
struct io_pgtable { struct io_pgtable_cfg cfg; };
struct arm_smmu_cfg { unsigned cbndx, asid, fmt; };
struct arm_smmu_cb { u64 ttbr[2]; u32 tcr[2], mair[2]; struct arm_smmu_cfg *cfg; };
struct arm_smmu_device { void *dev; struct arm_smmu_cb *cbs; unsigned num_context_banks, numpage; };
struct arm_smmu_domain {
    struct arm_smmu_device *smmu;
    struct io_pgtable *pgtbl_ops;
    struct arm_smmu_cfg cfg;
    unsigned stage;
    int cb_lock;
};
#define io_pgtable_ops_to_pgtable(ops) (ops)
static struct arm_smmu_cb banks[8], before;
static struct arm_smmu_device smmu;
static struct arm_smmu_domain domain;
static struct io_pgtable parent;
static struct io_pgtable_cfg child;
static int pm_gets, pm_puts, refs, pm_error, locked, writes, reads, busy, debug_count, cases;
static u64 live_root;
static u32 live_context, invalidated_asid;
static char events[512];
static size_t event_count;
static void event(char c) { assert(event_count + 1 < sizeof(events)); events[event_count++] = c; }
static struct arm_smmu_cb *bank(void) { return &banks[domain.cfg.cbndx]; }
static void expect_cache_unchanged(void) { assert(!memcmp(bank(), &before, sizeof(before))); }
static int pm_runtime_resume_and_get(void *dev)
{
    assert(dev == smmu.dev && !locked && !refs);
    expect_cache_unchanged(); /* Catch mutation before resume, on success too. */
    pm_gets++;
    event('P');
    if (pm_error) return pm_error;
    refs++;
    return 0;
}
static void pm_runtime_put_autosuspend(void *dev)
{
    assert(dev == smmu.dev && !locked && refs == 1);
    refs--; pm_puts++; event('p');
}
#define spin_lock_irqsave(lock, flags) do { \
    assert((lock) == &domain.cb_lock && !locked && refs == 1); \
    (flags) = 0; locked = 1; event('L'); \
} while (0)
#define spin_unlock_irqrestore(lock, flags) do { \
    assert((lock) == &domain.cb_lock && locked && (flags) == 0); \
    locked = 0; event('l'); \
} while (0)
static void arm_smmu_write_context_bank(struct arm_smmu_device *s, unsigned idx)
{
    assert(s == &smmu && idx == domain.cfg.cbndx && refs && locked);
    live_root = bank()->ttbr[0]; writes++; event('C');
}
static void arm_smmu_cb_writeq(struct arm_smmu_device *s, unsigned idx, int reg, u64 value)
{
    assert(s == &smmu && idx == domain.cfg.cbndx && refs && locked);
    assert(reg == ARM_SMMU_CB_TTBR0);
    live_root = value; writes++; event('R');
}
static void arm_smmu_cb_write(struct arm_smmu_device *s, unsigned idx, int reg, u32 value)
{
    assert(s == &smmu && idx == domain.cfg.cbndx && refs && locked);
    if (reg == ARM_SMMU_CB_CONTEXTIDR) { live_context = value; event('I'); }
    else { assert(reg == ARM_SMMU_CB_S1_TLBIASID); invalidated_asid = value; event('A'); }
    writes++;
}
static void wmb(void) { assert(locked && refs); event('B'); }
static void arm_smmu_writel(struct arm_smmu_device *s, int page, int reg, u32 value)
{
    assert(s == &smmu && page == (int)ARM_SMMU_CB(s, domain.cfg.cbndx));
    assert(reg == ARM_SMMU_CB_TLBSYNC && value == (u32)QCOM_DUMMY_VAL);
    assert(locked && refs); writes++; event('S');
}
static u32 arm_smmu_readl(struct arm_smmu_device *s, int page, int reg)
{
    assert(s == &smmu && page == (int)ARM_SMMU_CB(s, domain.cfg.cbndx));
    assert(reg == ARM_SMMU_CB_TLBSTATUS && locked && refs);
    reads++;
    if (busy < 0) return ARM_SMMU_sTLBGSTATUS_GSACTIVE;
    if (busy) { busy--; return ARM_SMMU_sTLBGSTATUS_GSACTIVE; }
    return 0;
}
static void cpu_relax(void) { assert(locked && refs); }
static void udelay(unsigned delay) { assert(delay && locked && refs); }
static void qcom_smmu_tlb_sync_debug(struct arm_smmu_device *s)
{
    assert(s == &smmu && locked && refs); debug_count++; event('D');
}
#include "smmu-functions.h"

static void setup(bool enabled)
{
    memset(banks, 0, sizeof(banks));
    memset(&domain, 0, sizeof(domain));
    memset(&parent, 0, sizeof(parent));
    memset(&child, 0, sizeof(child));
    memset(events, 0, sizeof(events)); event_count = 0;
    smmu = (struct arm_smmu_device){ .dev = &smmu, .cbs = banks, .num_context_banks = 8, .numpage = 16 };
    domain.smmu = &smmu; domain.pgtbl_ops = &parent;
    domain.cfg = (struct arm_smmu_cfg){ .cbndx = 3, .asid = 0x123, .fmt = ARM_SMMU_CTX_FMT_AARCH64 };
    parent.cfg.quirks = IO_PGTABLE_QUIRK_ARM_TTBR1;
    parent.cfg.arm_lpae_s1_cfg.tcr.tsz = 15;
    parent.cfg.arm_lpae_s1_cfg.tcr.sh = 3;
    parent.cfg.arm_lpae_s1_cfg.tcr.irgn = parent.cfg.arm_lpae_s1_cfg.tcr.orgn = 1;
    child = parent.cfg; child.quirks = 0; child.arm_lpae_s1_cfg.ttbr = 0x12345000;
    bank()->cfg = &domain.cfg;
    bank()->tcr[0] = arm_smmu_lpae_tcr(&parent.cfg);
    bank()->tcr[1] = 0xbeef;
    bank()->ttbr[0] = FIELD_PREP(ARM_SMMU_TTBRn_ASID, domain.cfg.asid);
    bank()->ttbr[1] = 0x87654000; bank()->mair[0] = 0xaa; bank()->mair[1] = 0xbb;
    if (enabled) {
        bank()->tcr[0] |= arm_smmu_lpae_tcr(&child);
        bank()->tcr[0] &= ~(ARM_SMMU_TCR_EPD0 | ARM_SMMU_TCR_EPD1);
        bank()->ttbr[0] |= child.arm_lpae_s1_cfg.ttbr;
    }
    before = *bank();
    live_root = 0xdead000; live_context = 99; invalidated_asid = 0;
    pm_gets = pm_puts = refs = pm_error = locked = writes = reads = busy = debug_count = 0;
    cases++;
}
static void balanced(void) { assert(!refs && !locked && pm_puts == pm_gets); }
static void keep_ttbr1(void)
{
    assert(bank()->ttbr[1] == before.ttbr[1] && bank()->tcr[1] == before.tcr[1]);
    assert(!memcmp(bank()->mair, before.mair, sizeof(before.mair)));
}
int main(void)
{
    const int errors[] = {-EIO, -ENOMEM, -ETIMEDOUT};
    u32 cb, asid;
    for (unsigned i = 0; i < sizeof(errors)/sizeof(errors[0]); ++i) {
        for (int enabled = 0; enabled < 2; ++enabled) {
            setup(enabled); pm_error = errors[i];
            assert(qcom_adreno_smmu_set_ttbr0_cfg(&domain, enabled ? NULL : &child) == errors[i]);
            expect_cache_unchanged(); assert(!writes && !pm_puts && !locked && !refs && pm_gets == 1);
        }
        setup(true); pm_error = errors[i];
        assert(qcom_adreno_smmu_reset_ttbr0(&domain) == errors[i]);
        expect_cache_unchanged(); assert(!writes && !pm_puts && !locked && !refs && pm_gets == 1);
    }
    setup(false);
    assert(qcom_adreno_smmu_set_ttbr0_cfg(&domain, &child) == 0);
    assert(bank()->ttbr[0] == (0x123ULL << 48 | 0x12345000));
    assert(!(bank()->tcr[0] & (ARM_SMMU_TCR_EPD0 | ARM_SMMU_TCR_EPD1)));
    assert(live_root == bank()->ttbr[0] && writes == 1 && !strcmp(events, "PLClp"));
    keep_ttbr1(); balanced();
    setup(true);
    assert(qcom_adreno_smmu_set_ttbr0_cfg(&domain, NULL) == 0);
    assert(bank()->ttbr[0] == 0x123ULL << 48);
    assert(bank()->tcr[0] == arm_smmu_lpae_tcr(&parent.cfg));
    keep_ttbr1(); balanced();
    for (int mode = 0; mode < 3; ++mode) {
        setup(mode == 0);
        if (mode == 2) bank()->tcr[0] |= ARM_SMMU_TCR_EPD1;
        before = *bank();
        assert(qcom_adreno_smmu_set_ttbr0_cfg(&domain, mode == 1 ? NULL : &child) == -EINVAL);
        expect_cache_unchanged(); assert(!writes); balanced();
    }
    setup(false); cb = asid = 0;
    assert(qcom_adreno_smmu_get_context_bank(&domain, &cb, &asid) == 0);
    assert(cb == 3 && asid == 0x123 && !pm_gets && !writes);
    for (int invalid = 0; invalid < 8; ++invalid) {
        setup(true); cb = asid = 0xfefefefe;
        if (invalid == 0) domain.smmu = NULL;
        if (invalid == 1) domain.pgtbl_ops = NULL;
        if (invalid == 2) domain.stage = 1;
        if (invalid == 3) domain.cfg.fmt = 1;
        if (invalid == 4) domain.cfg.cbndx = 8;
        if (invalid == 5) bank()->cfg = NULL;
        if (invalid == 6) bank()->tcr[0] |= ARM_SMMU_TCR_EPD1;
        const void *cookie = invalid == 7 ? NULL : &domain;
        assert(qcom_adreno_smmu_get_context_bank(cookie, &cb, &asid) < 0);
        assert(cb == 0xfefefefe && asid == 0xfefefefe);
        assert(qcom_adreno_smmu_reset_ttbr0(cookie) < 0);
        assert(!pm_gets && !writes);
    }
    setup(true);
    assert(qcom_adreno_smmu_get_context_bank(&domain, NULL, &asid) == -EINVAL);
    assert(qcom_adreno_smmu_get_context_bank(&domain, &cb, NULL) == -EINVAL);
    for (int enabled = 0; enabled < 2; ++enabled) {
        setup(enabled); busy = 23;
        assert(qcom_adreno_smmu_reset_ttbr0(&domain) == 0);
        assert(live_root == before.ttbr[0] && live_context == 0 && invalidated_asid == 0x123);
        assert(reads == 24 && !debug_count && !strcmp(events, "PLRIBASlp"));
        expect_cache_unchanged(); balanced();
    }
    setup(true); busy = -1;
    assert(qcom_adreno_smmu_reset_ttbr0(&domain) == -ETIMEDOUT);
    assert(debug_count == 1 && reads > TLB_SPIN_COUNT);
    assert(!strcmp(events, "PLRIBASDlp")); expect_cache_unchanged(); balanced();
    /* A timeout is not success, but a later retry must be possible. */
    busy = 0; before = *bank();
    assert(qcom_adreno_smmu_reset_ttbr0(&domain) == 0); balanced();
    setup(true); refs = locked = 1;
    qcom_smmu_tlb_sync(&smmu, ARM_SMMU_CB(&smmu, 3), ARM_SMMU_CB_TLBSYNC, ARM_SMMU_CB_TLBSTATUS);
    assert(reads == 1 && !debug_count); refs = locked = 0;
    setup(true); busy = 21;
    assert(qcom_adreno_smmu_flush_tlb(&domain) == 0);
    assert(live_root == 0xdead000 && live_context == 99 && invalidated_asid == 0x123);
    assert(!strcmp(events, "PLBASlp") && reads == 22); expect_cache_unchanged(); balanced();
    setup(true); busy = -1;
    assert(qcom_adreno_smmu_flush_tlb(&domain) == -ETIMEDOUT);
    assert(live_root == 0xdead000 && live_context == 99 && debug_count == 1);
    expect_cache_unchanged(); balanced();
    setup(true); pm_error = -EIO;
    assert(qcom_adreno_smmu_flush_tlb(&domain) == -EIO);
    assert(!writes && !pm_puts && !refs && !locked); expect_cache_unchanged();
    printf("PASS: %d SMMU provider scenarios (PM/cache/metadata/restore/TLB timeout)\n", cases);
    return 0;
}
