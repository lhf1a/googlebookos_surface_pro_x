/* SPDX-License-Identifier: MIT */
#include "qti-msm-vmbind.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct fake {
    unsigned step, calls, maps, unmaps;
    int fail_at;
    struct drm_msm_vm_bind bind;
};
static int fake_ioctl(void *opaque, int fd, unsigned long request, void *arg)
{
    struct fake *f = opaque;
    assert(fd == 7);
    f->calls++;
    if (f->fail_at && f->calls == (unsigned)f->fail_at) return -ENOMEM;
    if (f->step == 0) {
        assert(request == DRM_IOCTL_MSM_SET_PARAM);
        struct drm_msm_param *p = arg;
        assert(p->param == MSM_PARAM_EN_VM_BIND && p->value == 1);
    } else if (f->step == 1 || f->step == 2) {
        assert(request == DRM_IOCTL_MSM_GET_PARAM);
        struct drm_msm_param *p = arg;
        assert(p->param == (f->step == 1 ? MSM_PARAM_VA_START : MSM_PARAM_VA_SIZE));
        p->value = f->step == 1 ? 0x100000000ULL : 0x100000ULL;
    } else if (f->step == 3) {
        assert(request == DRM_IOCTL_MSM_SUBMITQUEUE_NEW);
        struct drm_msm_submitqueue *q = arg;
        assert(q->flags == MSM_SUBMITQUEUE_VM_BIND);
        q->id = 9;
    } else {
        assert(request == DRM_IOCTL_MSM_VM_BIND);
        struct drm_msm_vm_bind *b = arg;
        f->bind = *b;
        assert(b->queue_id == 9 && b->nr_ops == 1 && b->op_stride == 40);
        assert(b->flags & MSM_VM_BIND_FENCE_FD_OUT);
        if (b->op.op == MSM_VM_BIND_OP_MAP) f->maps++;
        else { assert(b->op.op == MSM_VM_BIND_OP_UNMAP && !b->op.handle); f->unmaps++; }
        b->fence_fd = 100 + f->step;
    }
    f->step++;
    return 0;
}
static int resolve(void *unused, uint32_t handle, struct qti_vm_mapping *m)
{
    (void)unused;
    if (handle != 10 && handle != 20) return -ENOENT;
    *m = (struct qti_vm_mapping){handle, 0x100000000ULL + handle * 4096, 4096};
    return 0;
}
int main(void)
{
    struct fake f = {0}; struct qti_vm vm;
    assert(qti_vm_init(&vm, 7, 4096, fake_ioctl, &f) == 0);
    assert(vm.ready && vm.enabled && !vm.lost && f.step == 4);
    struct qti_vm_mapping map; assert(!resolve(NULL, 10, &map));
    int fence = -1;
    assert(!qti_vm_map(&vm, &map, -1, &fence) && fence == 104);
    assert(f.bind.op.handle == 10 && f.bind.op.iova == map.iova && !f.bind.op.obj_offset);
    assert(f.bind.flags == MSM_VM_BIND_FENCE_FD_OUT);
    assert(qti_vm_unmap(&vm, &map, -1, &fence) == -EINVAL && fence == -1);
    assert(!qti_vm_unmap(&vm, &map, 50, &fence));
    assert(f.bind.fence_fd == 50 && (f.bind.flags & MSM_VM_BIND_FENCE_FD_IN));
    struct qti_vm_mapping invalid = map; invalid.iova++;
    unsigned calls = f.calls;
    assert(qti_vm_map(&vm, &invalid, -1, &fence) == -EINVAL && f.calls == calls);
    invalid = map; invalid.iova = UINT64_MAX - 4095;
    assert(qti_vm_map(&vm, &invalid, -1, &fence) == -EINVAL);
    struct drm_msm_gem_submit_bo bos[] = {{3,10,0},{3,20,0}};
    struct drm_msm_gem_submit_cmd cmds[] = {
        {.type=MSM_SUBMIT_CMD_BUF,.submit_idx=0,.submit_offset=16,.size=32},
        {.type=MSM_SUBMIT_CMD_CTX_RESTORE_BUF,.submit_idx=1,.size=16},
    };
    struct drm_msm_syncobj ins[] = {{33,0,123}}, outs[] = {{34,0,124}};
    struct drm_msm_gem_submit old = {
        .flags=MSM_PIPE_3D0|MSM_SUBMIT_NO_IMPLICIT|MSM_SUBMIT_FENCE_FD_IN|
               MSM_SUBMIT_FENCE_FD_OUT|MSM_SUBMIT_SYNCOBJ_IN|MSM_SUBMIT_SYNCOBJ_OUT,
        .fence=42,.fence_fd=51,.queueid=3,.nr_bos=2,.nr_cmds=2,
        .bos=(uintptr_t)bos,.cmds=(uintptr_t)cmds,
        .in_syncobjs=(uintptr_t)ins,.out_syncobjs=(uintptr_t)outs,
        .nr_in_syncobjs=1,.nr_out_syncobjs=1,.syncobj_stride=16,
    };
    struct qti_vm_submit next;
    assert(!qti_vm_prepare_submit(&vm,&old,resolve,NULL,&next));
    assert(!next.request.nr_bos && !next.request.bos && next.request.nr_cmds==2);
    assert(next.commands[0].iova==map.iova+16 && next.commands[0].size==32);
    assert(!next.commands[0].submit_idx && !next.commands[0].submit_offset && !next.commands[0].nr_relocs);
    assert(next.commands[1].type==MSM_SUBMIT_CMD_CTX_RESTORE_BUF);
    struct drm_msm_gem_submit preserved = next.request;
    preserved.nr_bos=old.nr_bos;preserved.bos=old.bos;preserved.cmds=old.cmds;
    assert(!memcmp(&preserved,&old,sizeof(old)));
    qti_vm_submit_finish(&next);
    old.flags &= ~MSM_SUBMIT_NO_IMPLICIT;
    assert(qti_vm_prepare_submit(&vm,&old,resolve,NULL,&next)==-EOPNOTSUPP);
    old.flags |= MSM_SUBMIT_NO_IMPLICIT;
    cmds[0].nr_relocs=1;assert(qti_vm_prepare_submit(&vm,&old,resolve,NULL,&next)==-EINVAL);cmds[0].nr_relocs=0;
    cmds[0].submit_idx=2;assert(qti_vm_prepare_submit(&vm,&old,resolve,NULL,&next)==-EINVAL);cmds[0].submit_idx=0;
    cmds[0].size=4096;assert(qti_vm_prepare_submit(&vm,&old,resolve,NULL,&next)==-EINVAL);cmds[0].size=32;
    bos[1].handle=99;assert(qti_vm_prepare_submit(&vm,&old,resolve,NULL,&next)==-ENOENT);bos[1].handle=20;
    old.nr_cmds=0;old.nr_bos=0;old.cmds=0;old.bos=0;
    assert(!qti_vm_prepare_submit(&vm,&old,resolve,NULL,&next));qti_vm_submit_finish(&next);
    f.fail_at=f.calls+1;
    assert(qti_vm_map(&vm,&map,-1,&fence)==-ENOMEM && vm.lost);
    assert(qti_vm_prepare_submit(&vm,&old,resolve,NULL,&next)==-EPIPE);
    for(int i=1;i<=4;i++) {
        f=(struct fake){.fail_at=i};
        assert(qti_vm_init(&vm,7,4096,fake_ioctl,&f)==-ENOMEM);
        assert(!vm.ready && vm.enabled==(i>1));
    }
    puts("PASS init-before-query; map/unmap fences; BO-to-IOVA; queue/sync/fence preservation; unresolved-implicit/reloc/range/error rejection");
    return 0;
}
