/* SPDX-License-Identifier: MIT */
#include "qti-msm-vmbind-sync.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/sync_file.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int real_dup(void *opaque, int fd)
{
    (void)opaque;
    int copy = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    return copy >= 0 ? copy : -errno;
}
static void real_close(void *opaque, int fd)
{
    (void)opaque;
    /* Linux releases the descriptor even when close reports EINTR. */
    close(fd);
}
const struct qti_vm_sync_ops qti_vm_sync_real_ops = {
    qti_vm_real_ioctl, real_dup, real_close,
};

static int compare_access(const void *a, const void *b)
{
    uint32_t x = ((const struct qti_vm_shared_access *)a)->handle;
    uint32_t y = ((const struct qti_vm_shared_access *)b)->handle;
    return (x > y) - (x < y);
}
static int compare_bo(const void *a, const void *b)
{
    uint32_t x = ((const struct drm_msm_gem_submit_bo *)a)->handle;
    uint32_t y = ((const struct drm_msm_gem_submit_bo *)b)->handle;
    return (x > y) - (x < y);
}

void qti_vm_sync_finish(struct qti_vm_sync_batch *b)
{
    if (!b) return;
    if (b->ops) {
        if (b->wait_fd >= 0) b->ops->close_fd(b->opaque, b->wait_fd);
        for (size_t i = 0; !b->borrowed_dma_bufs && i < b->count; ++i)
            b->ops->close_fd(b->opaque, b->accesses[i].dma_buf_fd);
    }
    free(b->accesses);
    *b = (struct qti_vm_sync_batch){ .wait_fd = -1, .original_fence = -1 };
}

/* Takes ownership of fd, including on failure. */
static int merge_owned(struct qti_vm_sync_batch *b, int fd)
{
    if (b->wait_fd < 0) { b->wait_fd = fd; return 0; }
    struct sync_merge_data merge = { .fd2 = fd, .fence = -1 };
    memcpy(merge.name, "qti-msm-vmbind", sizeof("qti-msm-vmbind"));
    int ret = b->ops->ioctl_fn(b->opaque, b->wait_fd, SYNC_IOC_MERGE, &merge);
    b->ops->close_fd(b->opaque, fd);
    if (ret) return ret;
    if (merge.fence < 0) return -EPROTO;
    b->ops->close_fd(b->opaque, b->wait_fd);
    b->wait_fd = merge.fence;
    return 0;
}

static int begin(struct qti_vm_sync_batch *b,
                      const struct qti_vm_sync_ops *ops, void *opaque,
                      const struct qti_vm_shared_access *accesses, size_t count,
                      int original_fence, int mapping_fence, bool borrowed_dma_bufs,
                      bool mixed, int private_fence, struct qti_vm_shared_access **take)
{
    if (!b || b->state != QTI_VM_SYNC_EMPTY || !ops || !ops->ioctl_fn ||
        !ops->dup_fd || !ops->close_fd || (count && !accesses) ||
        original_fence < -1 || mapping_fence < -1 || private_fence < -1) return -EINVAL;
    if (count && SIZE_MAX / count < sizeof(*accesses)) return -EOVERFLOW;
    for (size_t i = 0; i < count; ++i) {
        uint32_t allowed = DMA_BUF_SYNC_RW | (mixed ? QTI_VM_ACCESS_NO_IMPLICIT : 0);
        if (!accesses[i].handle || accesses[i].dma_buf_fd < (mixed ? -1 : 0) ||
            !(accesses[i].access & DMA_BUF_SYNC_RW) || (accesses[i].access & ~allowed))
            return -EINVAL;
    }
    *b = (struct qti_vm_sync_batch){ .ops = ops, .opaque = opaque,
        .wait_fd = -1, .original_fence = original_fence,
        .borrowed_dma_bufs = borrowed_dma_bufs, .registry_order = mixed };
    int ret = 0;
    if (take) {
        /* The registry owns a fresh heap allocation. Adopt only after all
         * preconditions; finish() now frees it on success and late errors. */
        b->accesses = *take;
        *take = NULL;
    }
    if (count) {
        if (!take) {
            b->accesses = calloc(count, sizeof(*b->accesses));
            if (!b->accesses) { qti_vm_sync_finish(b); return -ENOMEM; }
            memcpy(b->accesses, accesses, count * sizeof(*accesses));
        }
        if (!mixed) {
            bool ordered = true;
            for (size_t i = 1; i < count; ++i)
                ordered &= b->accesses[i-1].handle < b->accesses[i].handle;
            if (!ordered) qsort(b->accesses, count, sizeof(*accesses), compare_access);
            for (size_t i = 1; i < count; ++i)
                if (b->accesses[i-1].handle == b->accesses[i].handle) {
                    ret = -EINVAL; goto fail;
                }
        }
    }
    int borrowed[] = { original_fence, mapping_fence, private_fence };
    for (unsigned i = 0; i < 3; ++i) {
        if (borrowed[i] < 0) continue;
        int owned = ops->dup_fd(opaque, borrowed[i]);
        if (owned < 0) { ret = owned; goto fail; }
        ret = merge_owned(b, owned);
        if (ret) goto fail;
    }
    for (size_t i = 0; i < count; ++i) {
        if (mixed && b->accesses[i].dma_buf_fd == -1) { b->count++; continue; }
        int fd = borrowed_dma_bufs ? b->accesses[i].dma_buf_fd :
                                    ops->dup_fd(opaque, b->accesses[i].dma_buf_fd);
        if (fd < 0) { ret = fd; goto fail; }
        b->accesses[b->count++].dma_buf_fd = fd;
        if (b->accesses[i].access & QTI_VM_ACCESS_NO_IMPLICIT) continue;
        struct dma_buf_export_sync_file export = { .flags = b->accesses[i].access & DMA_BUF_SYNC_RW, .fd = -1 };
        ret = ops->ioctl_fn(opaque, fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &export);
        if (ret) goto fail;
        if (export.fd < 0) { ret = -EPROTO; goto fail; }
        ret = merge_owned(b, export.fd);
        if (ret) goto fail;
    }
    b->state = QTI_VM_SYNC_READY;
    return 0;
fail:
    qti_vm_sync_finish(b);
    return ret;
}

