/* SPDX-License-Identifier: GPL-2.0-only */
/* Executes the unchanged extracted prepare_fb body; fence/resv/alloc leaves modeled. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct dma_fence { int refs; bool chain; struct dma_fence *prev, *next; };
struct dma_fence_chain { struct dma_fence base; };
enum dma_resv_usage { DMA_RESV_USAGE_KERNEL, DMA_RESV_USAGE_WRITE };
struct dma_resv { struct dma_fence *kernel, *write; };
struct drm_gem_object { struct dma_resv *resv; };
struct drm_format_info { unsigned int num_planes; };
struct drm_framebuffer { struct drm_format_info *format; struct drm_gem_object *obj[3]; };
struct drm_plane_state { struct drm_framebuffer *fb; struct dma_fence *fence; };
struct drm_plane { int unused; };

static int live, scenarios, failures, lookups, singleton_fail_at, chain_calls, chain_fail_at;
static enum dma_resv_usage queried[3];
static const char *scenario;
#define MUST(x) do { if (!(x)) { fprintf(stderr, "MODEL invariant %s line %d: %s\n", scenario, __LINE__, #x); abort(); } } while (0)
#define EXPECT(x) do { if (!(x)) { fprintf(stderr, "FAIL %s line %d: %s\n", scenario, __LINE__, #x); failures++; } } while (0)

static struct dma_fence *new_fence(void)
{
    struct dma_fence *f = calloc(1, sizeof(*f));
    MUST(f); f->refs = 1; live++; return f;
}
static struct dma_fence *dma_fence_get(struct dma_fence *f)
{
    if (f) { MUST(f->refs > 0); f->refs++; } return f;
}
static void dma_fence_put(struct dma_fence *f)
{
    if (!f) return;
    MUST(f->refs > 0);
    if (!--f->refs) {
        if (f->chain) { dma_fence_put(f->prev); dma_fence_put(f->next); }
        live--; free(f);
    }
}
static struct dma_fence_chain *dma_fence_chain_alloc(void)
{
    chain_calls++;
    if (chain_calls == chain_fail_at) return NULL;
    struct dma_fence_chain *c = calloc(1, sizeof(*c)); MUST(c); return c;
}
static void dma_fence_chain_init(struct dma_fence_chain *c, struct dma_fence *prev,
                                 struct dma_fence *next, int seq)
{
    MUST(prev && next && seq == 1);
    /* The real API transfers both owned references; it does not get them again. */
    c->base = (struct dma_fence){ 1, true, prev, next }; live++;
}
static int dma_resv_get_singleton(struct dma_resv *r, enum dma_resv_usage usage,
                                  struct dma_fence **out)
{
    MUST(lookups < 3); queried[lookups++] = usage;
    if (lookups == singleton_fail_at) return -EIO;
    *out = dma_fence_get(usage == DMA_RESV_USAGE_KERNEL ? r->kernel : r->write);
    return 0;
}
static struct drm_gem_object *drm_gem_fb_get_obj(struct drm_framebuffer *fb, size_t i)
{
    MUST(i < fb->format->num_planes && i < 3); return fb->obj[i];
}
#include "actual-function.h"

