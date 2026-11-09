/* Real descriptor aliases and threads; mock kernel proves adapter contracts only. */
#include "../../../probe/video/quest2-vidc-bridge.c"
#include <assert.h>
#include <sys/resource.h>
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static int pause_layout,layout_entered,layout_release,layout_calls;
static uint64_t last_usage;
static unsigned mock_format=V4L2_PIX_FMT_NV12,reported_format,fail_format_once,fail_after_select;
int quest2_omx_capture_layout_for_usage_v2(unsigned w,unsigned h,unsigned f,uint64_t usage,unsigned minimum,
                                      unsigned *stride,unsigned *scan,unsigned *size) {
    assert(w==320 && h==240 && (f==V4L2_PIX_FMT_NV12 || f==V4L2_PIX_FMT_P010));
    assert(!pthread_mutex_trylock(&q2_lock));pthread_mutex_unlock(&q2_lock);
    pthread_mutex_lock(&gate);
    layout_calls++;last_usage=usage;
    if(pause_layout) {
        layout_entered=1;pthread_cond_broadcast(&condition);
        while(!layout_release)pthread_cond_wait(&condition,&gate);
    }
    pthread_mutex_unlock(&gate);
    *stride=((usage&1)?640:512)*(f==V4L2_PIX_FMT_P010?2:1);*scan=512;*size=*stride*512*3/2;assert(*size>=minimum);return 0;
}
static int kernel(int fd,unsigned long request,void *argument) {
    assert(fcntl(fd,F_GETFD)>=0);
    assert(!pthread_mutex_trylock(&q2_lock));pthread_mutex_unlock(&q2_lock);
    if(request==VIDIOC_G_FMT) {
        struct v4l2_format *f=argument;
        f->fmt.pix_mp.width=320;f->fmt.pix_mp.height=256;
        f->fmt.pix_mp.pixelformat=reported_format?reported_format:mock_format;f->fmt.pix_mp.num_planes=1;
        f->fmt.pix_mp.plane_fmt[0].bytesperline=mock_format==V4L2_PIX_FMT_P010?768:384;
        f->fmt.pix_mp.plane_fmt[0].sizeimage=mock_format==V4L2_PIX_FMT_P010?491520:147456;
    } else if(request==VIDIOC_G_SELECTION)
        ((struct v4l2_selection *)argument)->r=(struct v4l2_rect){0,0,320,240};
    else if(request==VIDIOC_S_FMT) {
        struct v4l2_format *f=argument;
        if(fail_format_once){fail_format_once=0;return q2_error(EIO);}
        if(!f->fmt.pix_mp.plane_fmt[0].bytesperline) {
            assert(f->fmt.pix_mp.height==240);
            mock_format=f->fmt.pix_mp.pixelformat;reported_format=0;
            f->fmt.pix_mp.height=256;
            f->fmt.pix_mp.plane_fmt[0].bytesperline=mock_format==V4L2_PIX_FMT_P010?768:384;
            f->fmt.pix_mp.plane_fmt[0].sizeimage=mock_format==V4L2_PIX_FMT_P010?491520:147456;
            if(fail_after_select){fail_after_select=0;fail_format_once=1;}
            return 0;
        }
        if(f->fmt.pix_mp.pixelformat!=mock_format)return q2_error(EINVAL);
        assert(f->fmt.pix_mp.height==512);
        unsigned sample=f->fmt.pix_mp.pixelformat==V4L2_PIX_FMT_P010?2:1;
        assert(f->fmt.pix_mp.plane_fmt[0].bytesperline==512*sample ||
               f->fmt.pix_mp.plane_fmt[0].bytesperline==640*sample);
        mock_format=f->fmt.pix_mp.pixelformat;
    } else assert(request==VIDIOC_REQBUFS || request==VIDIOC_QUERYCAP);
    return 0;
}
static int mock_open(struct q2_session **out) {
    int fd=(int)syscall(SYS_memfd_create,"q2-node-test",1);assert(fd>=0);
    struct q2_session *s=calloc(1,sizeof(*s));assert(s);
    s->fd=fd;s->backend=kernel;
    assert(!pthread_mutex_init(&s->mutex,NULL));
    s->wake=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK|EFD_SEMAPHORE);assert(s->wake>=0);
    pthread_mutex_lock(&q2_lock);assert(!q2_attach(fd,s));q2_node_open(s);pthread_mutex_unlock(&q2_lock);
    *out=s;return fd;
}
static void source_change(struct q2_session *s) {
    struct v4l2_event event={.type=V4L2_EVENT_SOURCE_CHANGE};
    uint32_t metadata[]={V4L2_EVENT_SRC_CH_RESOLUTION,0x51324d31,15,240,320,0,1,9};
    memcpy(event.u.data,metadata,sizeof(metadata));assert(!q2_source_event(s,&event));
}
struct worker { uint64_t token; int result,error; struct quest2_vidc_node_layout layout; };
static void *query_worker(void *opaque) {
    struct worker *w=opaque;
    w->result=quest2_vidc_node_get_layout(w->token,&w->layout);w->error=errno;return NULL;
}
static pthread_barrier_t scope_barrier;
static void *scope_worker(void *opaque) {
    struct worker *w=opaque;w->token=quest2_vidc_node_begin();assert(w->token);
    pthread_barrier_wait(&scope_barrier);
    struct q2_session *s;int fd=mock_open(&s);
    assert(!quest2_vidc_node_end(w->token) && s->node_token==w->token);
    assert(!quest2_vidc_node_configure(w->token,0,0,0));
    assert(!close(fd));return NULL;
}
int main(int argc,char **argv) {
    (void)argv;
    struct q2_session *a,*b;struct quest2_vidc_node_layout layout;
    assert(!quest2_vidc_node_begin() && errno==EOPNOTSUPP);
    q2_enabled=1;
    if(argc>1) {
        uint64_t missing=quest2_vidc_node_begin();int missing_fd=mock_open(&a);
        assert(!quest2_vidc_node_end(missing));
        assert(quest2_vidc_node_configure(missing,1,0x20000000,1)<0 && errno==ENOSYS);
        assert(!a->node_native && !a->node_usage_valid && !a->native_layout_for_usage);
        assert(!quest2_vidc_node_configure(missing,0,0,0) && !close(missing_fd));
        puts("NODE_SYMBOL: missing native callback rejected without mutating ByteBuffer state");return 0;
    }
    uint64_t token=quest2_vidc_node_begin();assert(token);
    assert(!quest2_vidc_node_begin() && errno==EBUSY);
    int fd=mock_open(&a);
    assert(quest2_vidc_node_configure(token,1,0x20000000,1)<0 && errno==ENODEV);
    int alias=dup(fd);assert(alias>=0 && a->refs==2);
    assert(!quest2_vidc_node_end(token) && a->node_bound);
    assert(quest2_vidc_node_end(token)<0 && errno==EINVAL && a->node_bound);
    assert(!quest2_vidc_node_configure(token,0,0,0));
    assert(quest2_vidc_node_configure(token,2,0,0)<0 && errno==EINVAL);
    assert(quest2_vidc_node_configure(token,1,0,2)<0 && errno==EINVAL);
    assert(quest2_vidc_node_configure(0,0,0,0)<0 && errno==EINVAL);
    assert(quest2_vidc_node_get_layout(token,NULL)<0 && errno==EINVAL);
    assert(!close(fd) && a->refs==1);
    assert(!quest2_vidc_node_configure(token,1,0x20000000,0));
    assert(a->node_native && !a->node_usage_valid && !a->node_usage && !a->native_layout);
    assert(quest2_vidc_node_get_layout(token,&layout)<0 && errno==EINVAL);
    assert(!quest2_vidc_node_configure(token,1,0x20000000,1));
    assert(quest2_vidc_node_get_layout(token,&layout)<0 && errno==EAGAIN);
    source_change(a);assert(a->source_changes==1 && a->layout_pending);
    assert(!quest2_vidc_node_configure(token,1,0,0));
    int before=layout_calls;
    assert(q2_native_layout(a,0)<0 && errno==EAGAIN && layout_calls==before);
    assert(!quest2_vidc_node_configure(token,1,0x20000000,1));
    assert(!quest2_vidc_node_get_layout(token,&layout));
    assert(layout.width==320 && layout.height==240 && layout.format==V4L2_PIX_FMT_NV12 &&
           layout.stride==512 && layout.scan==512 && layout.size==393216 && layout.usage==0x20000000);
    assert(last_usage==0x20000000 && a->refs==1);
    assert(fcntl(a->fd,F_GETFD)<0 && errno==EBADF); /* Previous operation pin is intentionally stale. */
    assert(!quest2_vidc_node_get_layout(token,&layout));
    struct v4l2_format format={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
    format.fmt.pix_mp.width=320;format.fmt.pix_mp.height=240;format.fmt.pix_mp.num_planes=1;
    format.fmt.pix_mp.pixelformat=Q2_PIX_FMT_P010_VENUS;
    a->queues[1].count=1;
    assert(q2_ioctl_forward(alias,VIDIOC_S_FMT,(uintptr_t)&format)<0 && errno==EBUSY);
    assert(a->layout_format==V4L2_PIX_FMT_NV12 && a->layout_stride==512 && mock_format==V4L2_PIX_FMT_NV12);
    a->queues[1].count=0;fail_format_once=1;
    assert(q2_ioctl_forward(alias,VIDIOC_S_FMT,(uintptr_t)&format)<0 && errno==EIO);
    assert(a->layout_format==V4L2_PIX_FMT_NV12 && a->layout_stride==512 && mock_format==V4L2_PIX_FMT_NV12);
    fail_after_select=1;
    assert(q2_ioctl_forward(alias,VIDIOC_S_FMT,(uintptr_t)&format)<0 && errno==EIO);
    assert(mock_format==V4L2_PIX_FMT_P010 && a->layout_pending && !a->layout_stride);
    assert(!quest2_vidc_node_get_layout(token,&layout));
    assert(!q2_ioctl_forward(alias,VIDIOC_S_FMT,(uintptr_t)&format));
    assert(!quest2_vidc_node_get_layout(token,&layout));
    assert(layout.format==V4L2_PIX_FMT_P010 && layout.stride==1024 && layout.size==786432);
    assert(format.fmt.pix_mp.pixelformat==Q2_PIX_FMT_P010_VENUS);
    before=layout_calls;
    assert(!q2_ioctl_forward(alias,VIDIOC_S_FMT,(uintptr_t)&format) && before==layout_calls);
    reported_format=V4L2_PIX_FMT_NV12;source_change(a);
    assert(mock_format==V4L2_PIX_FMT_P010 && a->layout_pending);
    assert(!quest2_vidc_node_get_layout(token,&layout));
    assert(mock_format==V4L2_PIX_FMT_NV12 && !reported_format);
    assert(a->layout_format==V4L2_PIX_FMT_NV12 && a->layout_stride==512);
    puts("NODE_P010: explicit and G_FMT-normalized format switch recomputes byte layout; busy/error leaves cache truthful; repeated format idempotent");
    struct rlimit old,limit;assert(!getrlimit(RLIMIT_NOFILE,&old));limit=old;limit.rlim_cur=64;
    assert(!setrlimit(RLIMIT_NOFILE,&limit));
    int fill[64],fill_count=0,extra;
    while((extra=(int)syscall(SYS_openat,AT_FDCWD,"/dev/null",O_RDONLY,0))>=0)fill[fill_count++]=extra;
    assert(errno==EMFILE && fill_count>0);
    assert(quest2_vidc_node_configure(token,1,0x20000001,1)<0 && errno==EMFILE && a->node_usage==0x20000000);
    assert(!setrlimit(RLIMIT_NOFILE,&old));while(fill_count)assert(!syscall(SYS_close,fill[--fill_count]));
    puts("NODE_BIND: one synchronous open, aliases and stale ioctl fd handled; unknown usage/pin failure rejected");

    uint64_t byte_token=quest2_vidc_node_begin();assert(byte_token && byte_token!=token);
    int byte_fd=mock_open(&b);assert(!quest2_vidc_node_end(byte_token));
    assert(!quest2_vidc_node_configure(byte_token,0,0xdeadbeef,1) && !b->node_native && !b->node_usage_valid);
    source_change(b);assert(!b->layout_pending && !q2_has_native_layout(b));
    before=layout_calls;assert(!q2_native_layout(b,0) && before==layout_calls);
    assert(quest2_vidc_node_get_layout(byte_token,&layout)<0 && errno==EINVAL);
    a->streaming=2;
    assert(!quest2_vidc_node_configure(token,1,0x20000000,1));
    assert(quest2_vidc_node_configure(token,0,0,0)<0 && errno==EBUSY);
    assert(quest2_vidc_node_configure(token,1,0x20000001,1)<0 && errno==EBUSY);
    a->streaming=0;a->queued=1;
    assert(quest2_vidc_node_configure(token,1,0x20000001,1)<0 && errno==EBUSY);a->queued=0;
    struct v4l2_requestbuffers buffers={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,.memory=V4L2_MEMORY_USERPTR,.count=2};
    assert(!q2_ioctl_forward(alias,VIDIOC_REQBUFS,(uintptr_t)&buffers));
    a->queues[1].slots[0].valid=1;
    assert(quest2_vidc_node_configure(token,1,0x20000001,1)<0 && errno==EBUSY);
    a->queues[1].slots[0].valid=0;
    assert(!quest2_vidc_node_configure(token,1,0x20000001,1));
    assert(quest2_vidc_node_get_layout(token,&layout)<0 && errno==EBUSY);
    buffers.count=0;
    assert(!q2_ioctl_forward(alias,VIDIOC_REQBUFS,(uintptr_t)&buffers));
    assert(!quest2_vidc_node_get_layout(token,&layout) && layout.stride==640 && layout.usage==0x20000001);
    assert(last_usage==0x20000001 && !b->node_native);
    source_change(a);assert(a->source_changes==3 && a->layout_pending && !a->layout_stride);
    assert(!quest2_vidc_node_get_layout(token,&layout) && layout.stride==640);
    assert(!close(byte_fd));
    assert(quest2_vidc_node_configure(byte_token,0,0,0)<0 && errno==ENODEV);
    puts("NODE_MODES: mixed ByteBuffer/native isolated; busy mutations refused, idempotence and empty CAPTURE DRC preserved");

    source_change(a);pause_layout=1;
    struct worker w={.token=token};pthread_t thread;
    assert(!pthread_create(&thread,NULL,query_worker,&w));
    pthread_mutex_lock(&gate);while(!layout_entered)pthread_cond_wait(&condition,&gate);pthread_mutex_unlock(&gate);
    assert(!close(alias)); /* Drop the final tracked alias while callback and fd pin remain live. */
    assert(quest2_vidc_node_configure(token,0,0,0)<0 && errno==ENODEV);
    struct q2_session *reused;uint64_t new_token=quest2_vidc_node_begin();assert(new_token!=token);
    int reused_fd=mock_open(&reused);assert(!quest2_vidc_node_end(new_token));
    assert(quest2_vidc_node_get_layout(token,&layout)<0 && errno==ENODEV);
    pthread_mutex_lock(&gate);layout_release=1;pthread_cond_broadcast(&condition);pthread_mutex_unlock(&gate);
    assert(!pthread_join(thread,NULL) && !w.result && w.layout.stride==640 && w.layout.usage==0x20000001);
    assert(!close(reused_fd) && !q2_fds);
    puts("NODE_LIFETIME: in-flight layout survives concurrent final close; descriptor reuse cannot revive old token");

    token=quest2_vidc_node_begin();assert(token);
    assert(quest2_vidc_node_end(token)<0 && errno==EPROTO && !q2_node_scope.token);
    token=quest2_vidc_node_begin();fd=mock_open(&a);
    assert(quest2_vidc_node_end(token+1)<0 && errno==EINVAL && !q2_node_scope.token && !a->node_token);
    assert(!close(fd));
    token=quest2_vidc_node_begin();fd=mock_open(&a);byte_fd=mock_open(&b);
    assert(quest2_vidc_node_end(token)<0 && errno==EPROTO && !a->node_token && !b->node_token);
    assert(!close(fd) && !close(byte_fd));
    token=quest2_vidc_node_begin();fd=mock_open(&a);assert(!close(fd));
    assert(quest2_vidc_node_end(token)<0 && errno==ENODEV);
    assert(!pthread_barrier_init(&scope_barrier,NULL,2));
    struct worker workers[2]={0};pthread_t threads[2];
    for(unsigned i=0;i<2;i++)assert(!pthread_create(&threads[i],NULL,scope_worker,&workers[i]));
    for(unsigned i=0;i<2;i++)assert(!pthread_join(threads[i],NULL));
    assert(workers[0].token && workers[1].token && workers[0].token!=workers[1].token && !q2_fds);
    assert(!pthread_barrier_destroy(&scope_barrier));
    q2_next_node_token=UINT64_MAX;
    assert(!quest2_vidc_node_begin() && errno==EOVERFLOW && !q2_node_scope.token);
    puts("NODE_SCOPE: failed/multiple/closed opens rejected, nested scope retained, per-thread tokens unique, wrap refused");
    return 0;
}
