"""Host-check the real runner's no-frame stage boundaries with wrapped syscalls."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <linux/dma-heap.h>
#include <linux/dma-buf.h>
int runner_main(int,char**);
static unsigned req,maps,closed,queue,start;
static unsigned events,gfmt,off;
static int data_mode;
static int dma_mode,allocs,dma_closed,heap_closed,fail_alloc;
static unsigned syncs[32];
int __wrap_poll(struct pollfd *p,nfds_t n,int t){(void)p;(void)n;(void)t;return 0;}
int __wrap_open(const char *p,int flags,...){(void)flags;if(!strcmp(p,"/dev/dma_heap/system")){assert(dma_mode);return 8;}assert(!strcmp(p,"/dev/test"));return 7;}
int __wrap_fsync(int fd){(void)fd;return 0;}
int __wrap_close(int fd){if(fd==8){heap_closed++;return 0;}if(fd>=100){assert(fd<100+allocs);dma_closed++;return 0;}assert(fd==7);closed++;return 0;}
void *__wrap_mmap(void *a,size_t len,int prot,int flags,int fd,off_t off){(void)a;(void)prot;(void)flags;(void)off;assert(fd==7||(dma_mode&&fd>=100));maps++;return malloc(len);}
int __wrap_munmap(void *p,size_t len){(void)len;free(p);return 0;}
int __wrap_ioctl(int fd,unsigned long op,...){va_list args;va_start(args,op);void *p=va_arg(args,void*);va_end(args);
 if(fd==8){assert(op==DMA_HEAP_IOCTL_ALLOC);if(fail_alloc&&allocs==2){errno=ENOMEM;return -1;}struct dma_heap_allocation_data *a=p;assert(a->len==8192);a->fd=100+allocs++;return 0;}
 if(fd>=100){assert(op==DMA_BUF_IOCTL_SYNC);unsigned i=fd-100;struct dma_buf_sync *s=p;assert(s->flags==(syncs[i]?DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE:DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE));syncs[i]++;return 0;}
 assert(fd==7);
 switch(op){
 case VIDIOC_QUERYCAP:strcpy((char*)((struct v4l2_capability*)p)->driver,"qcom-venus");return 0;
 case VIDIOC_SUBSCRIBE_EVENT:return 0;
 case VIDIOC_S_FMT:((struct v4l2_format*)p)->fmt.pix_mp.plane_fmt[0].sizeimage=8192;return 0;
 case VIDIOC_REQBUFS:req++;((struct v4l2_requestbuffers*)p)->count=6;return 0;
 case VIDIOC_QUERYBUF:{struct v4l2_buffer *b=p;b->length=1;b->m.planes[0].length=8192;return 0;}
 case VIDIOC_QBUF:{struct v4l2_buffer *b=p;assert(b->index==queue && b->length==1 && b->m.planes[0].bytesused>0 && b->m.planes[0].bytesused<=8192);if(dma_mode)assert(b->memory==V4L2_MEMORY_DMABUF&&b->m.planes[0].m.fd==(int)(100+queue)&&syncs[queue]==2);queue++;return 0;}
 case VIDIOC_STREAMON:assert(data_mode);start++;return 0;
 case VIDIOC_STREAMOFF:assert(data_mode);off++;return 0;
 case VIDIOC_DQEVENT:assert(data_mode);events++;if(events<3){errno=events==1?ENOENT:EAGAIN;return -1;}((struct v4l2_event*)p)->type=V4L2_EVENT_SOURCE_CHANGE;return 0;
 case VIDIOC_G_FMT:assert(data_mode);gfmt++;errno=EINVAL;return -1;
 }fprintf(stderr,"UNEXPECTED_IOCTL=%lx\n",op);abort();
}
int main(void){char *format[]={"runner","--format-only","/dev/test"};assert(runner_main(3,format)==0);assert(!req&&!maps&&closed==1&&!queue&&!start);
 req=maps=closed=queue=start=0;char *buffers[]={"runner","--buffers-only","/dev/test"};assert(runner_main(3,buffers)==0);assert(req==1&&maps==6&&closed==1&&!queue&&!start);
 req=maps=closed=queue=start=0;char *queued[]={"runner","--queue-only","/dev/test"};assert(runner_main(3,queued)==0);assert(req==1&&maps==6&&closed==1&&queue==6&&!start);
 req=maps=closed=queue=start=0;data_mode=1;char *decode[]={"runner","/dev/test"};assert(runner_main(2,decode)==1);assert(events==3&&gfmt==1&&start==1&&off==1&&closed==1);
 puts("EVENT_WAIT_PASS ENOENT/EAGAIN then SOURCE_CHANGE; deliberate G_FMT error cleaned up");
 req=maps=closed=queue=start=events=gfmt=off=0;dma_mode=1;
 char *dma[]={"runner","--dmabuf","/dev/test"};assert(runner_main(3,dma)==1);assert(allocs==6&&dma_closed==6&&heap_closed==1&&queue==6&&start==1&&off==1&&closed==1&&gfmt==1);
 req=maps=closed=queue=start=events=gfmt=off=allocs=dma_closed=heap_closed=0;fail_alloc=1;memset(syncs,0,sizeof(syncs));
 assert(runner_main(3,dma)==1);assert(allocs==2&&dma_closed==2&&heap_closed==1&&maps==2&&!queue&&!start&&closed==1);
 puts("DMABUF_STAGE_PASS six-import-fds write-sync-pairs close-all; allocation-failure unwinds two");
 puts("RUNNER_STAGE_PASS format:no-REQBUFS buffers:no-QBUF queue:6-QBUF all:no-STREAMON close=1");return 0;}
'''
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp);(d/'test.c').write_text(HARNESS)
 # Stage-boundary tests never decode pixels; do not depend on private out/ data.
 (d/'sq2-h264-vector.h').write_text('''
#define SQ2_VEC_W 320
#define SQ2_VEC_H 240
#define SQ2_VEC_FRAMES 6
#define SQ2_VEC_SHA256 "host-stage-fixture-not-a-media-vector"
static const u8 sq2_vec_stream[6]={1,2,3,4,5,6};
static const u32 sq2_vec_au[7]={0,1,2,3,4,5,6};
static const u32 sq2_vec_crc_y[6]={0};
static const u32 sq2_vec_crc_uv[6]={0};
''')
 flags=['gcc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie']
 subprocess.run(flags+['-Dmain=runner_main','-I',str(d),'-c',str(ROOT/'probe/video/sq2-v4l2-decode-test.c'),'-o',str(d/'runner.o')],check=True)
 subprocess.run(flags+[str(d/'test.c'),str(d/'runner.o')]+['-Wl,--wrap='+x for x in ['open','close','fsync','mmap','munmap','ioctl','poll']]+['-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
