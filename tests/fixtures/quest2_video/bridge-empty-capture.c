#define Q2_BRIDGE_TEST 1
#include "../../../probe/video/quest2-vidc-bridge.c"
#include <assert.h>
static unsigned allocated;
static int released,formats,release_error,format_error,restore_error,alter;
static int layout(unsigned a,unsigned b,unsigned c,unsigned *d,unsigned *e,unsigned *f) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return -1;
}
static int kernel(int fd,unsigned long request,void *arg) {
    assert(fd==91);
    if(request==VIDIOC_S_FMT) {
        formats++;
        if(allocated)return q2_error(EBUSY);
        return format_error?q2_error(format_error):0;
    }
    assert(request==VIDIOC_REQBUFS);
    struct v4l2_requestbuffers *r=arg;
    assert(r->type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && r->memory==V4L2_MEMORY_DMABUF);
    if(!r->count) {
        if(release_error)return q2_error(release_error);
        released++;allocated=0;
    } else {
        if(restore_error)return q2_error(restore_error);
        if(alter)r->count--;
        allocated=r->count;
    }
    return 0;
}
static void setup(struct q2_session *s) {
    *s=(struct q2_session){.fd=91,.backend=kernel,.native_layout=layout};
    s->queues[1]=(struct q2_queue){.count=18,.userptr=1,.slots=calloc(18,sizeof(struct q2_buffer_slot))};
    assert(s->queues[1].slots);allocated=18;released=formats=0;
    release_error=format_error=restore_error=alter=0;
}
int main(void) {
    struct q2_session s;
    struct v4l2_format f={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
    setup(&s);assert(!q2_dispatch(&s,VIDIOC_S_FMT,&f) && allocated==18 && released==1 && formats==2);
    s.native_layout=NULL;assert(q2_dispatch(&s,VIDIOC_S_FMT,&f)<0 && errno==EBUSY && released==1);
    s.native_layout=layout;s.streaming=1;assert(q2_dispatch(&s,VIDIOC_S_FMT,&f)<0 && errno==EBUSY && released==1);
    s.streaming=0;s.queued=1;assert(q2_dispatch(&s,VIDIOC_S_FMT,&f)<0 && errno==EBUSY && released==1);
    s.queued=0;s.queues[1].slots[0].valid=1;s.queues[1].slots[0].pin=-1;
    assert(q2_dispatch(&s,VIDIOC_S_FMT,&f)<0 && errno==EBUSY && released==1);
    s.queues[1].slots[0].valid=0;
    release_error=EIO;assert(q2_dispatch(&s,VIDIOC_S_FMT,&f)<0 && errno==EIO && allocated==18 && released==1);
    release_error=0;format_error=EINVAL;assert(q2_dispatch(&s,VIDIOC_S_FMT,&f)<0 && errno==EINVAL && allocated==18);
    q2_queue_clear(&s,&s.queues[1]);
    setup(&s);restore_error=ENOMEM;assert(q2_dispatch(&s,VIDIOC_S_FMT,&f)<0 && errno==ENOMEM && !s.queues[1].count && !allocated);
    setup(&s);alter=1;assert(q2_dispatch(&s,VIDIOC_S_FMT,&f)<0 && errno==EPROTO && !s.queues[1].count && !allocated);
    q2_clear(&s);
    puts("EMPTY_CAPTURE: real release/format/restore; ownership, default, errors and count preserved");
}
