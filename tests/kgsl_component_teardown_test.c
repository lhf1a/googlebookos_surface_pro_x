/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Actual KGSL/component callback bodies are extracted by the companion checker.
 * Only control-flow/ownership contracts are tested. Kernel types, locking,
 * devres operations and a6xx_gmu_remove are models, not hardware execution.
 * The ordinary mode requires retention. --observe records both control and
 * error paths without turning a known unsafe result into acceptance.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

struct device { void *data; int releases; };
struct platform_device { struct device dev; };
struct kgsl_device {
    struct platform_device *pdev;
    struct { bool components_bound; } gmu_core;
};
struct component_ops { void (*unbind)(struct device *, struct device *, void *); };
struct component { bool bound; struct device *dev; const struct component_ops *ops; };
struct component_match {
    size_t num;
    struct { bool duplicate; struct component *component; } compare[1];
};
struct component_master_ops { void (*unbind)(struct device *); };
struct aggregate_device {
    bool bound;
    struct device *parent;
    struct component_match *match;
    const struct component_master_ops *ops;
};
static struct aggregate_device *current;
static int remove_error, remove_calls, dispatcher_closes, component_mutex;
static bool warn_on(bool value) { return value; }
#define WARN_ON(x) warn_on(x)
#define dev_dbg(...) ((void)0)
#define ADRENO_DEVICE(x) (x)
static void *dev_get_drvdata(struct device *dev) { return dev->data; }
static int a6xx_gmu_remove(struct kgsl_device *dev) { remove_calls++; return remove_error; }
static void adreno_hwsched_dispatcher_close(struct kgsl_device *dev) { dispatcher_closes++; }
static void devres_release_group(struct device *dev, void *group) { dev->releases++; }
static bool mutex_is_locked(int *lock) { return true; }
static struct aggregate_device *__aggregate_find(struct device *dev, void *unused) { return current; }

#include "component-teardown-functions.h"

/* Deliberately not the full adreno_unbind: isolates its component handoff. */
static void master_unbind(struct device *dev) { adreno_unbind_gmu(dev_get_drvdata(dev)); }

int main(int argc, char **argv)
{
    unsigned gaps = 0;
    bool observe = argc == 2 && !strcmp(argv[1], "--observe");

    if (argc > 1 && !observe)
        return 2;
    for (int hwsched = 0; hwsched < 2; hwsched++) {
        for (int failed = 0; failed < 2; failed++) {
            struct platform_device gpu = {0};
            struct device gmu = {0};
            struct kgsl_device kgsl = {.pdev = &gpu, .gmu_core = {.components_bound = true}};
            struct component_ops ops = {.unbind = hwsched ? a6xx_hwsched_unbind : a6xx_gmu_unbind};
            struct component c = {.bound = true, .dev = &gmu, .ops = &ops};
            struct component_match match = {.num = 1, .compare = {{.component = &c}}};
            struct component_master_ops master_ops = {.unbind = master_unbind};
            struct aggregate_device adev = {.bound = true, .parent = &gpu.dev,
                                            .match = &match, .ops = &master_ops};
            gpu.dev.data = &kgsl;
            current = &adev;
            remove_error = failed ? -EUCLEAN : 0;
            remove_calls = dispatcher_closes = 0;
            take_down_aggregate_device(&adev);
            assert(remove_calls == 1);
            assert(dispatcher_closes == (hwsched && !failed));
            if (!failed) {
                assert(!c.bound && !adev.bound && !kgsl.gmu_core.components_bound);
                assert(gmu.releases == 1 && gpu.dev.releases == 1);
            } else if (gmu.releases || gpu.dev.releases || !c.bound ||
                       !adev.bound || !kgsl.gmu_core.components_bound) {
                gaps++;
            }
            printf("%s remove=%d dispatcher_closes=%d component_devres=%d master_devres=%d "
                   "component_bound=%d aggregate_bound=%d kgsl_components_bound=%d\n",
                   hwsched ? "HWSCHED" : "GMU", remove_error, dispatcher_closes,
                   gmu.releases, gpu.dev.releases, c.bound, adev.bound,
                   kgsl.gmu_core.components_bound);
        }
    }
    printf("observed_gap_cases=%u; callback/core code actual, remove/devres/locks modeled\n", gaps);
    if (!observe && gaps) {
        fprintf(stderr, "FAIL: cleanup error did not retain component/master devres ownership\n");
        return 1;
    }
    return 0;
}
