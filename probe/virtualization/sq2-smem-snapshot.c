// SPDX-License-Identifier: GPL-2.0-only
/* Explicit lab diagnostic: bounded reads only, no allocation or remote reset. */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/soc/qcom/smem.h>

static int __init snapshot_init(void)
{
    static const struct { unsigned host, item; } items[] = {
        { QCOM_SMEM_HOST_ANY, 421 }, /* modem failure reason */
        { 1, 435 }, { 1, 428 },     /* AP/modem SMP2P */
        { 2, 443 }, { 2, 429 },     /* AP/ADSP SMP2P */
        { 5, 94 }, { 5, 432 },      /* AP/CDSP SMP2P */
    };
    unsigned i;
    if (!of_machine_is_compatible("microsoft,surface-pro-x"))
        return -ENODEV;
    for (i = 0; i < ARRAY_SIZE(items); i++) {
        size_t size = 0;
        const void *data = qcom_smem_get(items[i].host, items[i].item, &size);
        if (IS_ERR(data)) {
            pr_info("SQ2_SMEM host=%u item=%u error=%ld\n",
                    items[i].host, items[i].item, PTR_ERR(data));
            continue;
        }
        pr_info("SQ2_SMEM host=%u item=%u size=%zu (live, non-atomic)\n",
                items[i].host, items[i].item, size);
        print_hex_dump(KERN_INFO, "SQ2_SMEM ", DUMP_PREFIX_OFFSET,
                       16, 1, data, min_t(size_t, size, 128), true);
    }
    return 0;
}
static void __exit snapshot_exit(void) {}
module_init(snapshot_init);
module_exit(snapshot_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Surface lab bounded read-only SMP2P snapshot");
