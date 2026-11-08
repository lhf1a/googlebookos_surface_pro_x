// Exact-layout entry points for the verified Android libstagefright integration.
// Source implementation is patches/stagefright-p010-renderer.patch.
#include <new>
#include <list>
#include <optional>
#include <media/stagefright/foundation/AMessage.h>
#define private public
#include "SoftwareRenderer.h"
#undef private

using namespace android;

// The ELF patcher separately pins the actual library and audited call sites.
static_assert(sizeof(SoftwareRenderer) == 168);
static_assert(__builtin_offsetof(SoftwareRenderer, mNativeWindow) == 24);
static_assert(__builtin_offsetof(SoftwareRenderer, mRotationDegrees) == 68);
static_assert(__builtin_offsetof(SoftwareRenderer, mDataSpace) == 72);
static_assert(__builtin_offsetof(SoftwareRenderer, mHDRStaticInfo) == 76);
static_assert(__builtin_offsetof(SoftwareRenderer, mRenderTracker) == 104);
static_assert(sizeof(FrameRenderTracker) == 64);
static_assert(sizeof(FrameRenderTracker::Info) == 40);
static_assert(sizeof(std::list<FrameRenderTracker::Info>) == 24);

#define ENTRY extern "C" __attribute__((visibility("default")))

ENTRY void sq2_p010_renderer_ctor(
        SoftwareRenderer *storage, const sp<ANativeWindow>& window, int32_t rotation) {
    new (storage) SoftwareRenderer(window, rotation);
}

ENTRY void sq2_p010_renderer_dtor(SoftwareRenderer *renderer) {
    renderer->~SoftwareRenderer();
}

// AArch64 passes the non-trivial list return address in x8, as the original call does.
ENTRY std::list<FrameRenderTracker::Info> sq2_p010_renderer_render(
        SoftwareRenderer *renderer, const void *data, size_t size, int64_t mediaTimeUs,
        nsecs_t renderTimeNs, size_t numOutputBuffers, const sp<AMessage>& format) {
    return renderer->render(data, size, mediaTimeUs, renderTimeNs, numOutputBuffers, format);
}
