/* SPDX-License-Identifier: MIT */
#ifndef QTI_MSM_VMBIND_H
#define QTI_MSM_VMBIND_H
#include <stdbool.h>
#include <stdint.h>
#ifndef __user
#define __user
#endif
#include <drm/msm_drm.h>

/* Experimental QTI/MSM backend core, NOT a live ioctl interposer.
 * The owner serializes registry changes, retains every mapping until all GPU
 * users retire, and supplies explicit sync for imported/shared buffers.
 * Unknown residency or unresolved implicit synchronization fails closed.
 */
typedef int (*qti_vm_ioctl_fn)(void *opaque, int fd, unsigned long request, void *arg);
struct qti_vm {
    int fd;
    qti_vm_ioctl_fn ioctl_fn;
    void *opaque;
    uint64_t va_start, va_size, page_size;
    uint32_t bind_queue;
    bool enabled, ready, lost;
};
struct qti_vm_mapping {
    uint32_t handle;
    uint64_t iova, size;
};
/* Resolver succeeds only for a mapping owned/retained by this VM. */
typedef int (*qti_vm_resolve_fn)(void *registry, uint32_t handle,
                                struct qti_vm_mapping *mapping);
struct qti_vm_private_access;
/* Indexed by the original BO position. Borrowed under the registry lock
 * only, from prepare through conversion and completion publication. */
struct qti_vm_prepared_bo {
    const struct qti_vm_mapping *mapping;
    struct qti_vm_private_access *access;
};
struct qti_vm_submit {
    struct drm_msm_gem_submit request;
    struct drm_msm_gem_submit_cmd *commands;
};

int qti_vm_real_ioctl(void *opaque, int fd, unsigned long request, void *arg);
/* fd MUST be fresh: no MSM_GET_PARAM/GEM/PRIME operation before this call.
 * After enabled becomes true, failure requires discarding this FD; it cannot
 * be reverted to legacy mode. This function never closes the owner's FD.
 */
int qti_vm_init(struct qti_vm *vm, int fd, uint64_t page_size,
                qti_vm_ioctl_fn ioctl_fn, void *opaque);
/* Successful bind returns an owned sync-file FD. Caller closes it after
 * arranging GPU/map/unmap dependencies; in_fence remains caller-owned.
 */
int qti_vm_map(struct qti_vm *vm, const struct qti_vm_mapping *mapping,
               int in_fence, int *out_fence);
/* Requires a retirement fence, never just a mapping-completion fence when
 * GPU work could still reference this range. Address reuse must wait for the
 * returned unmap fence. The future allocator must enforce that invariant.
 */
int qti_vm_unmap(struct qti_vm *vm, const struct qti_vm_mapping *mapping,
                 int retirement_fence, int *out_fence);
/* Converts addresses/ABI, preserving queue, fence and syncobj fields. The
 * input must ALREADY express all waits explicitly (NO_IMPLICIT). The actual
 * QTI implicit-sync translation is a separate required integration step.
 * This function does not issue the submit or release any resource.
 */
int qti_vm_prepare_submit(const struct qti_vm *vm,
                          const struct drm_msm_gem_submit *legacy,
                          qti_vm_resolve_fn resolve, void *registry,
                          struct qti_vm_submit *out);
int qti_vm_prepare_submit_prepared(const struct qti_vm *vm,
                                   const struct drm_msm_gem_submit *legacy,
                                   const struct qti_vm_prepared_bo *bos,
                                   struct qti_vm_submit *out);
void qti_vm_submit_finish(struct qti_vm_submit *submit);
#endif
