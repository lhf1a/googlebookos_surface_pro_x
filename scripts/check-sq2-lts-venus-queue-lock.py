#!/usr/bin/env python3
"""Compile actual Venus queue setup against the 6.18 queue-lock requirement.

This tests the reported queue-init rejection, not hardware decode or concurrency.
"""
from pathlib import Path
import subprocess,tempfile,sys
s=Path(sys.argv[1]).read_text();kernel=Path(sys.argv[2]).read_text()
a=s.index('static int m2m_queue_init(');b=s.index('\n}\n',a)+3;function=s[a:b]
guard='if (WARN_ON(!q->lock && !q->ops->wait_prepare))\n\t\treturn -EINVAL;'
assert guard in kernel
c=r'''
#include <stdio.h>
#include <errno.h>
#define V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE 1
#define V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE 2
#define VB2_MMAP 1
#define VB2_DMABUF 2
#define VB2_USERPTR 4
#define HFI_VERSION_1XX 1
#define V4L2_BUF_FLAG_TIMESTAMP_COPY 1
#define IS_SQ2_HFI(x) 1
#define WARN_ON(x) (x)
struct mutex { int value; };
struct venus_buffer { int value; };
struct resources { int hfi_version; };
struct venus_core { void *dev; struct resources *res; };
struct venus_inst { struct venus_core *core; struct mutex lock,ctx_q_lock; };
struct vb2_ops { void *wait_prepare,*wait_finish; };
static const struct vb2_ops vdec_vb2_ops={0};
static const struct vb2_ops venc_vb2_ops={0};
static int sq2_vb2_memops,vb2_dma_contig_memops;
struct vb2_queue { int type,io_modes,timestamp_flags,buf_struct_size,allow_zero_bytesused,min_queued_buffers,bidirectional;
 const struct vb2_ops *ops; void *mem_ops,*drv_priv,*dev; struct mutex *lock; };
static int vb2_queue_init(struct vb2_queue *q) { GUARD return 0; }
FUNCTION
int main(void) {
 struct resources res={.hfi_version=4};struct venus_core core={.res=&res};struct venus_inst first={.core=&core},second={.core=&core};
 struct vb2_queue src={0},dst={0},other_src={0},other_dst={0};
 int result=m2m_queue_init(&first,&src,&dst);
 int other=m2m_queue_init(&second,&other_src,&other_dst);
 printf("queue_init=%d src_lock=%d dst_lock=%d independent_instances=%d\n",result,
 src.lock==&first.ctx_q_lock,dst.lock==&first.ctx_q_lock,
 !other && src.lock && other_src.lock && src.lock!=other_src.lock);
 return 0;
}
'''.replace('GUARD',guard).replace('FUNCTION',function)
with tempfile.TemporaryDirectory(prefix='venus-queue-check-') as d:
 p=Path(d);(p/'check.c').write_text(c)
 subprocess.run(['cc','-std=c11',str(p/'check.c'),'-o',str(p/'check')],check=True)
 sys.exit(subprocess.run([str(p/'check')]).returncode)
