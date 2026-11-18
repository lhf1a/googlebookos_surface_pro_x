/* SPDX-License-Identifier: MIT */
#include "qti-msm-vmbind.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

_Static_assert(sizeof(struct drm_msm_gem_submit_cmd) == 32, "MSM command ABI");
_Static_assert(sizeof(struct drm_msm_gem_submit) == 72, "MSM submit ABI");
_Static_assert(sizeof(struct drm_msm_vm_bind_op) == 40, "MSM VM_BIND op ABI");
_Static_assert(sizeof(struct drm_msm_vm_bind) == 88, "MSM VM_BIND ABI");

int qti_vm_real_ioctl(void *opaque, int fd, unsigned long request, void *arg)
{
    (void)opaque;
    return ioctl(fd, request, arg) == 0 ? 0 : -errno;
}

static int get_param(struct qti_vm *vm, uint32_t id, uint64_t *value)
{
    struct drm_msm_param p = { .pipe = MSM_PIPE_3D0, .param = id };
    int ret = vm->ioctl_fn(vm->opaque, vm->fd, DRM_IOCTL_MSM_GET_PARAM, &p);
    if (!ret) *value = p.value;
    return ret;
}

int qti_vm_init(struct qti_vm *vm, int fd, uint64_t page_size,
                qti_vm_ioctl_fn fn, void *opaque)
{
    if (!vm || !fn || fd < 0 || !page_size || (page_size & (page_size - 1)))
        return -EINVAL;
    *vm = (struct qti_vm){ .fd = fd, .page_size = page_size,
                          .ioctl_fn = fn, .opaque = opaque };
    struct drm_msm_param enable = { .pipe = MSM_PIPE_3D0,
        .param = MSM_PARAM_EN_VM_BIND, .value = 1 };
    int ret = fn(opaque, fd, DRM_IOCTL_MSM_SET_PARAM, &enable);
    if (ret) return ret;
    vm->enabled = true;
    ret = get_param(vm, MSM_PARAM_VA_START, &vm->va_start);
    if (!ret) ret = get_param(vm, MSM_PARAM_VA_SIZE, &vm->va_size);
    if (ret) return ret;
    if (!vm->va_size || vm->va_size > UINT64_MAX - vm->va_start)
        return -ERANGE;
    struct drm_msm_submitqueue q = { .flags = MSM_SUBMITQUEUE_VM_BIND };
    ret = fn(opaque, fd, DRM_IOCTL_MSM_SUBMITQUEUE_NEW, &q);
    if (ret) return ret;
    vm->bind_queue = q.id;
    vm->ready = true;
    return 0;
}

static bool valid_mapping(const struct qti_vm *vm, const struct qti_vm_mapping *m)
{
    return m && m->handle && m->size && m->iova &&
        !(m->iova % vm->page_size) && !(m->size % vm->page_size) &&
        m->iova >= vm->va_start && m->size <= vm->va_size &&
        m->iova - vm->va_start <= vm->va_size - m->size;
}

static int bind_range(struct qti_vm *vm, const struct qti_vm_mapping *m,
                      uint32_t op, int in_fence, int *out_fence)
{
    if (!vm || !out_fence) return -EINVAL;
    *out_fence = -1;
    if (!vm->ready || vm->lost) return -EPIPE;
    if (!valid_mapping(vm, m) || in_fence < -1) return -EINVAL;
    struct drm_msm_vm_bind req = {
        .flags = MSM_VM_BIND_FENCE_FD_OUT |
                 (in_fence >= 0 ? MSM_VM_BIND_FENCE_FD_IN : 0),
        .nr_ops = 1, .fence_fd = in_fence, .queue_id = vm->bind_queue,
        .op_stride = sizeof(struct drm_msm_vm_bind_op),
        .op = { .op = op, .handle = op == MSM_VM_BIND_OP_MAP ? m->handle : 0,
                .iova = m->iova, .range = m->size },
    };
    int ret = vm->ioctl_fn(vm->opaque, vm->fd, DRM_IOCTL_MSM_VM_BIND, &req);
    if (ret || req.fence_fd < 0) {
        vm->lost = true;
        return ret ? ret : -EPROTO;
    }
    *out_fence = req.fence_fd;
    return 0;
}

int qti_vm_map(struct qti_vm *vm, const struct qti_vm_mapping *mapping,
               int in_fence, int *out_fence)
{
    return bind_range(vm, mapping, MSM_VM_BIND_OP_MAP, in_fence, out_fence);
}

int qti_vm_unmap(struct qti_vm *vm, const struct qti_vm_mapping *mapping,
                 int retirement_fence, int *out_fence)
{
    if (out_fence) *out_fence = -1;
    if (retirement_fence < 0) return -EINVAL;
    return bind_range(vm, mapping, MSM_VM_BIND_OP_UNMAP, retirement_fence, out_fence);
}

