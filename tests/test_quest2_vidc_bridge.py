"""Execute the actual process-local VIDC bridge against a truthful fake kernel.

Fixtures establish adapter semantics, not decoding or device compatibility.
"""
from pathlib import Path
import json
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'probe/video/quest2-vidc-bridge.c'
HARNESS = r'''
#define Q2_BRIDGE_TEST 1
#include "SOURCE"
#include <assert.h>
static int calls, fail_source, fail_native, last_value, fail_control, minimum_queries;
static unsigned long last_request;
static int kernel(int fd, unsigned long request, void *arg) {
    assert(fd == 91); calls++; last_request=request;
    if(request==VIDIOC_SUBSCRIBE_EVENT) {
        assert(((struct v4l2_event_subscription *)arg)->type==V4L2_EVENT_SOURCE_CHANGE);
        if(fail_source) return q2_error(ENOTTY);
        return 0;
    }
    if(request==VIDIOC_G_CTRL) {
        if(fail_control) return q2_error(fail_control);
        struct v4l2_control *c=arg;
        c->value=c->id==V4L2_CID_MPEG_VIDEO_H264_LEVEL?18:17; return 0;
    }
    if(request==VIDIOC_S_CTRL) { last_value=((struct v4l2_control *)arg)->value;return 0; }
    if(request==VIDIOC_QUERYCTRL) {
        struct v4l2_queryctrl *q=arg;q->minimum=0;q->maximum=19;q->default_value=18;return 0;
    }
    if(request==VIDIOC_QUERYMENU) { last_value=(int)((struct v4l2_querymenu *)arg)->index;return 0; }
    if(request==VIDIOC_DQEVENT) {
        struct v4l2_event *e=arg;memset(e,0,sizeof(*e));e->type=V4L2_EVENT_SOURCE_CHANGE;
        e->u.src_change.changes=V4L2_EVENT_SRC_CH_RESOLUTION;return 0;
    }
    if(fail_native) return q2_error(fail_native);
    return 0;
}
static int minimum(struct q2_session *s,uint32_t id,int *value) {
    assert(s->fd==91);minimum_queries++;
    *value=id==V4L2_CID_MIN_BUFFERS_FOR_OUTPUT?29:31;return 0;
}
int main(void) {
    struct q2_session s={.fd=91,.backend=kernel};
    s.wake=eventfd(0,EFD_NONBLOCK|EFD_SEMAPHORE);assert(s.wake>=0);
    struct v4l2_event_subscription sub={.type=Q2_RECONFIG};
    fail_source=1;
    assert(q2_dispatch(&s,VIDIOC_SUBSCRIBE_EVENT,&sub)==-1 && errno==ENOTTY && !s.subscriptions);
    fail_source=0;
    const uint32_t events[]={Q2_FLUSH_DONE,Q2_RECONFIG,Q2_RELEASE_REFERENCE,Q2_RELEASE_UNQUEUED,Q2_SYS_ERROR,Q2_OVERLOAD,Q2_UNSUPPORTED};
    for(unsigned i=0;i<sizeof(events)/sizeof(events[0]);i++) {sub.type=events[i];assert(!q2_dispatch(&s,VIDIOC_SUBSCRIBE_EVENT,&sub));}
    assert(s.subscriptions==(2|8|32|64|128|256|1024));
    int before=calls;sub.type=Q2_EVENT_BASE+9;
    assert(q2_dispatch(&s,VIDIOC_SUBSCRIBE_EVENT,&sub)==-1 && errno==EOPNOTSUPP && calls==before);
    sub.type=Q2_FLUSH_DONE;sub.flags=1;assert(q2_dispatch(&s,VIDIOC_SUBSCRIBE_EVENT,&sub)==-1 && errno==EOPNOTSUPP);sub.flags=0;
    puts("SUBSCRIBE: real source subscription failure preserved; seven known events registered; unknown rejected");

    struct v4l2_decoder_cmd command={.cmd=Q2_CMD_FLUSH,.flags=3};
    assert(!q2_dispatch(&s,VIDIOC_DECODER_CMD,&command));
    struct pollfd ready={.fd=s.wake,.events=POLLIN};assert(poll(&ready,1,0)==1);
    struct v4l2_event e={0};assert(!q2_dispatch(&s,VIDIOC_DQEVENT,&e));
    uint32_t flags;memcpy(&flags,e.u.data,4);assert(e.type==Q2_FLUSH_DONE && flags==3);
    assert(poll(&ready,1,0)==0);
    s.queued=1;assert(q2_dispatch(&s,VIDIOC_DECODER_CMD,&command)==-1 && errno==EOPNOTSUPP && !s.head);s.queued=0;
    s.streaming=1;assert(q2_dispatch(&s,VIDIOC_DECODER_CMD,&command)==-1 && errno==EOPNOTSUPP && !s.head);s.streaming=0;
    assert(!q2_enqueue(&s,Q2_FLUSH_DONE,&command.flags,1));
    assert(!q2_dispatch(&s,VIDIOC_UNSUBSCRIBE_EVENT,&sub) && !s.head && poll(&ready,1,0)==0);
    puts("BROKER: real eventfd readiness; idle-empty flush completion; active flush refused; unsubscribe purges delivery");

    struct v4l2_control ctrl={.id=Q2_CTRL_BASE+109,.value=0};before=calls;
    assert(q2_dispatch(&s,VIDIOC_S_CTRL,&ctrl)==-1 && errno==EOPNOTSUPP && calls==before);
    ctrl.id=V4L2_CID_MIN_BUFFERS_FOR_OUTPUT;assert(!q2_dispatch(&s,VIDIOC_G_CTRL,&ctrl));assert(ctrl.value==17 && last_request==VIDIOC_G_CTRL);
    fail_control=EAGAIN;s.query_minimum=minimum;
    assert(!q2_dispatch(&s,VIDIOC_G_CTRL,&ctrl) && ctrl.value==29 && minimum_queries==1);
    ctrl.id=V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
    assert(!q2_dispatch(&s,VIDIOC_G_CTRL,&ctrl) && ctrl.value==31 && minimum_queries==2);
    fail_control=EINVAL;assert(q2_dispatch(&s,VIDIOC_G_CTRL,&ctrl)==-1 && errno==EINVAL && minimum_queries==2);
    fail_control=0;
    ctrl.id=V4L2_CID_MPEG_VIDEO_H264_LEVEL;ctrl.value=19;assert(!q2_dispatch(&s,VIDIOC_S_CTRL,&ctrl) && last_value==18 && ctrl.value==19);
    ctrl.value=18;before=calls;assert(q2_dispatch(&s,VIDIOC_S_CTRL,&ctrl)==-1 && errno==EINVAL && calls==before);
    assert(!q2_dispatch(&s,VIDIOC_G_CTRL,&ctrl) && ctrl.value==19);
    struct v4l2_queryctrl query={.id=V4L2_CID_MPEG_VIDEO_H264_LEVEL};assert(!q2_dispatch(&s,VIDIOC_QUERYCTRL,&query) && query.maximum==20 && query.default_value==19);
    struct v4l2_querymenu menu={.id=V4L2_CID_MPEG_VIDEO_H264_LEVEL,.index=20};assert(!q2_dispatch(&s,VIDIOC_QUERYMENU,&menu) && last_value==19 && menu.index==20);
    puts("CONTROL: real minimum buffers preserved; downstream H264 enum mapped; unknown/private controls refused");

    struct v4l2_requestbuffers req={.memory=V4L2_MEMORY_USERPTR,.count=4};before=calls;
    assert(q2_dispatch(&s,VIDIOC_REQBUFS,&req)==-1 && errno==EOPNOTSUPP && calls==before);
    struct v4l2_buffer buffer={.memory=V4L2_MEMORY_USERPTR};
    assert(q2_dispatch(&s,VIDIOC_QBUF,&buffer)==-1 && errno==EOPNOTSUPP && !s.queued);
    buffer.memory=V4L2_MEMORY_DMABUF;assert(!q2_dispatch(&s,VIDIOC_QBUF,&buffer) && s.queued==1);
    assert(!q2_dispatch(&s,VIDIOC_DQBUF,&buffer) && !s.queued);
    fail_native=EIO;struct v4l2_capability cap;
    assert(q2_dispatch(&s,VIDIOC_QUERYCAP,&cap)==-1 && errno==EIO);
    assert(!q2_dispatch(&s,VIDIOC_DQEVENT,&e) && e.type==Q2_SYS_ERROR);fail_native=0;
    assert(q2_dispatch(&s,VIDIOC_DQEVENT,&e)==-1 && errno==EOPNOTSUPP && !s.head);
    puts("BUFFERS: USERPTR not fabricated; native DMABUF passed through; actual EIO event delivered; unknown reconfig metadata rejected");
    q2_clear(&s);close(s.wake);return 0;
}
'''

