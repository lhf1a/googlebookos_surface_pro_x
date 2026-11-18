/* SPDX-License-Identifier: MIT */
#ifndef QTI_MSM_VMBIND_REGISTRY_H
#define QTI_MSM_VMBIND_REGISTRY_H
#include "qti-msm-vmbind-sync.h"

struct qti_vm_range;
struct qti_vm_bo_record;
struct qti_vm_queue_fence;
struct qti_vm_registry {
    struct qti_vm *vm;
    const struct qti_vm_sync_ops *ops;
    void *opaque;
    /* 0=pending, 1=successful signal, negative errno/fence error. */
    int (*fence_status)(void *opaque, int fd);
    struct qti_vm_range *free_ranges;
    struct qti_vm_bo_record **live;
    size_t live_count, live_capacity, retired_count;
    struct qti_cached_bo { uint32_t handle; struct qti_vm_bo_record *record; uint32_t input_flags; } *cache;
    struct qti_vm_shared_access *template_access;
    struct qti_vm_prepared_bo *template_prepared;
    size_t template_cap;
    uint32_t template_queue,template_explicit;
    uint64_t template_hits;
    bool template_valid;
    size_t cache_len, cache_cap;
    uint64_t cache_full, cache_saved;
    size_t lookup_hint; /* verified next slot for sequential BO tables */
    struct qti_vm_bo_record *retired;
    struct qti_vm_queue_fence *queues;
    int mapping_fence;
    bool ready;
};
enum qti_vm_handle_origin { QTI_VM_HANDLE_NEW, QTI_VM_HANDLE_PRIME };
int qti_vm_real_fence_status(void *opaque, int fd);

/* Caller serializes ALL registry and associated DRM operations. These APIs
 * manage one open-file-description, not merely one numeric FD. FD/dup/close
 * interception is a separate integration responsibility.
 */
int qti_vm_registry_init(struct qti_vm_registry *reg, struct qti_vm *vm,
                         const struct qti_vm_sync_ops *ops, void *opaque,
                         int (*fence_status)(void *, int));
/* PRIME may return an existing GEM handle: it must not create a second mapping
 * or invent a new GEM-handle reference. NEW must never replace a live handle.
 * dma_buf_fd may be -1; a provided descriptor is duplicated and retained.
 */
int qti_vm_registry_add(struct qti_vm_registry *reg, uint32_t handle,
                        uint64_t size, int dma_buf_fd, enum qti_vm_handle_origin origin,
                        struct qti_vm_mapping *mapping);
int qti_vm_registry_attach_dma_buf(struct qti_vm_registry *reg, uint32_t handle, int fd);
/* NEW with fd=-1 stays private. Before exposing a PRIME/name export or CPU
 * access, attach_dma_buf publishes all retained private read/write fences and
 * permanently promotes the object to shared synchronization.
 * prepare_sync/note_access surround each GPU submit under the registry lock;
 * queueid must be a non-reused logical queue identity, not a recycled kernel ID.
 */
int qti_vm_registry_prepare_sync(struct qti_vm_registry *reg,
                                 const struct drm_msm_gem_submit *request,
                                 struct qti_vm_sync_batch *batch);
/* Caller supplies request->nr_bos slots and keeps the registry lock
 * until publication or error cleanup. Do not use slots after unlock. */
int qti_vm_registry_prepare_sync_prepared(struct qti_vm_registry *reg,
                                 const struct drm_msm_gem_submit *request,
                                 struct qti_vm_sync_batch *batch,
                                 struct qti_vm_prepared_bo *prepared);
int qti_vm_registry_note_access_prepared(struct qti_vm_registry *reg,
                                const struct drm_msm_gem_submit *request,
                                int completion_fence,
                                const struct qti_vm_prepared_bo *prepared);
int qti_vm_registry_note_access(struct qti_vm_registry *reg,
                                const struct drm_msm_gem_submit *request,
                                int completion_fence);
int qti_vm_registry_resolve(void *registry, uint32_t handle, struct qti_vm_mapping *mapping);
int qti_vm_registry_dma_buf(struct qti_vm_registry *reg, uint32_t handle); /* borrowed */
int qti_vm_registry_mapping_fence(struct qti_vm_registry *reg); /* borrowed */
/* Call after EVERY successful GPU submit, before releasing the registry lock.
 * A later completion on the same MSM submitqueue covers earlier submissions.
 * Different queues remain separate and are merged on retirement. Failure
 * poisons the VM, since work is already queued and tracking cannot be lost.
 */
int qti_vm_registry_note_submit(struct qti_vm_registry *reg, uint32_t queue,
                                int completion_fence);
void qti_vm_registry_close_queue(struct qti_vm_registry *reg, uint32_t queue);
/* Retire before forwarding GEM_CLOSE. The handle immediately leaves the live
 * index so a reused kernel handle gets a new record, while its old VA stays
 * reserved until GPU retirement AND asynchronous UNMAP completion.
 */
int qti_vm_registry_retire(struct qti_vm_registry *reg, uint32_t handle);
int qti_vm_registry_collect(struct qti_vm_registry *reg); /* never CPU-waits */
/* With outstanding records, true is allowed ONLY after the last real DRM FD
 * alias has been closed. Kernel VM teardown then owns outstanding GPU work.
 */
int qti_vm_registry_finish(struct qti_vm_registry *reg, bool drm_fd_closed);
#endif