static int prepare_submit(const struct qti_vm *vm,
                          const struct drm_msm_gem_submit *legacy,
                          qti_vm_resolve_fn resolve, void *registry,
                          const struct qti_vm_prepared_bo *prepared,
                          struct qti_vm_submit *out)
{
    if (!vm || !legacy || !out || (!resolve && !prepared && legacy->nr_bos)) return -EINVAL;
    *out = (struct qti_vm_submit){0};
    if (!vm->ready || vm->lost) return -EPIPE;
    if (MSM_PIPE_ID(legacy->flags) != MSM_PIPE_3D0 || legacy->pad ||
        (MSM_PIPE_FLAGS(legacy->flags) & ~MSM_SUBMIT_FLAGS)) return -EINVAL;
    /* Do not silently discard QTI's implicit BO waits while dropping its BO
     * table. A complete dma-buf/explicit-sync adapter must precede this core.
     */
    if (!(legacy->flags & MSM_SUBMIT_NO_IMPLICIT)) return -EOPNOTSUPP;
    if ((legacy->nr_bos && !legacy->bos) || (legacy->nr_cmds && !legacy->cmds))
        return -EFAULT;
    const struct drm_msm_gem_submit_bo *bos = (void *)(uintptr_t)legacy->bos;
    const struct drm_msm_gem_submit_cmd *cmds = (void *)(uintptr_t)legacy->cmds;
    for (uint32_t i = 0; i < legacy->nr_bos; ++i) {
        struct qti_vm_mapping m = {0};
        if ((bos[i].flags & ~MSM_SUBMIT_BO_FLAGS) ||
            !(bos[i].flags & (MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE))) return -EINVAL;
        int ret = 0;
        if (prepared) {
            if (prepared[i].mapping) m = *prepared[i].mapping;
            else ret = -ENOENT;
        } else ret = resolve(registry, bos[i].handle, &m);
        if (ret) return ret;
        if (m.handle != bos[i].handle || !valid_mapping(vm, &m)) return -EINVAL;
    }
    if (legacy->nr_cmds && SIZE_MAX / legacy->nr_cmds < sizeof(*out->commands))
        return -EOVERFLOW;
    if (legacy->nr_cmds) {
        out->commands = calloc(legacy->nr_cmds, sizeof(*out->commands));
        if (!out->commands) return -ENOMEM;
    }
    int ret = 0;
    for (uint32_t i = 0; i < legacy->nr_cmds; ++i) {
        const struct drm_msm_gem_submit_cmd *c = &cmds[i];
        if (c->type < MSM_SUBMIT_CMD_BUF || c->type > MSM_SUBMIT_CMD_CTX_RESTORE_BUF ||
            c->submit_idx >= legacy->nr_bos || !c->size || (c->size & 3) ||
            c->nr_relocs || c->pad || (c->submit_offset & 3)) { ret = -EINVAL; break; }
        struct qti_vm_mapping m = {0};
        ret = 0;
        if (prepared) {
            if (prepared[c->submit_idx].mapping) m = *prepared[c->submit_idx].mapping;
            else ret = -ENOENT;
        } else ret = resolve(registry, bos[c->submit_idx].handle, &m);
        if (ret) break;
        if (m.handle != bos[c->submit_idx].handle || !valid_mapping(vm, &m) ||
            c->submit_offset > m.size || c->size > m.size - c->submit_offset) {
            ret = -EINVAL; break;
        }
        out->commands[i] = (struct drm_msm_gem_submit_cmd){
            .type = c->type, .size = c->size, .iova = m.iova + c->submit_offset,
        };
    }
    if (ret) { qti_vm_submit_finish(out); return ret; }
    out->request = *legacy;
    out->request.nr_bos = 0;
    out->request.bos = 0;
    out->request.cmds = (uintptr_t)out->commands;
    return 0;
}

int qti_vm_prepare_submit(const struct qti_vm *vm, const struct drm_msm_gem_submit *legacy,
                          qti_vm_resolve_fn resolve, void *registry, struct qti_vm_submit *out)
{
    if (!resolve) return -EINVAL;
    return prepare_submit(vm, legacy, resolve, registry, NULL, out);
}

int qti_vm_prepare_submit_prepared(const struct qti_vm *vm,
                                   const struct drm_msm_gem_submit *legacy,
                                   const struct qti_vm_prepared_bo *bos,
                                   struct qti_vm_submit *out)
{
    return prepare_submit(vm, legacy, NULL, NULL, bos, out);
}

void qti_vm_submit_finish(struct qti_vm_submit *submit)
{
    if (submit) { free(submit->commands); *submit = (struct qti_vm_submit){0}; }
}
