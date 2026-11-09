#define Q2_BRIDGE_TEST 1
#include "../../../probe/video/quest2-vidc-bridge.c"
#include <assert.h>
static int allocated,negotiated,fail,alter,invalid,calls;
static int layout(unsigned w,unsigned h,unsigned f,unsigned *stride,unsigned *scan,unsigned *size) {
    assert(w==320 && h==240 && f==V4L2_PIX_FMT_NV12);
    *stride=invalid?321:512;*scan=512;*size=393216;return 0;
}
static int kernel(int fd,unsigned long request,void *argument) {
    assert(fd==91);calls++;
    if(request==VIDIOC_G_FMT) {
        struct v4l2_format *f=argument;
        f->fmt.pix_mp.width=320;f->fmt.pix_mp.height=256;
        f->fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12;f->fmt.pix_mp.num_planes=1;
    } else if(request==VIDIOC_G_SELECTION) {
        ((struct v4l2_selection *)argument)->r=(struct v4l2_rect){0,0,320,240};
    } else if(request==VIDIOC_S_FMT) {
        assert(!allocated);
        struct v4l2_format *f=argument;
        assert(f->fmt.pix_mp.height==512 && f->fmt.pix_mp.plane_fmt[0].bytesperline==512);
        if(fail)return q2_error(EINVAL);
        if(alter)f->fmt.pix_mp.height=256;
        negotiated++;
    } else if(request==VIDIOC_REQBUFS) {
        struct v4l2_requestbuffers *r=argument;
        assert(r->memory==V4L2_MEMORY_DMABUF);
        if(r->count)assert(negotiated);
        allocated=r->count;
    } else assert(0);
    return 0;
}
int main(void) {
    struct q2_session s={.fd=91,.backend=kernel,.native_layout=layout,.layout_pending=1};
    s.queues[1].count=1;
    assert(q2_native_layout(&s,0)==-1 && errno==EBUSY && !calls);
    s.queues[1].count=0;s.streaming=2;
    assert(q2_native_layout(&s,0)==-1 && errno==EBUSY && !calls);
    s.streaming=0;invalid=1;
    assert(q2_native_layout(&s,0)==-1 && errno==EINVAL && !negotiated && s.layout_pending);
    invalid=0;fail=1;
    assert(q2_native_layout(&s,0)==-1 && errno==EINVAL && !negotiated && s.layout_pending);
    fail=0;alter=1;
    assert(q2_native_layout(&s,0)==-1 && errno==EPROTO && s.layout_pending && !s.layout_stride);
    alter=0;
    struct v4l2_requestbuffers r={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,.memory=V4L2_MEMORY_USERPTR,.count=2};
    assert(!q2_dispatch(&s,VIDIOC_REQBUFS,&r) && allocated==2 && !s.layout_pending && s.layout_stride==512);
    s.layout_pending=1;r.count=0;
    assert(!q2_dispatch(&s,VIDIOC_REQBUFS,&r) && !allocated && !s.layout_pending && !s.queues[1].count);
    s.layout_pending=1;s.native_layout=NULL;calls=0;
    assert(!q2_native_layout(&s,0) && !calls);
    q2_clear(&s);
    puts("NATIVE_LAYOUT: busy queues rejected; real format negotiation precedes allocation; errors and default preserved");
}
