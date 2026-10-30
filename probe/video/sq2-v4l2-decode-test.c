/* Bounded stateful V4L2 lab test. Uses the pinned independently decoded vector.
 * No module loading, firmware start, driver rebinding or system configuration.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <linux/dma-heap.h>
#include <linux/dma-buf.h>
typedef uint32_t u32;
typedef uint8_t u8;
#ifdef SQ2_MPEG2_VECTOR
#include "sq2-mpeg2-vector.h"
#define SQ2_INPUT_CODEC SQ2_VEC_CODEC
#elif defined(SQ2_VPX_VECTOR)
#include "sq2-vpx-vector.h"
#define SQ2_INPUT_CODEC SQ2_VEC_CODEC
#elif defined(SQ2_HEVC_VECTOR)
#include "sq2-hevc-vector.h"
#define SQ2_INPUT_CODEC V4L2_PIX_FMT_HEVC
#else
#include "sq2-h264-vector.h"
#define SQ2_INPUT_CODEC V4L2_PIX_FMT_H264
#endif
#define MAXBUF 32
#ifndef SQ2_VEC_BYTES_PER_SAMPLE
#define SQ2_VEC_BYTES_PER_SAMPLE 1
#endif
#if SQ2_VEC_BYTES_PER_SAMPLE == 2
#define SQ2_OUTPUT_CODEC V4L2_PIX_FMT_P010
#elif SQ2_VEC_BYTES_PER_SAMPLE == 1
#define SQ2_OUTPUT_CODEC V4L2_PIX_FMT_NV12
#else
#error "Unsupported sample size"
#endif
#define SQ2_ROW_BYTES (SQ2_VEC_W * SQ2_VEC_BYTES_PER_SAMPLE)
struct queue { unsigned type,n,memory,size; void *ptr[MAXBUF]; size_t len[MAXBUF]; int dmafd[MAXBUF]; int on; };
static int fd=-1;
static int heap=-1;
static int graphic_fds=0;
static int cpu_sync(struct queue *q,unsigned i,unsigned flags){
 if(q->memory!=V4L2_MEMORY_DMABUF)return 0;
 struct dma_buf_sync s={.flags=flags};int ret;
 do{ret=ioctl(q->dmafd[i],DMA_BUF_IOCTL_SYNC,&s);}while(ret<0&&errno==EINTR);
 if(ret)perror("DMA_BUF_SYNC");
 return ret;
}
static int x(unsigned long op,void *p,const char *name){printf("IOCTL_BEGIN %s\n",name);fsync(STDOUT_FILENO);int r;do{r=ioctl(fd,op,p);}while(r<0&&errno==EINTR);if(r<0)fprintf(stderr,"%s errno=%d %s\n",name,errno,strerror(errno));printf("IOCTL_END %s ret=%d\n",name,r);fsync(STDOUT_FILENO);fsync(STDERR_FILENO);return r;}
static long long ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static u32 crc_rows(const u8 *p,unsigned stride,unsigned w,unsigned h){u32 c=~0U;for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){c^=p[(size_t)y*stride+x];for(int b=0;b<8;b++)c=(c>>1)^(0xedb88320U & (0U-(c&1)));}return ~c;}
static int allocq(struct queue *q,unsigned count){
 struct v4l2_requestbuffers r={.type=q->type,.memory=q->memory,.count=count};
 if(x(VIDIOC_REQBUFS,&r,"REQBUFS"))return -1;
 if(!r.count||r.count>MAXBUF){fprintf(stderr,"buffer count refused=%u\n",r.count);return -1;}q->n=r.count;
 size_t total=0;
 for(unsigned i=0;i<q->n;i++){
  if(q->memory==V4L2_MEMORY_DMABUF){
   size_t len=((size_t)q->size+4095)&~(size_t)4095;
   if(!len||len>16*1024*1024U||(total+=len)>128*1024*1024U)return -1;
   if(graphic_fds&&q->type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE){
    if(q->n!=9||q->size!=159744||len!=159744)return -1;
    int imported=dup(200+(int)i);if(imported<0)return -1;
    q->dmafd[i]=imported;
    if(lseek(imported,0,SEEK_END)!=(off_t)len)return -1;
    lseek(imported,0,SEEK_SET);q->len[i]=len;
    q->ptr[i]=mmap(NULL,len,PROT_READ|PROT_WRITE,MAP_SHARED,imported,0);
    if(q->ptr[i]==MAP_FAILED){q->ptr[i]=NULL;return -1;}
    printf("C2_IMPORT index=%u bytes=%zu\n",i,len);continue;
   }
   struct dma_heap_allocation_data a={.len=len,.fd_flags=O_RDWR|O_CLOEXEC};
   if(ioctl(heap,DMA_HEAP_IOCTL_ALLOC,&a)){perror("DMA_HEAP_ALLOC");return -1;}
   q->dmafd[i]=a.fd;q->len[i]=len;q->ptr[i]=mmap(NULL,len,PROT_READ|PROT_WRITE,MAP_SHARED,a.fd,0);
   if(q->ptr[i]==MAP_FAILED){q->ptr[i]=NULL;perror("dma mmap");return -1;}
   continue;
  }
  struct v4l2_plane p[VIDEO_MAX_PLANES]={0};struct v4l2_buffer b={.type=q->type,.memory=V4L2_MEMORY_MMAP,.index=i,.length=VIDEO_MAX_PLANES,.m.planes=p};
  if(x(VIDIOC_QUERYBUF,&b,"QUERYBUF"))return -1;
  if(b.length!=1||!p[0].length||p[0].length>16*1024*1024U||(total+=p[0].length)>128*1024*1024U){fprintf(stderr,"allocation bounds\n");return -1;}
  q->len[i]=p[0].length;q->ptr[i]=mmap(NULL,p[0].length,PROT_READ|PROT_WRITE,MAP_SHARED,fd,p[0].m.mem_offset);
  if(q->ptr[i]==MAP_FAILED){q->ptr[i]=NULL;perror("mmap");return -1;}
 }
 printf("ALLOC type=%u count=%u bytes=%zu\n",q->type,q->n,total);return 0;
}
/* Explicit local evidence only: one bounded payload, never overwrite a file.
 * This does not change any decode/geometry/CRC acceptance criterion. */