FORWARD_HARNESS = r'''
#include "SOURCE"
#include <assert.h>
#include <sys/resource.h>
static int original,closed_original,backend_calls;
static int kernel(int fd,unsigned long request,void *arg) {
    (void)arg;assert(request==VIDIOC_QUERYCAP);backend_calls++;
    assert(!pthread_mutex_trylock(&q2_lock));pthread_mutex_unlock(&q2_lock);
    assert((closed_original || fd!=original) && fcntl(fd,F_GETFD)>=0 && (fcntl(fd,F_GETFL)&O_NONBLOCK));
    if(!closed_original) {assert(!close(original));closed_original=1;}
    /* The real operation file description remains pinned after close(original). */
    assert(fcntl(fd,F_GETFD)>=0);return 0;
}
static struct q2_session *attach(int fd,q2_backend backend) {
    struct q2_session *s=calloc(1,sizeof(*s));assert(s);
    s->fd=fd;s->backend=backend;assert(!pthread_mutex_init(&s->mutex,NULL));
    s->wake=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK|EFD_SEMAPHORE);assert(s->wake>=0);
    assert(!q2_attach(fd,s));return s;
}
static int aux_main,aux_fail,aux_req,aux_queries;
static int aux_kernel(int fd,request_t req,...) {
    va_list ap;va_start(ap,req);void *arg=va_arg(ap,void*);va_end(ap);
    assert(!pthread_mutex_trylock(&q2_lock));pthread_mutex_unlock(&q2_lock);
    if(req==VIDIOC_G_FMT) {
        struct v4l2_format *f=arg;f->fmt.pix_mp.width=320;f->fmt.pix_mp.height=240;
        f->fmt.pix_mp.pixelformat=f->type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE?V4L2_PIX_FMT_H264:V4L2_PIX_FMT_NV12;return 0;
    }
    if(req==VIDIOC_S_FMT) {if(aux_fail)return q2_error(aux_fail);return 0;}
    if(req==VIDIOC_REQBUFS) {
        struct v4l2_requestbuffers *b=arg;assert(fd!=aux_main && b->count==1 && b->memory==V4L2_MEMORY_DMABUF);
        b->count=4;aux_req++;return 0;
    }
    if(req==VIDIOC_G_CTRL) {
        struct v4l2_control *c=arg;assert(fd!=aux_main);aux_queries++;
        c->value=c->id==V4L2_CID_MIN_BUFFERS_FOR_OUTPUT?17:23;return 0;
    }
    assert(0);return -1;
}
int main(void) {
    int p[2];assert(!pipe(p));original=p[0];
    struct q2_session *s=attach(original,kernel);int alias=dup(original);assert(alias>=0 && s->refs==2);
    struct v4l2_capability cap={0};
    assert(!q2_ioctl_forward(original,VIDIOC_QUERYCAP,(uintptr_t)&cap));
    assert(closed_original && backend_calls==1 && q2_lookup(original)==NULL && q2_lookup(alias)==s && s->refs==1);
    assert(!q2_ioctl_forward(alias,VIDIOC_QUERYCAP,(uintptr_t)&cap) && backend_calls==2);
    puts("FORWARD: real backend runs outside registry lock; operation fd and session survive original close; nonblocking enforced");

    s->subscriptions=q2_event_bit(Q2_FLUSH_DONE);uint32_t flags=3;assert(!q2_enqueue(s,Q2_FLUSH_DONE,&flags,1));
    struct rlimit old,limit;assert(!getrlimit(RLIMIT_NOFILE,&old));limit=old;limit.rlim_cur=64;assert(!setrlimit(RLIMIT_NOFILE,&limit));
    int fill[64],n=0,fd;
    while((fd=(int)syscall(SYS_openat,AT_FDCWD,"/dev/null",O_RDONLY,0))>=0) fill[n++]=fd;
    assert(errno==EMFILE && n>0);assert(!syscall(SYS_close,fill[--n]));
    struct pollfd events[2]={{.fd=alias,.events=POLLPRI},{.fd=alias,.events=POLLPRI}};
    assert(q2_poll(events,2,0,NULL,NULL,0)==-1 && errno==EMFILE && s->head);
    fd=(int)syscall(SYS_dup,s->wake);assert(fd>=0);assert(!syscall(SYS_close,fd));
    assert(!setrlimit(RLIMIT_NOFILE,&old));while(n)assert(!syscall(SYS_close,fill[--n]));
    assert(q2_poll(events,2,0,NULL,NULL,0)==2 && (events[0].revents&POLLPRI));
    puts("POLL: partial duplicate EMFILE reported; duplicates cleaned; queued event remains observable");
    assert(!close(alias) && !close(p[1]) && !q2_fds);

    char name[]="/tmp/q2-aux-XXXXXX";aux_main=mkstemp(name);assert(aux_main>=0);
    s=attach(aux_main,q2_real_ioctl);q2_next_ioctl=aux_kernel;q2_aux_enabled=1;int answer=0;
    assert(!q2_aux_minimum(s,V4L2_CID_MIN_BUFFERS_FOR_OUTPUT,&answer) && answer==17);
    assert(!q2_aux_minimum(s,V4L2_CID_MIN_BUFFERS_FOR_CAPTURE,&answer) && answer==23);
    assert(aux_req==2 && aux_queries==2);aux_fail=ENOSPC;
    assert(q2_aux_minimum(s,V4L2_CID_MIN_BUFFERS_FOR_OUTPUT,&answer)==-1 && errno==ENOSPC && aux_req==2);
    assert(!close(aux_main));assert(!unlink(name));
    puts("AUX: OUTPUT and CAPTURE use real queried values, not request_count; real S_FMT failure preserved");return 0;
}
'''

