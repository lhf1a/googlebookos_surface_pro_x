/* DDR metadata and property parser functions extracted without source rewriting. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
#define GFP_KERNEL 0
#define WARN_ON(x) (!!(x))
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
struct device_node { bool has_type; u32 type; int refs; };
struct platform_device { struct { struct device_node *of_node; } dev; };
static struct device_node nodes[3], gpu;
static int node_count, typed_error, base_error, property_count, array_error, allocations, frees;
static bool table_present, allocation_error;
static struct device_node *next_node(struct device_node *node)
{
    int index = 0;
    if (node) { assert(node->refs == 1); --node->refs; index = (int)(node - nodes) + 1; }
    if (index >= node_count) return NULL;
    ++nodes[index].refs; return &nodes[index];
}
#define for_each_node_by_type(node, type) for ((node) = next_node(NULL); (node); (node) = next_node(node))
static void of_node_put(struct device_node *node) { assert(node->refs == 1); --node->refs; }
static int of_property_read_u32(struct device_node *node, const char *name, u32 *out)
{
    if (node == &gpu) {
        if (!strcmp(name, "qcom,bus-freq-ddr7")) {
            if (typed_error) return typed_error;
            *out = 17; return 0;
        }
        assert(!strcmp(name, "qcom,bus-freq"));
        if (base_error) return base_error;
        *out = 11; return 0;
    }
    assert(!strcmp(name, "ddr_device_type"));
    if (!node->has_type) return -EINVAL;
    *out = node->type; return 0;
}
static const void *of_find_property(struct device_node *node, const char *name, void *len)
{ assert(node == &gpu); return table_present ? &gpu : NULL; }
static int of_property_count_elems_of_size(struct device_node *node, const char *name, unsigned size)
{ assert(node == &gpu && size == sizeof(u32)); return property_count; }
static int of_property_read_u32_array(struct device_node *node, const char *name, u32 *v, int count)
{
    assert(node == &gpu && count == property_count);
    if (array_error) return array_error;
    for (int i = 0; i < count; ++i) v[i] = i * 1000;
    return 0;
}
static void *kcalloc(unsigned n, unsigned size, int flags)
{ ++allocations; return allocation_error ? NULL : calloc(n, size); }
static void kfree(void *p) { assert(p); ++frees; free(p); }
#include "ddr-functions.h"
static void reset(void)
{
    for (int i = 0; i < 3; ++i) assert(!nodes[i].refs);
    memset(nodes, 0, sizeof(nodes)); node_count = 0; typed_error = base_error = array_error = 0;
    allocations = frees = 0; allocation_error = false; table_present = true; property_count = 3;
}
int main(void)
{
    unsigned cases = 0;
    for (int scenario = 0; scenario < 7; ++scenario) {
        reset(); int expected = 7;
        node_count = 2; nodes[0].has_type = nodes[1].has_type = true; nodes[0].type = nodes[1].type = 7;
        if (scenario == 1) { node_count = 0; expected = -ENOENT; }
        if (scenario == 2) { nodes[0].has_type = nodes[1].has_type = false; expected = -ENOENT; }
        if (scenario == 3) nodes[0].has_type = false;
        if (scenario == 4) { nodes[1].type = 8; expected = -EINVAL; }
        if (scenario == 5) { nodes[0].type = UINT32_MAX; expected = -EINVAL; }
        if (scenario == 6) { nodes[0].type = nodes[1].type = 0; expected = 0; }
        assert(kgsl_get_ddrtype() == expected);
        for (int i = 0; i < 3; ++i) assert(!nodes[i].refs);
        ++cases;
    }
    for (int scenario = 0; scenario < 4; ++scenario) {
        reset(); node_count = 1; nodes[0].has_type = true; nodes[0].type = 7;
        u32 value = 0; int expected = 0;
        if (scenario == 1) typed_error = -EINVAL;
        if (scenario == 2) nodes[0].has_type = false;
        if (scenario == 3) { typed_error = -EINVAL; base_error = -ENOENT; expected = -ENOENT; }
        assert(kgsl_of_property_read_ddrtype(&gpu, "qcom,bus-freq", &value) == expected);
        assert(value == (scenario == 0 ? 17 : scenario == 3 ? 0 : 11)); ++cases;
    }
    reset(); node_count = 1; nodes[0].has_type = true; nodes[0].type = 7;
    u32 value; char base[32]; memset(base, 'x', 27); base[27] = '\0';
    assert(kgsl_of_property_read_ddrtype(&gpu, base, &value) == -ENOMEM); ++cases;
    for (int scenario = 0; scenario < 6; ++scenario) {
        reset(); int expected = 0, count = 99;
        if (scenario == 1) { table_present = false; expected = -ENOENT; }
        if (scenario == 2) { property_count = -EOVERFLOW; expected = -EOVERFLOW; }
        if (scenario == 3) { property_count = 0; expected = -EINVAL; }
        if (scenario == 4) { allocation_error = true; expected = -ENOMEM; }
        if (scenario == 5) { array_error = -EFAULT; expected = -EFAULT; }
        struct platform_device pdev = { .dev.of_node = &gpu };
        u32 *table = kgsl_bus_get_table(&pdev, "qcom,bus-table-ddr", &count);
        if (expected) { assert(IS_ERR(table) && PTR_ERR(table) == expected && !count); }
        else { assert(count == 3 && table[0] == 0 && table[2] == 2000); kfree(table); }
        assert(frees == (scenario == 0 || scenario == 5)); ++cases;
    }
    printf("PASS: %u DDR cases (memory-node enumeration, references, metadata selection, truncation, property/array failures)\n", cases);
    return 0;
}
