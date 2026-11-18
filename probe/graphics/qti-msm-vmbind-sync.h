/* SPDX-License-Identifier: MIT */
#ifndef QTI_MSM_VMBIND_SYNC_H
#define QTI_MSM_VMBIND_SYNC_H
#include "qti-msm-vmbind.h"
#include <stddef.h>

struct qti_vm_sync_ops {
    qti_vm_ioctl_fn ioctl_fn;
    int (*dup_fd)(void *opaque, int fd); /* owned FD or negative errno */
    void (*close_fd)(void *opaque, int fd);
};
extern const struct qti_vm_sync_ops qti_vm_sync_real_ops;
#define QTI_VM_ACCESS_NO_IMPLICIT (1u << 2)
struct qti_vm_shared_access {
    uint32_t handle;      /* unique GEM handle in the current DRM FD */
    int dma_buf_fd;       /* borrowed; duplicated for this transaction */
    uint32_t access;      /* DMA_BUF_SYNC_READ / WRITE; mixed mode may add NO_IMPLICIT */
};
enum qti_vm_sync_state { QTI_VM_SYNC_EMPTY, QTI_VM_SYNC_READY,
                         QTI_VM_SYNC_PUBLISHED, QTI_VM_SYNC_FAILED };
struct qti_vm_sync_batch {
    const struct qti_vm_sync_ops *ops;
    void *opaque;
    struct qti_vm_shared_access *accesses;
    size_t count;
    int wait_fd, original_fence;
    bool borrowed_dma_bufs;
    bool registry_order; /* unique records aligned with the registry-validated BO table */
    enum qti_vm_sync_state state;
};

/* Caller must zero-initialize batch. This is an explicit-sync interop
 * transaction, NOT an atomic substitute for arbitrary implicit sharing.
 * Caller holds its resource/queue registry stable and must ensure buffer
 * ownership/acquire-release synchronization across external contexts through
 * publish. A local mutex alone does not serialize another process.
 * Original/map fences and input dma-buf FDs remain borrowed.
 */
int qti_vm_sync_begin(struct qti_vm_sync_batch *batch,
                      const struct qti_vm_sync_ops *ops, void *opaque,
                      const struct qti_vm_shared_access *accesses, size_t count,
                      int original_fence, int mapping_fence);
/* Same synchronization, without duplicating each dma-buf descriptor. The
 * caller MUST pin every input FD until finish(), including failure cleanup.
 * The GSL bridge holds its context lock and registry references throughout
 * begin/submit/publish/finish. Wait/map sync-file FDs are still duplicated.
 */
int qti_vm_sync_begin_borrowed(struct qti_vm_sync_batch *batch,
                              const struct qti_vm_sync_ops *ops, void *opaque,
                              const struct qti_vm_shared_access *accesses, size_t count,
                              int original_fence, int mapping_fence);
/* Registry-only mixed mode: fd=-1 identifies a proven private BO, whose
 * required per-resource dependencies are covered by private_fence (or absent
 * when none exist). The registry verifies unique handles and supplies records
 * in exactly the original BO order; mixed mode preserves that alignment rather
 * than sorting both tables again. All private/shared records remain mandatory.
 * Shared NO_IMPLICIT skips acquisition only; release publication is retained.
 */
int qti_vm_sync_begin_mixed(struct qti_vm_sync_batch *batch,
                           const struct qti_vm_sync_ops *ops, void *opaque,
                           const struct qti_vm_shared_access *accesses, size_t count,
                           int original_fence, int mapping_fence, int private_fence);
/* Registry-internal: the pointer is cleared iff the batch took ownership.
 * Caller frees any remaining pointer, including on early validation errors.
 * Only the array changes owners; DMA-BUF descriptors stay registry-borrowed. */
__attribute__((visibility("hidden")))
int qti_vm_sync_begin_mixed_take(struct qti_vm_sync_batch *b,
                                const struct qti_vm_sync_ops *ops, void *opaque,
                                struct qti_vm_shared_access **owned, size_t count,
                                int original_fence, int mapping_fence, int private_fence);

/* Every BO must have a matching access record: a pinned dma-buf FD or a private
 * resource whose dependencies were collected by the registry in mixed mode.
 * Missing records are rejected rather than silently dropping implicit waits.
 * An output fence is forced internally for publish/retirement. A future GSL
 * interposer must expose it only if the original caller requested one.
 */
int qti_vm_sync_prepare_request(const struct qti_vm_sync_batch *batch,
                               const struct drm_msm_gem_submit *legacy,
                               struct drm_msm_gem_submit *explicit_request);
/* completion_fence stays borrowed. On partial import failure, work may already
 * be running and some fences already published: do not retry or claim rollback.
 */
int qti_vm_sync_publish(struct qti_vm_sync_batch *batch, int completion_fence);
void qti_vm_sync_finish(struct qti_vm_sync_batch *batch);
#endif
