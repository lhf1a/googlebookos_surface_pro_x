#include "../../../probe/video/quest2-vidc-bridge.c"
#include <assert.h>
static unsigned allocation_size=393216,minimum_size=147456;
static int allocation_error,layout_calls;
int quest2_omx_capture_layout_for_usage(unsigned w,unsigned h,unsigned format,uint64_t usage,
                                      unsigned *stride,unsigned *scan,unsigned *size) {
    assert(w==320 && h==240 && format==V4L2_PIX_FMT_NV12 && usage==0x20400b00);
    layout_calls++;
    if(allocation_error)return -ENOMEM;
    *stride=512;*scan=512;*size=allocation_size;return 0;
}
static int kernel(int fd,unsigned long request,void *arg) {
    assert(fcntl(fd,F_GETFD)>=0);
    if(request==VIDIOC_G_FMT) {
        struct v4l2_format *f=arg;
        f->fmt.pix_mp.width=320;f->fmt.pix_mp.height=256;
        f->fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12;
        f->fmt.pix_mp.plane_fmt[0].bytesperline=384;
        f->fmt.pix_mp.plane_fmt[0].sizeimage=minimum_size;
    } else if(request==VIDIOC_G_SELECTION)
        ((struct v4l2_selection*)arg)->r=(struct v4l2_rect){0,0,320,240};
    else {assert(request==VIDIOC_S_FMT);assert(((struct v4l2_format*)arg)->fmt.pix_mp.plane_fmt[0].sizeimage==allocation_size);}
    return 0;
}
int main(void) {
    q2_enabled=1;
    uint64_t token=quest2_vidc_node_begin();assert(token);
    int fd=syscall(SYS_memfd_create,"legacy-layout",1);assert(fd>=0);
    struct q2_session *s=calloc(1,sizeof(*s));assert(s);
    s->fd=fd;s->backend=kernel;s->wake=eventfd(0,EFD_CLOEXEC);
    assert(s->wake>=0 && !pthread_mutex_init(&s->mutex,NULL));
    pthread_mutex_lock(&q2_lock);assert(!q2_attach(fd,s));q2_node_open(s);pthread_mutex_unlock(&q2_lock);
    assert(!quest2_vidc_node_end(token));
    assert(!quest2_vidc_node_configure(token,1,0,0));
    assert(!quest2_vidc_node_configure(token,1,0x20400b00,1));
    s->layout_pending=1;s->source_changes=1;
    struct quest2_vidc_node_layout out;
    assert(!quest2_vidc_node_get_layout(token,&out));
    assert(out.width==320 && out.height==240 && out.stride==512 && out.scan==512 && out.size==allocation_size);
    s->layout_pending=1;minimum_size=allocation_size+4096;
    assert(quest2_vidc_node_get_layout(token,&out)<0 && errno==EINVAL && s->layout_pending);
    minimum_size=147456;allocation_error=1;
    assert(quest2_vidc_node_get_layout(token,&out)<0 && errno==EOPNOTSUPP && s->layout_pending);
    allocation_error=0;assert(!quest2_vidc_node_get_layout(token,&out));
    assert(layout_calls==4);
    assert(!quest2_vidc_node_configure(token,0,0,0));
    assert(!close(fd));puts("LEGACY_LAYOUT: v1-only service negotiates actual layout; undersized/error allocations rejected");
}