static int dump_first(const char *path,struct queue *q,unsigned i,size_t offset,size_t bytes){
 if(!bytes||bytes>1024*1024||offset>q->len[i]||bytes>q->len[i]-offset)return -1;
 int target=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(target<0){perror("dump open");return -1;}
 int bad=cpu_sync(q,i,DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ),synced=!bad;
 size_t done=0;
 while(!bad&&done<bytes){ssize_t n=write(target,(u8*)q->ptr[i]+offset+done,bytes-done);if(n<0&&errno==EINTR)continue;if(n<=0){bad=1;break;}done+=(size_t)n;}
 if(synced&&cpu_sync(q,i,DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ))bad=1;
 if(fsync(target))bad=1;
 if(close(target))bad=1;
 printf("DUMP_FIRST bytes=%zu written=%zu result=%d\n",bytes,done,bad);
 return bad;
}
static int put(struct queue *q,unsigned i,unsigned bytes){
 struct v4l2_plane p={.length=q->len[i],.bytesused=bytes};struct v4l2_buffer b={.type=q->type,.memory=q->memory,.index=i,.length=1,.m.planes=&p};
 if(q->memory==V4L2_MEMORY_DMABUF)p.m.fd=q->dmafd[i];
 if(q->type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)b.timestamp.tv_usec=(i+1)*33333;
 return x(VIDIOC_QBUF,&b,"QBUF");
}
static int start(struct queue *q){printf("STREAMON_BEGIN type=%u\n",q->type);if(x(VIDIOC_STREAMON,&q->type,"STREAMON"))return -1;q->on=1;printf("STREAMON_DONE type=%u\n",q->type);return 0;}
static int end(struct queue *q){int failed=0;if(q->on){printf("STREAMOFF_BEGIN type=%u\n",q->type);if(x(VIDIOC_STREAMOFF,&q->type,"STREAMOFF"))failed=1;else q->on=0;printf("STREAMOFF_DONE type=%u failed=%d\n",q->type,failed);}for(unsigned i=0;i<q->n;i++){if(q->ptr[i]&&munmap(q->ptr[i],q->len[i]))failed=1;if(q->memory==V4L2_MEMORY_DMABUF&&q->dmafd[i]>=0){if(close(q->dmafd[i]))failed=1;q->dmafd[i]=-1;}}return failed;}
int main(int argc,char **argv){
 setbuf(stdout,NULL);
 if(argc==3&&!strcmp(argv[1],"--reference-test")){
  FILE *ref=fopen(argv[2],"rb");if(!ref)return 1;
  const unsigned stride=(SQ2_ROW_BYTES+255)&~255u,scan=(SQ2_VEC_H+31)&~31u;
  const size_t uv=(size_t)stride*scan,bytes=uv+(size_t)stride*((SQ2_VEC_H+1)/2);
  u8 *p=malloc(bytes);if(!p){fclose(ref);return 1;}int bad=0;
  for(unsigned n=0;n<SQ2_VEC_FRAMES&&!bad;n++){
   memset(p,0xa5,bytes);
   for(unsigned row=0;row<SQ2_VEC_H;row++)if(fread(p+(size_t)row*stride,1,SQ2_ROW_BYTES,ref)!=SQ2_ROW_BYTES)bad=1;
   for(unsigned row=0;row<SQ2_VEC_H/2;row++)if(fread(p+uv+(size_t)row*stride,1,SQ2_ROW_BYTES,ref)!=SQ2_ROW_BYTES)bad=1;
   if(crc_rows(p,stride,SQ2_ROW_BYTES,SQ2_VEC_H)!=sq2_vec_crc_y[n]||
      crc_rows(p+uv,stride,SQ2_ROW_BYTES,SQ2_VEC_H/2)!=sq2_vec_crc_uv[n])bad=1;
  }
  if(fgetc(ref)!=EOF)bad=1;
  free(p);fclose(ref);printf("PADDED_REFERENCE_TEST bytes_per_sample=%u result=%d\n",SQ2_VEC_BYTES_PER_SAMPLE,bad);return bad;
 }
 if(argc==2&&!strcmp(argv[1],"--self-test")){
  const u8 s[]="123456789";u32 c=crc_rows(s,9,9,1);printf("CRC_SELF_TEST=%08x vector_bytes=%zu frames=%u\n",c,sizeof(sq2_vec_stream),SQ2_VEC_FRAMES);return c!=0xcbf43926U;
 }
 int format_only=argc==3&&!strcmp(argv[1],"--format-only");
 int buffers_only=argc==3&&!strcmp(argv[1],"--buffers-only");
 int queue_only=argc==3&&!strcmp(argv[1],"--queue-only");
 graphic_fds=argc==3&&!strcmp(argv[1],"--graphic-fds");
 const char *dump_path=argc==4&&!strcmp(argv[1],"--dump-first")?argv[2]:NULL;
 int dmabuf=dump_path||graphic_fds||(argc==3&&!strcmp(argv[1],"--dmabuf"));
 if(argc!=2&&!format_only&&!buffers_only&&!queue_only&&!dmabuf){fprintf(stderr,"usage: %s [--format-only|--buffers-only|--queue-only|--dmabuf] /dev/videoN | --dump-first NEW_FILE /dev/videoN | --self-test\n",argv[0]);return 2;}
 struct queue out={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE},cap={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};int result=1,source=0,eos=0,stop=0;unsigned returned=0,mask=0,frames=0;long long deadline=ms()+20000;
 out.memory=cap.memory=dmabuf?V4L2_MEMORY_DMABUF:V4L2_MEMORY_MMAP;
 for(unsigned i=0;i<MAXBUF;i++)out.dmafd[i]=cap.dmafd[i]=-1;
 if(dmabuf){heap=open("/dev/dma_heap/system",O_RDONLY|O_CLOEXEC);if(heap<0){perror("dma heap");return 1;}}
 printf("MEMORY=%s\n",dmabuf?"DMABUF-system":"MMAP");
 puts("OPEN_BEGIN");fsync(STDOUT_FILENO);
 fd=open(argv[argc-1],O_RDWR|O_NONBLOCK|O_CLOEXEC);if(fd<0){perror("open");if(heap>=0)close(heap);heap=-1;return 1;}
 puts("OPEN_DONE");fsync(STDOUT_FILENO);
 struct v4l2_capability c={0};if(x(VIDIOC_QUERYCAP,&c,"QUERYCAP"))goto done;
 if(strcmp((char*)c.driver,"qcom-venus")){fprintf(stderr,"wrong driver\n");goto done;}
 printf("VECTOR_SHA256=%s\n",SQ2_VEC_SHA256);
 struct v4l2_event_subscription sub={.type=V4L2_EVENT_SOURCE_CHANGE};if(x(VIDIOC_SUBSCRIBE_EVENT,&sub,"SUB_SOURCE"))goto done;sub.type=V4L2_EVENT_EOS;if(x(VIDIOC_SUBSCRIBE_EVENT,&sub,"SUB_EOS"))goto done;
 struct v4l2_format f={.type=out.type};f.fmt.pix_mp.width=SQ2_VEC_W;f.fmt.pix_mp.height=SQ2_VEC_H;f.fmt.pix_mp.pixelformat=SQ2_INPUT_CODEC;f.fmt.pix_mp.field=V4L2_FIELD_NONE;
 if(x(VIDIOC_S_FMT,&f,"S_FMT_OUTPUT"))goto done;
 if(f.fmt.pix_mp.pixelformat!=SQ2_INPUT_CODEC){fprintf(stderr,"codec substitution refused\n");goto done;}
 out.size=f.fmt.pix_mp.plane_fmt[0].sizeimage;
 if(format_only){puts("FORMAT_ONLY_DONE_NO_BUFFERS_NO_FRAMES");result=0;goto done;}
 if(allocq(&out,SQ2_VEC_FRAMES)||out.n<SQ2_VEC_FRAMES)goto done;
 if(buffers_only){puts("BUFFERS_ONLY_DONE_NO_QBUF_NO_STREAMON");result=0;goto done;}
 for(unsigned i=0;i<SQ2_VEC_FRAMES;i++){unsigned n=sq2_vec_au[i+1]-sq2_vec_au[i];if(n>out.len[i])goto done;if(cpu_sync(&out,i,DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE))goto done;memcpy(out.ptr[i],sq2_vec_stream+sq2_vec_au[i],n);if(cpu_sync(&out,i,DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE)||put(&out,i,n))goto done;}
 if(queue_only){puts("QUEUE_ONLY_DONE_NO_STREAMON");result=0;goto done;}
 if(start(&out))goto done;
 while(!source&&ms()<deadline){struct v4l2_event e={0};if(!ioctl(fd,VIDIOC_DQEVENT,&e)){printf("EVENT type=%u changes=%u\n",e.type,e.u.src_change.changes);if(e.type==V4L2_EVENT_SOURCE_CHANGE)source=1;}else if(errno!=EAGAIN&&errno!=ENOENT){perror("DQEVENT");goto done;}if(!source){struct pollfd p={fd,POLLPRI,0};poll(&p,1,100);}}
 if(!source){fprintf(stderr,"SOURCE_CHANGE_TIMEOUT\n");goto done;}
 struct v4l2_selection before_format={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,.target=V4L2_SEL_TGT_COMPOSE};
 if(x(VIDIOC_G_SELECTION,&before_format,"G_SELECTION_BEFORE_FORMAT"))goto done;
 printf("CROP_BEFORE_FORMAT=%d,%d,%u,%u\n",before_format.r.left,before_format.r.top,before_format.r.width,before_format.r.height);
 memset(&f,0,sizeof(f));f.type=cap.type;if(x(VIDIOC_G_FMT,&f,"G_FMT_CAPTURE"))goto done;if(f.fmt.pix_mp.pixelformat!=SQ2_OUTPUT_CODEC){f.fmt.pix_mp.pixelformat=SQ2_OUTPUT_CODEC;if(x(VIDIOC_S_FMT,&f,"S_FMT_CAPTURE"))goto done;}
 unsigned stride=f.fmt.pix_mp.plane_fmt[0].bytesperline,scan=f.fmt.pix_mp.height;
 cap.size=f.fmt.pix_mp.plane_fmt[0].sizeimage;
 struct v4l2_selection sel={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,.target=V4L2_SEL_TGT_COMPOSE};if(x(VIDIOC_G_SELECTION,&sel,"G_SELECTION"))goto done;
 printf("CAPTURE fourcc=%08x width=%u height=%u stride=%u size=%u crop=%d,%d,%u,%u\n",f.fmt.pix_mp.pixelformat,f.fmt.pix_mp.width,scan,stride,f.fmt.pix_mp.plane_fmt[0].sizeimage,sel.r.left,sel.r.top,sel.r.width,sel.r.height);
 if(f.fmt.pix_mp.pixelformat!=SQ2_OUTPUT_CODEC||f.fmt.pix_mp.num_planes!=1||sel.r.left||sel.r.top||sel.r.width!=SQ2_VEC_W||sel.r.height!=SQ2_VEC_H||stride<SQ2_ROW_BYTES||scan<SQ2_VEC_H||stride>16384||scan>8192){fprintf(stderr,"unexpected geometry\n");goto done;}
 struct v4l2_control ctrl={.id=V4L2_CID_MIN_BUFFERS_FOR_CAPTURE};if(x(VIDIOC_G_CTRL,&ctrl,"MIN_CAPTURE"))goto done;
 if(ctrl.value<1||ctrl.value>28)goto done;
 if(graphic_fds&&(stride!=384||scan!=256||cap.size!=159744)){fprintf(stderr,"C2 geometry mismatch\n");goto done;}
 if(allocq(&cap,ctrl.value+4))goto done;
 for(unsigned i=0;i<cap.n;i++)if(put(&cap,i,0))goto done;
 if(start(&cap))goto done;
 while(ms()<deadline&&!eos){
  struct v4l2_plane p={0};struct v4l2_buffer b={.type=out.type,.memory=out.memory,.length=1,.m.planes=&p};
  if(!ioctl(fd,VIDIOC_DQBUF,&b)){returned++;printf("OUTPUT_DONE index=%u flags=%x\n",b.index,b.flags);if(b.flags&V4L2_BUF_FLAG_ERROR)goto done;}else if(errno!=EAGAIN){perror("DQ_OUTPUT");goto done;}
  if(returned==SQ2_VEC_FRAMES&&!stop){struct v4l2_decoder_cmd cmd={.cmd=V4L2_DEC_CMD_STOP};if(x(VIDIOC_DECODER_CMD,&cmd,"DECODER_STOP"))goto done;stop=1;puts("DRAIN_SENT");}
  memset(&p,0,sizeof(p));memset(&b,0,sizeof(b));b.type=cap.type;b.memory=cap.memory;b.length=1;b.m.planes=&p;
  if(!ioctl(fd,VIDIOC_DQBUF,&b)){
   printf("CAPTURE_DONE index=%u bytes=%u offset=%u flags=%x timestamp=%ld.%06ld\n",b.index,p.bytesused,p.data_offset,b.flags,(long)b.timestamp.tv_sec,(long)b.timestamp.tv_usec);
   if(b.index>=cap.n||p.bytesused>cap.len[b.index]||p.data_offset>p.bytesused||(b.flags&V4L2_BUF_FLAG_ERROR))goto done;
   if(dump_path&&p.bytesused>p.data_offset){if(dump_first(dump_path,&cap,b.index,p.data_offset,p.bytesused-p.data_offset))goto done;dump_path=NULL;}
   if(p.bytesused>p.data_offset){size_t uv=(size_t)stride*scan;size_t need=uv+(SQ2_VEC_H/2-1)*stride+SQ2_ROW_BYTES;if(need>p.bytesused-p.data_offset){fprintf(stderr,"short frame\n");goto done;}
    if(cpu_sync(&cap,b.index,DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ))goto done;
    u8 *data=(u8*)cap.ptr[b.index]+p.data_offset;u32 y=crc_rows(data,stride,SQ2_ROW_BYTES,SQ2_VEC_H),uvcrc=crc_rows(data+uv,stride,SQ2_ROW_BYTES,SQ2_VEC_H/2);int match=-1;
    if(cpu_sync(&cap,b.index,DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ))goto done;
    for(unsigned i=0;i<SQ2_VEC_FRAMES;i++)if(y==sq2_vec_crc_y[i]&&uvcrc==sq2_vec_crc_uv[i]&&!(mask&(1U<<i))){match=i;break;}
    printf("FRAME crc_y=%08x crc_uv=%08x match=%d\n",y,uvcrc,match);if(match<0)goto done;mask|=1U<<match;frames++;
   }
   if(b.flags&V4L2_BUF_FLAG_LAST)eos=1;else if(put(&cap,b.index,0))goto done;
  }else if(errno!=EAGAIN){perror("DQ_CAPTURE");goto done;}
  struct v4l2_event ev={0};if(!ioctl(fd,VIDIOC_DQEVENT,&ev)){printf("EVENT type=%u\n",ev.type);/* LAST, not an early EOS event, terminates draining. */if(ev.type==V4L2_EVENT_SOURCE_CHANGE){fprintf(stderr,"unexpected second source change\n");goto done;}}else if(errno!=EAGAIN&&errno!=ENOENT){perror("DQEVENT");goto done;}
  struct pollfd pollfd={fd,POLLIN|POLLPRI|POLLOUT,0};poll(&pollfd,1,50);
 }
 if(eos&&frames==SQ2_VEC_FRAMES&&mask==63&&returned==SQ2_VEC_FRAMES)result=0;
 done:
 printf("RESULT source=%d frames=%u mask=%x input_returned=%u drain=%d last=%d ret=%d\n",source,frames,mask,returned,stop,eos,result);
 if(end(&cap))result=1;
 if(end(&out))result=1;
 puts("CLOSE_BEGIN");fsync(STDOUT_FILENO);if(close(fd)){perror("close");result=1;}if(heap>=0){if(close(heap))result=1;heap=-1;}puts("CLOSE_DONE");fsync(STDOUT_FILENO);fsync(STDERR_FILENO);return result;
}