BUFFER_HARNESS = r'''
#define Q2_BRIDGE_TEST 1
#include "SOURCE"
#include <assert.h>
static int calls,last_pin=-1,fail_qbuf,queue_count;
static struct v4l2_plane stored;
static int kernel(int fd,unsigned long request,void *arg) {
    assert(fd==91);calls++;
    if(request==VIDIOC_REQBUFS) {
        struct v4l2_requestbuffers *r=arg;assert(r->memory==V4L2_MEMORY_DMABUF);
        if(r->count) r->count=3;
        return 0;
    }
    if(request==VIDIOC_QBUF || request==VIDIOC_PREPARE_BUF) {
        struct v4l2_buffer *b=arg;assert(b->memory==V4L2_MEMORY_DMABUF && b->length==1);
        assert(b->m.planes->m.fd>=0 && !b->m.planes->data_offset);
        for(unsigned i=0;i<11;i++) assert(!b->m.planes->reserved[i]);
        char actual[7]={0};assert(pread(b->m.planes->m.fd,actual,6,0)==6 && !strcmp(actual,"actual"));
        last_pin=b->m.planes->m.fd;stored=*b->m.planes;
        if(fail_qbuf) return q2_error(fail_qbuf);
        if(request==VIDIOC_QBUF) {assert(b->flags&V4L2_BUF_FLAG_LAST);queue_count++;}
        return 0;
    }
    if(request==VIDIOC_DQBUF) {
        struct v4l2_buffer *b=arg;assert(b->memory==V4L2_MEMORY_DMABUF && queue_count);
        b->index=0;*b->m.planes=stored;b->m.planes->bytesused=6;b->timestamp.tv_sec=42;
        b->flags=V4L2_BUF_FLAG_LAST;queue_count--;return 0;
    }
    if(request==VIDIOC_STREAMOFF) {queue_count=0;return 0;}
    assert(0);return -1;
}
int main(void) {
    struct q2_session s={.fd=91,.backend=kernel};s.wake=eventfd(0,EFD_NONBLOCK|EFD_SEMAPHORE);assert(s.wake>=0);
    struct v4l2_requestbuffers r={.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,.memory=V4L2_MEMORY_USERPTR,.count=2};
    assert(!q2_dispatch(&s,VIDIOC_REQBUFS,&r) && r.memory==V4L2_MEMORY_USERPTR && r.count==3 && s.queues[0].count==3);
    int dma=(int)syscall(SYS_memfd_create,"q2-test-actual-buffer",1);assert(dma>=0);
    assert(!ftruncate(dma,4096) && pwrite(dma,"actual",6,0)==6);
    struct v4l2_plane p={.length=4096,.bytesused=6,.m.userptr=0x123456781234ULL};
    p.reserved[0]=(unsigned)dma;p.reserved[3]=123;p.reserved[4]=456;
    struct v4l2_buffer b={.type=r.type,.memory=V4L2_MEMORY_USERPTR,.length=1,.m.planes=&p,.flags=Q2_BUF_EOS};
    int before=calls;p.reserved[1]=1;assert(q2_dispatch(&s,VIDIOC_QBUF,&b)<0 && errno==EOPNOTSUPP && calls==before);p.reserved[1]=0;
    b.length=2;assert(q2_dispatch(&s,VIDIOC_QBUF,&b)<0 && errno==EOPNOTSUPP && calls==before);b.length=1;
    b.flags=Q2_BUF_CODECCONFIG;assert(q2_dispatch(&s,VIDIOC_QBUF,&b)<0 && errno==EOPNOTSUPP && calls==before);b.flags=Q2_BUF_EOS;
    p.length=4097;assert(q2_dispatch(&s,VIDIOC_QBUF,&b)<0 && errno==EINVAL && calls==before);p.length=4096;
    assert(!q2_dispatch(&s,VIDIOC_PREPARE_BUF,&b) && !s.queued);int prepared=last_pin;
    assert(!q2_dispatch(&s,VIDIOC_QBUF,&b) && s.queued==1);assert(fcntl(prepared,F_GETFD)<0 && errno==EBADF);
    int queued=last_pin;assert(queued!=dma && fcntl(queued,F_GETFD)>=0);assert(!close(dma));
    assert(!q2_dispatch(&s,VIDIOC_DQBUF,&b) && !s.queued && b.memory==V4L2_MEMORY_USERPTR && b.flags==Q2_BUF_EOS);
    assert(b.timestamp.tv_sec==42 && p.bytesused==6 && p.m.userptr==0x123456781234ULL && p.reserved[0]==(unsigned)dma && p.reserved[3]==123 && p.reserved[4]==456);
    assert(fcntl(queued,F_GETFD)<0 && errno==EBADF);
    puts("DMABUF: actual backing fd/bytes reach backend; returned count honored; opaque donor cookie preserved; lifetime pinned through real DQBUF");
    dma=(int)syscall(SYS_memfd_create,"q2-error-buffer",1);assert(dma>=0 && !ftruncate(dma,4096) && pwrite(dma,"actual",6,0)==6);p.reserved[0]=(unsigned)dma;
    fail_qbuf=EIO;assert(q2_dispatch(&s,VIDIOC_QBUF,&b)<0 && errno==EIO && !s.queued);
    assert(fcntl(last_pin,F_GETFD)<0 && errno==EBADF);fail_qbuf=0;
    assert(!q2_dispatch(&s,VIDIOC_QBUF,&b));queued=last_pin;uint32_t type=r.type;
    assert(!q2_dispatch(&s,VIDIOC_STREAMOFF,&type) && !s.queued && fcntl(queued,F_GETFD)<0 && errno==EBADF);
    r.count=0;assert(!q2_dispatch(&s,VIDIOC_REQBUFS,&r) && !s.queues[0].slots);
    assert(!close(dma));q2_queue_clear(&s,&s.queues[0]);q2_queue_clear(&s,&s.queues[1]);q2_clear(&s);close(s.wake);
    puts("BUFFER_ERRORS: unsupported offsets/planes/flags rejected; actual fd size enforced; actual QBUF error retained; STREAMOFF/REQBUFS release ownership");return 0;
}
'''