struct fixture {
    struct dma_resv resv[3]; struct drm_gem_object objs[3];
    struct drm_format_info format; struct drm_framebuffer fb;
    struct drm_plane plane; struct drm_plane_state state;
    struct dma_fence *original;
};
static void reset(const char *name)
{
    scenario = name; scenarios++; lookups = singleton_fail_at = chain_calls = chain_fail_at = 0;
    memset(queried, 0, sizeof(queried));
}
static void init_fixture(struct fixture *f, bool explicit, unsigned int planes, unsigned int null_mask)
{
    memset(f, 0, sizeof(*f)); f->format.num_planes = planes; f->fb.format = &f->format;
    for (int i = 0; i < 3; i++) {
        f->resv[i] = (struct dma_resv){ new_fence(), new_fence() };
        f->objs[i].resv = &f->resv[i]; f->fb.obj[i] = &f->objs[i];
        if (null_mask & (1U << i)) {
            struct dma_fence **slot = explicit ? &f->resv[i].kernel : &f->resv[i].write;
            dma_fence_put(*slot); *slot = NULL;
        }
    }
    f->state.fb = &f->fb;
    f->state.fence = f->original = explicit ? new_fence() : NULL;
}
static void cleanup_fixture(struct fixture *f, int initial_live)
{
    dma_fence_put(f->state.fence); f->state.fence = NULL; f->original = NULL;
    for (int i = 0; i < 3; i++) {
        if (f->resv[i].kernel) EXPECT(f->resv[i].kernel->refs == 1);
        if (f->resv[i].write) EXPECT(f->resv[i].write->refs == 1);
        dma_fence_put(f->resv[i].kernel); dma_fence_put(f->resv[i].write);
        f->resv[i] = (struct dma_resv){ 0 };
    }
    EXPECT(live == initial_live);
}
static void assert_usage(bool explicit)
{
    for (int i = 0; i < lookups; i++) EXPECT(queried[i] == (explicit ? DMA_RESV_USAGE_KERNEL : DMA_RESV_USAGE_WRITE));
}
static void no_fb(bool explicit)
{
    reset(explicit ? "no-fb-explicit" : "no-fb-implicit");
    int initial = live; struct drm_plane p = { 0 };
    struct drm_plane_state state = { .fence = explicit ? new_fence() : NULL };
    struct dma_fence *original = state.fence;
    EXPECT(drm_gem_plane_helper_prepare_fb(&p, &state) == 0);
    EXPECT(state.fence == original); EXPECT(!lookups && !chain_calls);
    if (original) EXPECT(original->refs == 1);
    dma_fence_put(state.fence); state.fence = original = NULL;
    EXPECT(live == initial);
}
static void success(bool explicit, unsigned int planes, unsigned int null_mask)
{
    reset(explicit ? "success-explicit-KERNEL" : "success-implicit-WRITE");
    int initial = live; struct fixture f; init_fixture(&f, explicit, planes, null_mask);
    EXPECT(drm_gem_plane_helper_prepare_fb(&f.plane, &f.state) == 0);
    EXPECT(lookups == (int)planes); assert_usage(explicit);
    cleanup_fixture(&f, initial);
}
static void failure(bool explicit, const char *kind, int point)
{
    reset(kind); int initial = live; struct fixture f; init_fixture(&f, explicit, 3, 0);
    int expected;
    if (!strcmp(kind, "chain-explicit") || !strcmp(kind, "chain-implicit")) {
        chain_fail_at = point; expected = -ENOMEM;
    } else if (!strcmp(kind, "singleton-error")) { singleton_fail_at = point; expected = -EIO; }
    else { f.fb.obj[point - 1] = NULL; expected = -EINVAL; }
    EXPECT(drm_gem_plane_helper_prepare_fb(&f.plane, &f.state) == expected);
    EXPECT(f.state.fence == f.original);
    if (f.original) EXPECT(f.original->refs == 1);
    assert_usage(explicit); cleanup_fixture(&f, initial);
}
int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "all";
    if (!strcmp(mode, "no-fb-explicit")) no_fb(true);
    else if (!strcmp(mode, "chain-explicit")) failure(true, mode, 1);
    else if (!strcmp(mode, "chain-implicit")) failure(false, mode, 1);
    else if (!strcmp(mode, "all")) {
        no_fb(false); no_fb(true);
        for (int explicit = 0; explicit < 2; explicit++) {
            for (unsigned int n = 0; n <= 3; n++) success(explicit, n, 0);
            for (unsigned int mask = 1; mask <= 7; mask++) success(explicit, 3, mask);
            for (int point = 1; point <= 3; point++) {
                failure(explicit, "singleton-error", point);
                failure(explicit, "missing-object", point);
            }
            for (int point = 1; point <= (explicit ? 3 : 2); point++)
                failure(explicit, explicit ? "chain-explicit" : "chain-implicit", point);
        }
    } else { fprintf(stderr, "Unknown mode %s\n", mode); return 2; }
    if (failures || live) {
        fprintf(stderr, "FAIL %s: %d ownership assertions, %d leaked modeled fence(s), %d scenarios\n", mode, failures, live, scenarios);
        return 1;
    }
    printf("PASS %s: %d actual prepare_fb ownership scenarios; fence/resv/allocation leaves modeled\n", mode, scenarios);
    return 0;
}