int qti_vm_sync_begin(struct qti_vm_sync_batch *b,
                      const struct qti_vm_sync_ops *ops, void *opaque,
                      const struct qti_vm_shared_access *accesses, size_t count,
                      int original_fence, int mapping_fence)
{
    return begin(b, ops, opaque, accesses, count, original_fence, mapping_fence, false, false, -1, NULL);
}

int qti_vm_sync_begin_borrowed(struct qti_vm_sync_batch *b,
                              const struct qti_vm_sync_ops *ops, void *opaque,
                              const struct qti_vm_shared_access *accesses, size_t count,
                              int original_fence, int mapping_fence)
{
    return begin(b, ops, opaque, accesses, count, original_fence, mapping_fence, true, false, -1, NULL);
}

int qti_vm_sync_begin_mixed(struct qti_vm_sync_batch *b,
                           const struct qti_vm_sync_ops *ops, void *opaque,
                           const struct qti_vm_shared_access *accesses, size_t count,
                           int original_fence, int mapping_fence, int private_fence)
{
    return begin(b, ops, opaque, accesses, count, original_fence, mapping_fence, true, true, private_fence, NULL);
}

int qti_vm_sync_begin_mixed_take(struct qti_vm_sync_batch *b,
                                const struct qti_vm_sync_ops *ops, void *opaque,
                                struct qti_vm_shared_access **owned, size_t count,
                                int original_fence, int mapping_fence, int private_fence)
{
    if (!owned) return -EINVAL;
    return begin(b, ops, opaque, *owned, count, original_fence, mapping_fence,
                 true, true, private_fence, owned);
}

int qti_vm_sync_prepare_request(const struct qti_vm_sync_batch *b,
                               const struct drm_msm_gem_submit *legacy,
                               struct drm_msm_gem_submit *out)
{
    if (!b || !legacy || !out || b->state != QTI_VM_SYNC_READY) return -EINVAL;
    if ((legacy->flags & MSM_SUBMIT_FENCE_FD_IN ? legacy->fence_fd : -1) != b->original_fence)
        return -EINVAL;
    if (legacy->nr_bos != b->count) return -ENODATA;
    if (legacy->nr_bos && !legacy->bos) return -EFAULT;
    const struct drm_msm_gem_submit_bo *bos = (void *)(uintptr_t)legacy->bos;
    struct drm_msm_gem_submit_bo *sorted = NULL;
    bool ordered = true;
    if (!b->registry_order)
        for (uint32_t i = 1; i < legacy->nr_bos; ++i)
            ordered &= bos[i-1].handle < bos[i].handle;
    if (!ordered) {
        if (SIZE_MAX / legacy->nr_bos < sizeof(*sorted)) return -EOVERFLOW;
        sorted = calloc(legacy->nr_bos, sizeof(*sorted));
        if (!sorted) return -ENOMEM;
        memcpy(sorted, bos, legacy->nr_bos * sizeof(*sorted));
        qsort(sorted, legacy->nr_bos, sizeof(*sorted), compare_bo);
        bos = sorted;
    }
    int ret = 0;
    for (uint32_t i = 0; i < legacy->nr_bos; ++i) {
        uint32_t needed = bos[i].flags & (MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE);
        if (!needed || (bos[i].flags & ~MSM_SUBMIT_BO_FLAGS) ||
            (i && bos[i-1].handle == bos[i].handle)) {
            ret = -EINVAL; break;
        }
        if (b->accesses[i].handle != bos[i].handle ||
            (b->accesses[i].access & needed) != needed) {
            ret = -ENODATA; break;
        }
        if ((b->accesses[i].access & QTI_VM_ACCESS_NO_IMPLICIT) &&
            !(legacy->flags & MSM_SUBMIT_NO_IMPLICIT) &&
            !(bos[i].flags & MSM_SUBMIT_BO_NO_IMPLICIT)) { ret = -EINVAL; break; }
    }
    free(sorted);
    if (ret) return ret;
    *out = *legacy;
    out->flags |= MSM_SUBMIT_NO_IMPLICIT | MSM_SUBMIT_FENCE_FD_OUT;
    if (b->wait_fd >= 0) out->flags |= MSM_SUBMIT_FENCE_FD_IN;
    else out->flags &= ~MSM_SUBMIT_FENCE_FD_IN;
    out->fence_fd = b->wait_fd;
    return 0;
}

int qti_vm_sync_publish(struct qti_vm_sync_batch *b, int completion_fence)
{
    if (!b || b->state != QTI_VM_SYNC_READY || completion_fence < 0) return -EINVAL;
    for (size_t i = 0; i < b->count; ++i) {
        if (b->accesses[i].dma_buf_fd < 0) continue; /* tracked in registry */
        struct dma_buf_import_sync_file import = {
            .flags = b->accesses[i].access & DMA_BUF_SYNC_RW, .fd = completion_fence,
        };
        int ret = b->ops->ioctl_fn(b->opaque, b->accesses[i].dma_buf_fd,
                                  DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &import);
        if (ret) { b->state = QTI_VM_SYNC_FAILED; return ret; }
    }
    b->state = QTI_VM_SYNC_PUBLISHED;
    return 0;
}
