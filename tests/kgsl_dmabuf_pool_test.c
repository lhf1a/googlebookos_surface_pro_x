/* Actual pool init/exit functions with shrinker and DT ownership checks. */
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define DEFAULT_SEEKS 2
struct device_node { int id; };
struct kgsl_page_pool { int parsed; };
struct shrinker { void (*count_objects)(void), (*scan_objects)(void); int seeks; };
static struct shrinker *kgsl_pool_shrinker, instance;
static void kgsl_pool_shrink_count_objects(void) {}
static void kgsl_pool_shrink_scan_objects(void) {}
static struct kgsl_page_pool kgsl_pools[2];
static int kgsl_num_pools;
static unsigned kgsl_pool_max_pages;
static struct device_node root_node, children[3];
static int present, fail_alloc, child_count, bad_child, node_puts, allocations, registered, freed, reduced;
static struct device_node *of_find_compatible_node(void *a, void *b, const char *s)
{ return present ? &root_node : NULL; }
static void of_node_put(struct device_node *n) { assert(n); ++node_puts; }
static struct shrinker *shrinker_alloc(unsigned flags, const char *fmt)
{ ++allocations; return fail_alloc ? NULL : &instance; }
static void shrinker_register(struct shrinker *s)
{
    assert(s == &instance && !registered && !freed);
    assert(s->count_objects == &kgsl_pool_shrink_count_objects && s->scan_objects == &kgsl_pool_shrink_scan_objects);
    assert(s->seeks == DEFAULT_SEEKS);
    ++registered;
}
static void shrinker_free(struct shrinker *s)
{ assert(s == &instance && registered == 1 && !freed && !reduced); --registered; ++freed; }
static void of_property_read_u32(struct device_node *n, const char *name, unsigned *v) { *v = 123; }
#define for_each_child_of_node(n, c) \
    for (int ix=0; ix<child_count && ((c)=&children[ix],1); ++ix)
static int kgsl_of_parse_mempool(struct kgsl_page_pool *p, struct device_node *n)
{
    if (n->id == bad_child) return -1;
    p->parsed = 1;
    return 0;
}
static int kgsl_pool_reduce(int count, bool all)
{ assert(count == INT_MAX && all && !registered); ++reduced; return 0; }
#include "kgsl-pool-functions.h"
static void reset(void)
{
    assert(!kgsl_pool_shrinker && !kgsl_num_pools && !registered);
    present = 1; fail_alloc = 0; child_count = 2; bad_child = -1;
    node_puts = allocations = freed = reduced = 0;
    for (int i = 0; i < 3; ++i) children[i].id = i;
}
int main(void)
{
    unsigned cases = 0;
    for (int scenario = 0; scenario < 5; ++scenario) {
        reset();
        if (scenario == 0) present = 0;
        if (scenario == 1) fail_alloc = 1;
        if (scenario == 2) child_count = 0;
        if (scenario == 3) bad_child = 0;
        if (scenario == 4) child_count = 3;
        kgsl_probe_page_pools();
        assert(allocations == (scenario != 0));
        assert(registered == (scenario >= 2));
        assert(kgsl_num_pools == (scenario == 3 ? 1 : scenario == 4 ? 2 : 0));
        assert(node_puts == (scenario == 0 ? 0 : scenario == 4 ? 2 : 1));
        kgsl_exit_page_pools();
        assert(!kgsl_pool_shrinker && !kgsl_num_pools && !registered && reduced == 1);
        assert(freed == (scenario >= 2));
        kgsl_exit_page_pools();
        assert(freed == (scenario >= 2) && reduced == 2);
        ++cases;
    }
    printf("PASS: %u pool cases (optional DT, allocation failure, registration and teardown ordering)\n", cases);
    return 0;
}