class QuestVidcBridge(unittest.TestCase):
    def test_real_buffer_fd_translation_and_lifecycle(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            (d/'test.c').write_text(BUFFER_HARNESS.replace('"SOURCE"', '"' + str(SOURCE) + '"'))
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            str(d/'test.c'), '-ldl', '-pthread', '-o', str(d/'test')], check=True)
            p = subprocess.run([str(d/'test')], capture_output=True, text=True, timeout=15)
            self.assertEqual(p.returncode, 0, p.stdout+p.stderr)
            self.assertIn('DMABUF:', p.stdout)
            self.assertIn('BUFFER_ERRORS:', p.stdout)
            print(p.stdout, end='')

    def test_real_forward_registry_poll_and_auxiliary_query(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            (d/'test.c').write_text(FORWARD_HARNESS.replace('"SOURCE"', '"' + str(SOURCE) + '"'))
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g',
                            str(d/'test.c'), '-ldl', '-pthread', '-o', str(d/'test')], check=True)
            p = subprocess.run([str(d/'test')], capture_output=True, text=True, timeout=15)
            self.assertEqual(p.returncode, 0, p.stdout+p.stderr)
            for marker in ('FORWARD:', 'POLL:', 'AUX:'):
                self.assertIn(marker, p.stdout)
            print(p.stdout, end='')

    def test_actual_broker_and_error_contracts(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            (d/'test.c').write_text(HARNESS.replace('"SOURCE"', '"' + str(SOURCE) + '"'))
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-g',
                            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                            str(d/'test.c'), '-ldl', '-pthread', '-o', str(d/'test')], check=True)
            p = subprocess.run([str(d/'test')], capture_output=True, text=True)
            self.assertEqual(p.returncode, 0, p.stdout+p.stderr)
            for marker in ('SUBSCRIBE:', 'BROKER:', 'CONTROL:', 'BUFFERS:'):
                self.assertIn(marker, p.stdout)
            print(p.stdout, end='')

    def test_preload_leaves_unmanaged_process_io_unchanged(self):
        with tempfile.TemporaryDirectory() as td:
            library = Path(td)/'bridge.so'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O2',
                            '-fPIC', '-shared', str(SOURCE), '-ldl', '-pthread', '-o', str(library)], check=True)
            for enabled in ('0', '1'):
                env = dict(os.environ, LD_PRELOAD=str(library), QUEST2_VIDC_BRIDGE=enabled)
                p = subprocess.run(['python3', '-c',
                    'import os,select; r,w=os.pipe(); d=os.dup(w); os.write(d,b"actual"); '
                    'p=select.poll();p.register(r,select.POLLIN);assert p.poll(100); '
                    'assert os.read(r,6)==b"actual";os.close(d);os.close(r);os.close(w);print("UNMANAGED_IO_OK")'],
                    env=env, capture_output=True, text=True)
                self.assertEqual(p.returncode, 0, p.stderr)
                self.assertEqual(p.stdout.strip(), 'UNMANAGED_IO_OK')

    def test_capability_boundary_is_explicit(self):
        data = json.loads((ROOT/'probe/video/quest2-vidc-bridge-capabilities.json').read_text())
        self.assertFalse(data['hardware_decoding_verified'])
        self.assertEqual(data['decode_owner'], 'unchanged Quest OMX donor and real Venus backend')
        self.assertTrue(any('USERPTR_ION_to_DMABUF' in x for x in data['implemented']))
        self.assertIn('USERPTR multiple planes or nonzero allocation/data offsets', data['not_implemented'])


if __name__ == '__main__':
    unittest.main()
