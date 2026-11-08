/* Host mock tests ownership/ABI, not hardware DMA or decode capability. */
#define Q2_BRIDGE_TEST 1
#include "../../../probe/video/quest2-vidc-bridge.c"
#include <assert.h>
static unsigned control_calls,mask,allocations,queued_index;
static int fail_control,fail_allocate,fail_queue,fail_access,malformed,overflow,queued;
static int open_access,source_pending;
static uint32_t extra_bytes=188;
static struct v4l2_plane saved[2];
static unsigned char firmware[188];
static int cpu_access(int fd,uint64_t flags) {
    assert(fcntl(fd,F_GETFD)>=0);
    if(fail_access)return q2_error(EIO);
    if(flags&DMA_BUF_SYNC_END)open_access--;else open_access++;
    return 0;
}
static int allocate(size_t size,int *fd) {
    if(fail_allocate)return q2_error(ENOMEM);
    assert(size==4096);allocations++;
    *fd=(int)syscall(SYS_memfd_create,"mock-extra",1);
    assert(*fd>=0 && !ftruncate(*fd,size));return 0;
}
static int kernel(int fd,unsigned long req,void *arg) {
    assert(fd==91);
    if(req==VIDIOC_S_FMT || req==VIDIOC_TRY_FMT) return 0;
    if(req==VIDIOC_S_CTRL) {
        struct v4l2_control *c=arg;assert(c->id==Q2_NATIVE_EXTRA_CTRL);control_calls++;
        if(fail_control)return q2_error(EIO);
        mask=c->value;return 0;
    }
    if(req==VIDIOC_G_FMT) {
        if(source_pending)return q2_error(EINVAL);
        struct v4l2_format *f=arg;
        f->fmt.pix_mp=(struct v4l2_pix_format_mplane){.width=320,.height=256,.pixelformat=V4L2_PIX_FMT_P010,.num_planes=2};
        f->fmt.pix_mp.plane_fmt[0].bytesperline=640;
        f->fmt.pix_mp.plane_fmt[0].sizeimage=294912;
        f->fmt.pix_mp.plane_fmt[1].sizeimage=extra_bytes;return 0;
    }
    if(req==VIDIOC_G_SELECTION) {
        ((struct v4l2_selection *)arg)->r=(struct v4l2_rect){.width=320,.height=240};return 0;
    }
    if(req==VIDIOC_REQBUFS) {
        struct v4l2_requestbuffers *b=arg;assert(b->memory==V4L2_MEMORY_DMABUF);
        if(b->count)b->count=2;
        return 0;
    }
    if(req==VIDIOC_QBUF || req==VIDIOC_PREPARE_BUF) {
        struct v4l2_buffer *b=arg;
        assert(b->length==2 && b->memory==V4L2_MEMORY_DMABUF);
        assert(b->m.planes[0].m.fd!=b->m.planes[1].m.fd && b->m.planes[1].length==4096);
        for(unsigned n=0;n<2;n++){
            assert(!b->m.planes[n].data_offset);
            for(unsigned j=0;j<11;j++)assert(!b->m.planes[n].reserved[j]);
        }
        uint32_t zero=1;assert(pread(b->m.planes[1].m.fd,&zero,4,0)==4 && !zero);
        if(fail_queue)return q2_error(EIO);
        memcpy(saved,b->m.planes,sizeof(saved));queued_index=b->index;
        if(req==VIDIOC_QBUF)queued=1;
        return 0;
    }
    if(req==VIDIOC_DQBUF) {
        struct v4l2_buffer *b=arg;assert(queued && b->length==2);queued=0;
        assert(pwrite(saved[1].m.fd,firmware,sizeof(firmware),0)==sizeof(firmware));
        if(malformed){uint32_t size=UINT32_MAX;assert(pwrite(saved[1].m.fd,&size,4,0)==4);}
        memcpy(b->m.planes,saved,sizeof(saved));b->index=queued_index;b->flags=0;
        b->m.planes[0].bytesused=245760;b->m.planes[1].bytesused=overflow?4097:188;return 0;
    }
    if(req==VIDIOC_QUERYBUF) {
        struct v4l2_buffer *b=arg;assert(b->length==2);return 0;
    }
    assert(0);return -1;
}
static void make_firmware(unsigned transfer) {
    uint32_t vui[]={76,0x01010100,3,0x7f100006,56,1,5,10,10,0,1,9,transfer,9,0,1,0,0,0};
    uint32_t end[]={24,0x01010100,3,0,0,0};
    memset(firmware,0xa5,sizeof(firmware));size_t at=0;
    if(transfer==16) {
        uint32_t mastering[]={60,0x01010100,3,0x15,40,
            13250,7500,34000,34500,3000,16000,15635,16450,10000000,50};
        uint32_t light[]={28,0x01010100,3,0x16,8,1000,400};
        memcpy(firmware,mastering,sizeof(mastering));at+=sizeof(mastering);
        memcpy(firmware+at,light,sizeof(light));at+=sizeof(light);
    }
    memcpy(firmware+at,vui,sizeof(vui));at+=sizeof(vui);
    memcpy(firmware+at,end,sizeof(end));
}
int main(void) {
    make_firmware(16);
    struct q2_session s={.fd=91,.backend=kernel,.native_extra_enabled=1,.allocate_extra=allocate,.cpu_access=cpu_access};
    s.wake=eventfd(0,EFD_NONBLOCK|EFD_SEMAPHORE);assert(s.wake>=0);
    struct v4l2_format f={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE};f.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_HEVC;
    assert(!q2_dispatch(&s,VIDIOC_TRY_FMT,&f) && !mask && !control_calls);
    fail_control=1;assert(q2_dispatch(&s,VIDIOC_S_FMT,&f)<0 && errno==EIO && !s.native_extra_mask);
    fail_control=0;assert(!q2_dispatch(&s,VIDIOC_S_FMT,&f) && mask==7 && s.native_extra_mask==7);
    unsigned before=control_calls;assert(!q2_dispatch(&s,VIDIOC_S_FMT,&f) && control_calls==before);
    struct v4l2_control ctrl={.id=Q2_CTRL_BASE+17,.value=26};
    assert(!q2_dispatch(&s,VIDIOC_S_CTRL,&ctrl));ctrl.value=2;
    assert(q2_dispatch(&s,VIDIOC_S_CTRL,&ctrl)<0 && errno==EOPNOTSUPP);
    f.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    assert(!q2_dispatch(&s,VIDIOC_G_FMT,&f) && f.fmt.pix_mp.num_planes==2 &&
           f.fmt.pix_mp.pixelformat==Q2_PIX_FMT_P010_VENUS && f.fmt.pix_mp.plane_fmt[1].sizeimage==4096);
    struct v4l2_requestbuffers req={.type=f.type,.memory=V4L2_MEMORY_USERPTR,.count=2};
    assert(!q2_dispatch(&s,VIDIOC_REQBUFS,&req));
    int video=(int)syscall(SYS_memfd_create,"mock-pixels",1);
    int donor=(int)syscall(SYS_memfd_create,"mock-donor-extra",1);
    assert(video>=0 && donor>=0 && !ftruncate(video,294912) && !ftruncate(donor,12288));
    struct v4l2_plane planes[2]={{.length=294912,.m.userptr=123},{.length=4096,.m.userptr=456}};
    planes[0].reserved[0]=video;planes[1].reserved[0]=donor;planes[1].reserved[1]=4096;
    struct v4l2_buffer b={.type=f.type,.memory=V4L2_MEMORY_USERPTR,.length=2,.m.planes=planes};
    assert(!q2_dispatch(&s,VIDIOC_QUERYBUF,&b));
    planes[0].reserved[0]=video;planes[0].length=294912;
    planes[1]=(struct v4l2_plane){.length=4096,.m.userptr=456};
    planes[1].reserved[0]=donor;planes[1].reserved[1]=4096;
    s.native_extra_capacity=0;assert(q2_dispatch(&s,VIDIOC_QBUF,&b)<0 && errno==ENODATA && !s.queued);
    s.native_extra_capacity=188;fail_allocate=1;
    assert(q2_dispatch(&s,VIDIOC_QBUF,&b)<0 && errno==ENOMEM && !s.queued);
    fail_allocate=0;fail_access=1;
    assert(q2_dispatch(&s,VIDIOC_QBUF,&b)<0 && errno==EIO && !s.queued);
    fail_access=0;fail_queue=1;
    assert(q2_dispatch(&s,VIDIOC_QBUF,&b)<0 && errno==EIO && !s.queued);
    fail_queue=0;
    for(unsigned index=0;index<2;index++){
        b.index=index;planes[1].reserved[1]=4096*(index+1);
        source_pending=1;
        assert(!q2_dispatch(&s,VIDIOC_QBUF,&b) && s.queued==1);
        source_pending=0;
        int native=s.queues[1].slots[index].native_extra_fd;
        assert(native!=donor && native!=video && fcntl(native,F_GETFD)>=0);
        make_firmware(index?18:16);
        assert(!q2_dispatch(&s,VIDIOC_DQBUF,&b) && !s.queued && !open_access);
        size_t records=index?100:188;
        assert(b.length==2 && planes[1].bytesused==336+records && planes[1].reserved[1]==4096*(index+1));
        unsigned char actual[524];assert(pread(donor,actual,sizeof(actual),planes[1].reserved[1])==sizeof(actual));
        assert(!memcmp(actual+336,firmware,records));
        uint32_t crop;memcpy(&crop,actual,4);assert(crop==336);
        for(unsigned j=336+records;j<sizeof(actual);j++)assert(!actual[j]);
    }
    assert(allocations==2 && s.queues[1].slots[0].native_extra_fd!=s.queues[1].slots[1].native_extra_fd);
    unsigned char untouched=1;assert(pread(donor,&untouched,1,0)==1 && !untouched);
    b.index=1;assert(!q2_dispatch(&s,VIDIOC_QBUF,&b));malformed=1;
    assert(!q2_dispatch(&s,VIDIOC_DQBUF,&b) && !s.queued && (b.flags&Q2_BUF_DATA_CORRUPT) && !planes[1].bytesused && !open_access);
    uint32_t end[6];assert(pread(donor,end,sizeof(end),8192)==sizeof(end) && end[0]==24 && !end[3]);
    malformed=0;b.flags=0;assert(!q2_dispatch(&s,VIDIOC_QBUF,&b));overflow=1;
    assert(!q2_dispatch(&s,VIDIOC_DQBUF,&b) && (b.flags&Q2_BUF_DATA_CORRUPT) && !s.queued && !open_access);
    int fd0=s.queues[1].slots[0].native_extra_fd,fd1=s.queues[1].slots[1].native_extra_fd;
    req.count=0;assert(!q2_dispatch(&s,VIDIOC_REQBUFS,&req));
    assert(fcntl(fd0,F_GETFD)<0 && errno==EBADF);assert(fcntl(fd1,F_GETFD)<0 && errno==EBADF);
    size_t used=0;
    assert(!q2_extra_records(firmware,188,&used) && used==100);
    assert(q2_extra_records(firmware,76,&used)<0);
    uint32_t bad[]={24,0,1,0x15,4,1};assert(q2_extra_records(bad,sizeof(bad),&used)<0);
    f.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;f.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_H264;
    assert(!q2_dispatch(&s,VIDIOC_S_FMT,&f) && s.native_extra_mask==1);
    ctrl.value=26;assert(q2_dispatch(&s,VIDIOC_S_CTRL,&ctrl)<0);
    ctrl.value=29;assert(!q2_dispatch(&s,VIDIOC_S_CTRL,&ctrl));
    close(video);close(donor);q2_clear(&s);close(s.wake);
    puts("NATIVE_EXTRA: real fd/offset separation, known-mask controls, bounded records and lifecycle/error contracts passed");
    return 0;
}
