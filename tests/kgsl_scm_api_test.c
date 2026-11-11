/* Actual KGSL and matching SCM descriptor code; no real secure call is issued. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
typedef uint32_t u32;
typedef uint64_t u64;
#define U8_MAX UINT8_MAX
#define EPROBE_DEFER 517
#define GENMASK(h, l) ((~0UL << (l)) & (~0UL >> (sizeof(long) * 8 - 1 - (h))))
#define FIELD_PREP(mask, value) (((uint64_t)(value) << __builtin_ctzll(mask)) & (mask))
#define KGSL_MMU_SMMU_APERTURE 3
struct device { int unused; };
struct kgsl_device { struct device *dev; struct { unsigned long features; } mmu; };
static bool test_bit(int bit, const unsigned long *p) { return !!(*p & (1UL << bit)); }
#define dev_err(...) ((void)0)
#include "scm-private.h"
static struct device scm_dev, gpu_dev;
static struct { struct device *dev; } scm = { &scm_dev }, *__scm = &scm;
static bool ready, available;
static int checks, calls, returns[2];
static uint64_t result;
static struct qcom_scm_desc seen[2];
static bool has_result[2];
static bool qcom_scm_is_available(void) { return ready; }
static bool __qcom_scm_is_call_available(struct device *dev, u32 svc, u32 cmd)
{
    assert(ready && dev == &scm_dev && svc == 0x0c && cmd == 0x1b);
    checks++;
    return available;
}
static int qcom_scm_call(struct device *dev, const struct qcom_scm_desc *desc,
                         struct qcom_scm_res *res)
{
    assert(ready && dev == &scm_dev && calls < 2);
    seen[calls] = *desc;
    has_result[calls] = res != NULL;
    if (res)
        res->result[0] = result;
    return returns[calls++];
}
#include "scm-functions.h"
static struct kgsl_device gpu = { &gpu_dev, {1UL << KGSL_MMU_SMMU_APERTURE} };
static void reset(void)
{
    ready = available = true;
    checks = calls = 0;
    result = 0;
    memset(returns, 0, sizeof(returns));
    memset(seen, 0, sizeof(seen));
    memset(has_result, 0, sizeof(has_result));
    gpu.mmu.features = 1UL << KGSL_MMU_SMMU_APERTURE;
}
static void aperture_descriptor(int index, int bank)
{
    const struct qcom_scm_desc *d = &seen[index];
    assert(d->svc == 0x0c && d->cmd == 0x1b && d->owner == 2);
    assert(d->arginfo == 4 && !has_result[index]);
    assert(d->args[0] == (0xffff0000U | (unsigned)bank));
    for (int i = 1; i <= 3; i++) assert(d->args[i] == 0xffffffffU);
    for (int i = 4; i < MAX_QCOM_SCM_ARGS; i++) assert(d->args[i] == 0);
}
int main(void)
{
    /* Every valid bank is forwarded, including nonzero and eight-bit limits. */
    for (int bank = 0; bank <= 255; bank++) {
        reset();
        assert(set_smmu_aperture(&gpu, bank) == 0);
        assert(calls == 1 && checks == 1);
        aperture_descriptor(0, bank);
    }
    const int invalid[] = {-1, INT_MIN, 256, INT_MAX};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
        reset();
        assert(set_smmu_aperture(&gpu, invalid[i]) == -EINVAL);
        assert(!calls && !checks);
    }
    reset(); ready = false;
    assert(set_smmu_aperture(&gpu, 7) == -EPROBE_DEFER && !calls && !checks);
    reset(); available = false;
    assert(set_smmu_aperture(&gpu, 7) == -EOPNOTSUPP && !calls && checks == 1);
    reset(); ready = available = false; gpu.mmu.features = 0;
    assert(set_smmu_aperture(&gpu, -1) == 0 && !calls && !checks);
    const int errors[] = {-EIO, -EINVAL, -ETIMEDOUT, -EOPNOTSUPP};
    for (unsigned i = 0; i < sizeof(errors)/sizeof(errors[0]); i++) {
        reset(); returns[0] = errors[i];
        assert(set_smmu_aperture(&gpu, 17) == errors[i] && calls == 1);
        aperture_descriptor(0, 17);
    }
    const int retry_results[] = {0, -EBUSY, -ETIMEDOUT};
    for (unsigned i = 0; i < sizeof(retry_results)/sizeof(retry_results[0]); i++) {
        reset(); returns[0] = -EBUSY; returns[1] = retry_results[i];
        assert(set_smmu_aperture(&gpu, 37) == retry_results[i]);
        assert(calls == 2 && checks == 1);
        aperture_descriptor(0, 37); aperture_descriptor(1, 37);
    }
    /* ZAP uses the real BOOT service descriptor and both error channels. */
    const int transport[] = {0, -EIO, 0};
    const int firmware[] = {0, -EINVAL, -EACCES};
    for (unsigned i = 0; i < 3; i++) {
        reset(); returns[0] = transport[i]; result = firmware[i];
        assert(a5xx_zap_shader_resume(&gpu) == (transport[i] ?: firmware[i]));
        assert(calls == 1 && !checks && has_result[0]);
        assert(seen[0].svc == 1 && seen[0].cmd == 0x0a && seen[0].owner == 2);
        assert(seen[0].arginfo == 2 && seen[0].args[0] == 0 && seen[0].args[1] == 13);
        for (int j = 2; j < MAX_QCOM_SCM_ARGS; j++) assert(seen[0].args[j] == 0);
    }
    puts("SCM API: all 256 context banks, 4 invalid banks, availability, bounded retry, transport and ZAP result checks passed");
    return 0;
}
