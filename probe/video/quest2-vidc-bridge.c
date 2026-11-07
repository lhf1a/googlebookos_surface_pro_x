/* SPDX-License-Identifier: MIT
 * Explicit, process-local Quest VIDC ABI adapter. This is NOT a decoder.
 * The unchanged donor drives OMX; Venus performs all actual decoding.
 *
 * Candidate: real event broker, controls and offset-zero ION/dma-buf queues.
 * Unsupported private controls, active flush, reference-buffer registration,
 * multiple planes/offset layouts fail EOPNOTSUPP rather than fake success.
 * See tests/test_quest2_vidc_bridge.py and quest2-vidc-bridge-capabilities.json.
 */
#define _GNU_SOURCE
#define BIONIC_IOCTL_NO_SIGNEDNESS_OVERLOAD
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define Q2_EVENT_BASE 0x08001000U
#define Q2_FLUSH_DONE (Q2_EVENT_BASE + 1)
#define Q2_RECONFIG (Q2_EVENT_BASE + 3)
#define Q2_SYS_ERROR (Q2_EVENT_BASE + 5)
#define Q2_RELEASE_REFERENCE (Q2_EVENT_BASE + 6)
#define Q2_RELEASE_UNQUEUED (Q2_EVENT_BASE + 7)
#define Q2_OVERLOAD (Q2_EVENT_BASE + 8)
#define Q2_UNSUPPORTED (Q2_EVENT_BASE + 10)
#define Q2_CTRL_BASE 0x00992000U
#define Q2_CMD_FLUSH 4U
#define Q2_CMD_CONTINUE 5U
#define Q2_BUF_EOS 0x02000000U
#define Q2_BUF_CODECCONFIG 0x00020000U
#define Q2_BUF_DATA_CORRUPT 0x00400000U

#ifdef __ANDROID__
typedef int request_t;
#else
typedef unsigned long request_t;
#endif
typedef int (*q2_backend)(int, unsigned long, void *);
struct q2_event_node { struct v4l2_event event; struct q2_event_node *next; };
struct q2_buffer_slot { struct v4l2_plane donor; int pin,valid,queued; };
struct q2_queue { unsigned count; int userptr; struct q2_buffer_slot *slots; };
struct q2_session {
    int fd, wake;
    pthread_mutex_t mutex;
    unsigned subscriptions, refs, streaming, queued;
    int source_subscribed;
    uint32_t sequence;
    struct q2_event_node *head, *tail;
    q2_backend backend;
    int (*query_minimum)(struct q2_session *, uint32_t, int *);
    struct q2_queue queues[2];
};

static int q2_error(int error) { errno = error; return -1; }
static unsigned q2_event_bit(uint32_t type) {
    switch (type) {
    case Q2_FLUSH_DONE: case Q2_RECONFIG: case Q2_SYS_ERROR:
    case Q2_RELEASE_REFERENCE: case Q2_RELEASE_UNQUEUED:
    case Q2_OVERLOAD: case Q2_UNSUPPORTED:
        return 1U << (type - Q2_EVENT_BASE);
    default: return 0;
    }
}
static int q2_enqueue(struct q2_session *s, uint32_t type, const uint32_t *data, size_t words) {
    if (!(s->subscriptions & q2_event_bit(type))) return 0;
    if (words > sizeof(((struct v4l2_event *)0)->u.data) / sizeof(uint32_t)) return q2_error(EINVAL);
    struct q2_event_node *n = calloc(1, sizeof(*n));
    if (!n) return -1;
    n->event.type = type; n->event.sequence = s->sequence++;
    clock_gettime(CLOCK_MONOTONIC, &n->event.timestamp);
    if (words) memcpy(n->event.u.data, data, words * sizeof(uint32_t));
    uint64_t one = 1;
    if (write(s->wake, &one, sizeof(one)) != sizeof(one)) { free(n); return -1; }
    if (s->tail) s->tail->next = n; else s->head = n;
    s->tail = n;
    return 0;
}
static int q2_pop(struct q2_session *s, struct v4l2_event *event) {
    if (!s->head) return q2_error(EAGAIN);
    struct q2_event_node *n = s->head; s->head = n->next;
    if (!s->head) s->tail = NULL;
    *event = n->event; free(n);
    event->pending = 0;
    for (n = s->head; n; n = n->next) event->pending++;
    uint64_t one; while (syscall(SYS_read, s->wake, &one, sizeof(one)) < 0 && errno == EINTR) {}
    return 0;
}
static void q2_clear(struct q2_session *s) {
    struct v4l2_event event;
    while (s->head) (void)q2_pop(s, &event);
}
static int q2_subscribe(struct q2_session *s, struct v4l2_event_subscription *sub, int remove) {
    unsigned bit = q2_event_bit(sub->type);
    if (!bit) return q2_error(EOPNOTSUPP);
    if (sub->id || sub->flags) return q2_error(EOPNOTSUPP);
    if (remove) {
        s->subscriptions &= ~bit;
        /* Remove queued deliveries for this exact subscription. */
        struct q2_event_node **at = &s->head; s->tail = NULL;
        while (*at) {
            struct q2_event_node *n = *at;
            if (n->event.type == sub->type) {
                *at = n->next; free(n); uint64_t one;
                while (syscall(SYS_read, s->wake, &one, sizeof(one)) < 0 && errno == EINTR) {}
            } else { s->tail = n; at = &n->next; }
        }
        return 0;
    }
    if (sub->type == Q2_RECONFIG && !s->source_subscribed) {
        struct v4l2_event_subscription native = { .type = V4L2_EVENT_SOURCE_CHANGE };
        if (s->backend(s->fd, VIDIOC_SUBSCRIBE_EVENT, &native)) return -1;
        s->source_subscribed = 1;
    }
    s->subscriptions |= bit;
    return 0;
}

/* The downstream level enum inserts UNKNOWN=18; modern Linux does not.
 * No driver capability is invented: the translated request still goes to it. */
static int q2_level(int value, int to_native) {
    if (to_native) {
        if (value == 18 || value < 0 || value > 20) return -1;
        return value > 18 ? value - 1 : value;
    }
    if (value < 0 || value > 19) return -1;
    return value >= 18 ? value + 1 : value;
}
static int q2_private_control(uint32_t id) { return id >= Q2_CTRL_BASE && id < Q2_CTRL_BASE + 0x1000; }
static int q2_ctrl(struct q2_session *s, unsigned long request, struct v4l2_control *ctrl) {
    if (q2_private_control(ctrl->id)) return q2_error(EOPNOTSUPP);
    struct v4l2_control native = *ctrl;
    if (ctrl->id == V4L2_CID_MPEG_VIDEO_H264_LEVEL && request == VIDIOC_S_CTRL) {
        native.value = q2_level(ctrl->value, 1);
        if (native.value < 0) return q2_error(EINVAL);
    }
    int result = s->backend(s->fd, request, &native);
    if (result && errno == EAGAIN && request == VIDIOC_G_CTRL && s->query_minimum &&
        (ctrl->id == V4L2_CID_MIN_BUFFERS_FOR_OUTPUT || ctrl->id == V4L2_CID_MIN_BUFFERS_FOR_CAPTURE)) {
        result = s->query_minimum(s, ctrl->id, &native.value);
    }
    if (result) return result;
    if (ctrl->id == V4L2_CID_MPEG_VIDEO_H264_LEVEL) {
        native.value = q2_level(native.value, 0);
        if (native.value < 0) return q2_error(EOVERFLOW);
    }
    *ctrl = native; return 0;
}
static int q2_queue_index(uint32_t type) {
    if(type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) return 0;
    if(type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) return 1;
    return -1;
}
static void q2_queue_clear(struct q2_session *s,struct q2_queue *q) {
    for(unsigned i=0;i<q->count;i++) {
        struct q2_buffer_slot *slot=&q->slots[i];
        if(slot->valid && slot->pin>=0) (void)syscall(SYS_close,slot->pin);
        if(slot->queued && s->queued) s->queued--;
    }
    free(q->slots);memset(q,0,sizeof(*q));
}
static int q2_reqbufs(struct q2_session *s,struct v4l2_requestbuffers *request) {
    int index=q2_queue_index(request->type);if(index<0) return q2_error(EOPNOTSUPP);
    struct q2_queue *q=&s->queues[index];
    if(q->userptr && request->memory!=V4L2_MEMORY_USERPTR) return q2_error(EINVAL);
    if(request->memory!=V4L2_MEMORY_USERPTR) return s->backend(s->fd,VIDIOC_REQBUFS,request);
    struct v4l2_requestbuffers native=*request;native.memory=V4L2_MEMORY_DMABUF;
    if(s->backend(s->fd,VIDIOC_REQBUFS,&native)) return -1;
    q2_queue_clear(s,q);
    if(native.count) {
        q->slots=calloc(native.count,sizeof(*q->slots));
        if(!q->slots) {
            int error=errno;struct v4l2_requestbuffers release=native;release.count=0;
            int r=s->backend(s->fd,VIDIOC_REQBUFS,&release);
            fprintf(stderr,"Q2BRIDGE failed buffer metadata allocation; release result=%d errno=%d\n",r,errno);
            return q2_error(error);
        }
    }
    q->count=native.count;q->userptr=1;native.memory=V4L2_MEMORY_USERPTR;*request=native;
    return 0;
}
/* Qualcomm USERPTR actually describes an ION dma-buf: reserved[0] is its fd,
 * reserved[1] is the allocation offset; userptr is a completion cookie. Keep
 * the original cookie and a real fd reference, not a copied/decoded fake frame.
 * Offset-zero and one plane are deliberately the only implemented layout:
 * current Venus capture submits the allocation base, not data_offset. */
static int q2_user_buffer(struct q2_session *s,unsigned long request,struct v4l2_buffer *buffer) {
    int index=q2_queue_index(buffer->type);if(index<0) return q2_error(EOPNOTSUPP);
    struct q2_queue *q=&s->queues[index];
    if(!q->userptr || !q->slots) return q2_error(EINVAL);
    if(!buffer->m.planes) return q2_error(EFAULT);
    if(buffer->length!=1) return q2_error(EOPNOTSUPP);
    struct v4l2_buffer native=*buffer;struct v4l2_plane plane={0};
    native.memory=V4L2_MEMORY_DMABUF;native.m.planes=&plane;
    int pin=-1;struct q2_buffer_slot *slot=NULL;
    if(request!=VIDIOC_DQBUF) {
        if(buffer->index>=q->count) return q2_error(EINVAL);
        slot=&q->slots[buffer->index];
        if(request==VIDIOC_QBUF || request==VIDIOC_PREPARE_BUF) {
            struct v4l2_plane *donor=buffer->m.planes;
            if(slot->queued) return q2_error(EBUSY);
            if(donor->reserved[1] || donor->data_offset) return q2_error(EOPNOTSUPP);
            if(donor->reserved[0]>INT_MAX || !donor->length || donor->bytesused>donor->length) return q2_error(EINVAL);
            /* CODECCONFIG collides with the upstream timestamp-source field;
             * unknown private flags may not silently become native flags. */
            uint32_t allowed=0x00007fffU|V4L2_BUF_FLAG_LAST|Q2_BUF_EOS;
            if(buffer->flags&~allowed) return q2_error(EOPNOTSUPP);
            pin=(int)syscall(SYS_fcntl,(int)donor->reserved[0],F_DUPFD_CLOEXEC,0);
            if(pin<0) return -1;
            struct stat st;
            if(fstat(pin,&st)) {int error=errno;syscall(SYS_close,pin);return q2_error(error);}
            if(st.st_size<=0 || (uint64_t)st.st_size<donor->length) {
                syscall(SYS_close,pin);return q2_error(st.st_size<=0?EOPNOTSUPP:EINVAL);
            }
            plane=*donor;memset(plane.reserved,0,sizeof(plane.reserved));plane.m.fd=pin;
            native.flags&=~Q2_BUF_EOS;if(buffer->flags&Q2_BUF_EOS) native.flags|=V4L2_BUF_FLAG_LAST;
        }
    }
    int result=s->backend(s->fd,request,&native),error=errno;
    if(result) {if(pin>=0) syscall(SYS_close,pin);return q2_error(error);}
    if(native.length!=1 || native.index>=q->count) {
        if(pin>=0) syscall(SYS_close,pin);
        return q2_error(EPROTO);
    }
    slot=&q->slots[native.index];
    if(request==VIDIOC_QBUF || request==VIDIOC_PREPARE_BUF) {
        if(slot->valid && slot->pin>=0) (void)syscall(SYS_close,slot->pin);
        slot->donor=buffer->m.planes[0];slot->pin=pin;slot->valid=1;
        if(request==VIDIOC_QBUF) {slot->queued=1;s->queued++;}
    } else if(request==VIDIOC_DQBUF) {
        if(!slot->valid || !slot->queued) return q2_error(EPROTO);
        slot->queued=0;if(s->queued)s->queued--;
        if(slot->pin>=0) (void)syscall(SYS_close,slot->pin);
        slot->pin=-1;
    }
    struct v4l2_plane translated=plane;
    if(slot->valid) {
        translated.m.userptr=slot->donor.m.userptr;
        memcpy(translated.reserved,slot->donor.reserved,sizeof(translated.reserved));
    } else {translated.m.userptr=0;memset(translated.reserved,0,sizeof(translated.reserved));}
    if(native.flags&V4L2_BUF_FLAG_LAST) {native.flags&=~V4L2_BUF_FLAG_LAST;native.flags|=Q2_BUF_EOS;}
    if(native.flags&V4L2_BUF_FLAG_ERROR) native.flags|=Q2_BUF_DATA_CORRUPT;
    native.memory=V4L2_MEMORY_USERPTR;native.m.planes=buffer->m.planes;
    *buffer=native;buffer->m.planes[0]=translated;return 0;
}
static int q2_source_event(struct q2_session *s, const struct v4l2_event *native) {
    if (native->type != V4L2_EVENT_SOURCE_CHANGE ||
        native->u.src_change.changes != V4L2_EVENT_SRC_CH_RESOLUTION) return q2_error(EOPNOTSUPP);
    /* The downstream event requires bit depth, progressive and color metadata,
     * not just dimensions. Until those mappings are fully source-verified,
     * report the unrepresentable real event rather than invent zero metadata. */
    (void)s;
    return q2_error(EOPNOTSUPP);
}
static int q2_dispatch(struct q2_session *s, unsigned long request, void *argument) {
    if (!argument && _IOC_DIR(request) != _IOC_NONE) return q2_error(EFAULT);
    if (request == VIDIOC_SUBSCRIBE_EVENT || request == VIDIOC_UNSUBSCRIBE_EVENT)
        return q2_subscribe(s, argument, request == VIDIOC_UNSUBSCRIBE_EVENT);
    if (request == VIDIOC_DQEVENT) {
        if (s->head) return q2_pop(s, argument);
        struct v4l2_event native = {0};
        if (s->backend(s->fd, request, &native)) return -1;
        if (q2_source_event(s, &native)) {
            int error = errno;
            /* This is an actual unrepresentable event, not a fabricated VPU failure. */
            fprintf(stderr, "Q2BRIDGE unrepresentable native event=0x%x errno=%d\n", native.type, error);
            return q2_error(error);
        }
        return q2_pop(s, argument);
    }
    if (request == VIDIOC_G_CTRL || request == VIDIOC_S_CTRL) return q2_ctrl(s, request, argument);
    if (request == VIDIOC_G_EXT_CTRLS || request == VIDIOC_S_EXT_CTRLS || request == VIDIOC_TRY_EXT_CTRLS) {
        struct v4l2_ext_controls *ctrls = argument;
        for (uint32_t i = 0; i < ctrls->count; i++) {
            if (q2_private_control(ctrls->controls[i].id) || ctrls->controls[i].id == V4L2_CID_MPEG_VIDEO_H264_LEVEL) {
                ctrls->error_idx = i; return q2_error(EOPNOTSUPP);
            }
        }
    }
    if (request == VIDIOC_QUERYCTRL) {
        struct v4l2_queryctrl *query = argument;
        if (q2_private_control(query->id)) return q2_error(EOPNOTSUPP);
        if (query->id == V4L2_CID_MPEG_VIDEO_H264_LEVEL) {
            struct v4l2_queryctrl native = *query;
            if (s->backend(s->fd, request, &native)) return -1;
            native.minimum = q2_level(native.minimum, 0); native.maximum = q2_level(native.maximum, 0);
            native.default_value = q2_level(native.default_value, 0);
            if (native.minimum < 0 || native.maximum < 0 || native.default_value < 0) return q2_error(EOVERFLOW);
            *query = native; return 0;
        }
    }
    if (request == VIDIOC_QUERYMENU) {
        struct v4l2_querymenu *q = argument;
        if (q2_private_control(q->id)) return q2_error(EOPNOTSUPP);
        if (q->id == V4L2_CID_MPEG_VIDEO_H264_LEVEL) {
            struct v4l2_querymenu native = *q; int mapped = q2_level((int)q->index, 1);
            if (mapped < 0) return q2_error(EINVAL);
            native.index = (uint32_t)mapped;
            if (s->backend(s->fd, request, &native)) return -1;
            native.index = q->index; *q = native; return 0;
        }
    }
    if (request == VIDIOC_DECODER_CMD || request == VIDIOC_TRY_DECODER_CMD) {
        struct v4l2_decoder_cmd *cmd = argument;
        if (cmd->cmd == Q2_CMD_FLUSH) {
            if (!cmd->flags || (cmd->flags & ~3U)) return q2_error(EINVAL);
            if (s->queued || s->streaming) return q2_error(EOPNOTSUPP);
            /* No buffer ever submitted and no streaming: all requested queues
             * are already empty. The broker owns and reports that completion. */
            if (request == VIDIOC_TRY_DECODER_CMD) return 0;
            return q2_enqueue(s, Q2_FLUSH_DONE, &cmd->flags, 1);
        }
        if (cmd->cmd == Q2_CMD_CONTINUE) return q2_error(EOPNOTSUPP);
    }
    if (request == VIDIOC_REQBUFS) return q2_reqbufs(s,argument);
    if ((request == VIDIOC_QBUF || request == VIDIOC_PREPARE_BUF || request == VIDIOC_QUERYBUF || request == VIDIOC_DQBUF) &&
        ((struct v4l2_buffer *)argument)->memory == V4L2_MEMORY_USERPTR) return q2_user_buffer(s,request,argument);
    /* Standard requests retain their real errno. Private unknown ioctl numbers
     * are never converted to success, including VIDIOC_{REGISTER,RELEASE}_BUFFER. */
    int result = s->backend(s->fd, request, argument); int saved = errno;
    if (!result && request == VIDIOC_QBUF) s->queued++;
    if (!result && request == VIDIOC_DQBUF && s->queued) s->queued--;
    if (!result && (request == VIDIOC_STREAMON || request == VIDIOC_STREAMOFF)) {
        int index=q2_queue_index(*(uint32_t*)argument);
        if(index>=0) {
            if(request==VIDIOC_STREAMON) s->streaming|=1U<<index;
            else {
                s->streaming&=~(1U<<index);
                struct q2_queue *q=&s->queues[index];
                for(unsigned i=0;i<q->count;i++) {
                    struct q2_buffer_slot *slot=&q->slots[i];
                    if(slot->queued) {slot->queued=0;if(s->queued)s->queued--;}
                    if(slot->valid && slot->pin>=0) syscall(SYS_close,slot->pin);
                    slot->pin=-1;
                }
            }
        }
    }
    if (result < 0 && (saved == EIO || saved == ENODEV)) {
        /* Translate a real fatal backend failure; never emit success for it. */
        (void)q2_enqueue(s, Q2_SYS_ERROR, NULL, 0);
    }
    errno = saved; return result;
}

#ifndef Q2_BRIDGE_TEST
struct q2_fd { int fd; struct q2_session *session; struct q2_fd *next; };
static struct q2_fd *q2_fds;
static pthread_mutex_t q2_lock = PTHREAD_MUTEX_INITIALIZER;
static int q2_enabled;
static int q2_aux_enabled;
static int (*q2_next_ioctl)(int, request_t, ...);
static int (*q2_next_poll)(struct pollfd *, nfds_t, int);
static int (*q2_next_ppoll)(struct pollfd *, nfds_t, const struct timespec *, const sigset_t *);
static int q2_real_ioctl(int fd, unsigned long request, void *argument) {
    return q2_next_ioctl ? q2_next_ioctl(fd, (request_t)request, argument) : (int)syscall(SYS_ioctl, fd, request, argument);
}
static int q2_aux_minimum(struct q2_session *s,uint32_t id,int *answer) {
    if (!q2_aux_enabled) return q2_error(EAGAIN);
    struct v4l2_format formats[2]={{.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE},{.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE}};
    for(unsigned i=0;i<2;i++) {
        int r=q2_real_ioctl(s->fd,VIDIOC_G_FMT,&formats[i]);int error=errno;
        fprintf(stderr,"Q2BRIDGE AUX main G_FMT type=%u result=%d errno=%d\n",formats[i].type,r,error);
        if(r) return q2_error(error);
        if(!formats[i].fmt.pix_mp.width || !formats[i].fmt.pix_mp.height || !formats[i].fmt.pix_mp.pixelformat) return q2_error(EPROTO);
    }
    char proc[64],node[PATH_MAX];snprintf(proc,sizeof(proc),"/proc/self/fd/%d",s->fd);
    ssize_t n=readlink(proc,node,sizeof(node)-1);if(n<0) return -1;
    node[n]=0;
    int aux=(int)syscall(SYS_openat,AT_FDCWD,node,O_RDWR|O_NONBLOCK|O_CLOEXEC,0);
    fprintf(stderr,"Q2BRIDGE AUX open node=%s result=%d errno=%d\n",node,aux,errno);
    if(aux<0) return -1;
    int result=-1,error=0;
    for(unsigned i=0;i<2;i++) {
        struct v4l2_format expected=formats[i];
        int r=q2_real_ioctl(aux,VIDIOC_S_FMT,&formats[i]);error=errno;
        fprintf(stderr,"Q2BRIDGE AUX S_FMT type=%u result=%d errno=%d\n",formats[i].type,r,error);
        if(r) goto done;
        if(formats[i].fmt.pix_mp.width!=expected.fmt.pix_mp.width || formats[i].fmt.pix_mp.height!=expected.fmt.pix_mp.height ||
           formats[i].fmt.pix_mp.pixelformat!=expected.fmt.pix_mp.pixelformat) {error=ERANGE;goto done;}
    }
    struct v4l2_requestbuffers buffers={.count=1,.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,.memory=V4L2_MEMORY_DMABUF};
    int r=q2_real_ioctl(aux,VIDIOC_REQBUFS,&buffers);error=errno;
    fprintf(stderr,"Q2BRIDGE AUX REQBUFS request_count=1 actual_count=%u result=%d errno=%d (not used as minimum)\n",buffers.count,r,error);
    if(r) goto done;
    struct v4l2_control control={.id=id};
    r=q2_real_ioctl(aux,VIDIOC_G_CTRL,&control);error=errno;
    fprintf(stderr,"Q2BRIDGE AUX G_CTRL id=0x%x value=%d result=%d errno=%d\n",id,control.value,r,error);
    if(r) goto done;
    if(control.value<=0) {error=EPROTO;goto done;}
    *answer=control.value;result=0;
done:
    r=(int)syscall(SYS_close,aux);
    fprintf(stderr,"Q2BRIDGE AUX close result=%d errno=%d SESSION_END_confirmation=paired-kernel-log-required\n",r,errno);
    if(r && !result) {error=errno;result=-1;}
    errno=error;return result;
}
static struct q2_session *q2_lookup(int fd) {
    for (struct q2_fd *p = q2_fds; p; p = p->next) if (p->fd == fd) return p->session;
    return NULL;
}
static int q2_attach(int fd, struct q2_session *s) {
    struct q2_fd *p = malloc(sizeof(*p)); if (!p) return -1;
    p->fd = fd; p->session = s; p->next = q2_fds; q2_fds = p; s->refs++; return 0;
}
/* refs counts registry aliases and in-flight ioctl pins. No caller may drop its
 * final pin until the operation fd has been closed outside the registry lock. */
static struct q2_session *q2_release(struct q2_session *s) {
    return --s->refs ? NULL : s;
}
static void q2_dispose(struct q2_session *s) {
    if(!s) return;
    q2_queue_clear(s,&s->queues[0]);q2_queue_clear(s,&s->queues[1]);
    q2_clear(s); (void)syscall(SYS_close, s->wake);
    pthread_mutex_destroy(&s->mutex); free(s);
}
static struct q2_session *q2_detach(int fd) {
    struct q2_fd **at = &q2_fds;
    while (*at && (*at)->fd != fd) at = &(*at)->next;
    if (!*at) return NULL;
    struct q2_fd *p = *at; struct q2_session *s = p->session; *at = p->next; free(p);
    return q2_release(s);
}
static int q2_decoder(int flags) {
    DIR *dir = opendir("/sys/class/video4linux"); if (!dir) return -1;
    struct dirent *entry; int selected = -1, failure = ENODEV;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "video", 5)) continue;
        const char *n = entry->d_name + 5; if (!*n || strspn(n, "0123456789") != strlen(n)) continue;
        char path[PATH_MAX]; int size = snprintf(path, sizeof(path), "/dev/%s", entry->d_name);
        if (size < 0 || (size_t)size >= sizeof(path)) continue;
        int fd = (int)syscall(SYS_openat, AT_FDCWD, path, flags, 0);
        if (fd < 0) { failure = errno; continue; }
        struct v4l2_capability cap = {0}; struct stat st;
        int ok = !fstat(fd, &st) && S_ISCHR(st.st_mode) && !q2_real_ioctl(fd, VIDIOC_QUERYCAP, &cap);
        uint32_t caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS ? cap.device_caps : cap.capabilities;
        ok = ok && strstr((char *)cap.driver, "venus") && (caps & V4L2_CAP_VIDEO_M2M_MPLANE) && (caps & V4L2_CAP_STREAMING);
        int h264 = 0;
        for (unsigned i = 0; ok; i++) {
            struct v4l2_fmtdesc fmt = { .index = i, .type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE };
            if (q2_real_ioctl(fd, VIDIOC_ENUM_FMT, &fmt)) break;
            if (fmt.pixelformat == V4L2_PIX_FMT_H264) { h264 = 1; break; }
        }
        if (h264) { selected = fd; fprintf(stderr, "Q2BRIDGE decoder=%s driver=%s\n", path, cap.driver); break; }
        (void)syscall(SYS_close, fd);
    }
    closedir(dir); if (selected < 0) errno = failure; return selected;
}
__attribute__((constructor)) static void q2_initialize(void) {
    q2_next_ioctl = dlsym(RTLD_NEXT, "ioctl"); q2_next_poll = dlsym(RTLD_NEXT, "poll"); q2_next_ppoll = dlsym(RTLD_NEXT, "ppoll");
    const char *enabled = getenv("QUEST2_VIDC_BRIDGE"); q2_enabled = enabled && !strcmp(enabled, "1");
    const char *aux = getenv("QUEST2_VIDC_AUX_CAPS"); q2_aux_enabled = aux && !strcmp(aux, "1");
}
static int q2_openat(int dir, const char *path, int flags, mode_t mode) {
    if (!q2_enabled || !path || (strcmp(path, "/dev/video32") && strcmp(path, "/dev/video/q6_dec")))
        return (int)syscall(SYS_openat, dir, path, flags, mode);
    if (flags & (O_CREAT | O_TRUNC)) return q2_error(EINVAL);
    /* The donor polls then drains DQBUF until EAGAIN. On upstream Venus a
     * blocking fd can sleep in that drain, preventing the other queue's QBUF.
     * Only this private redirected descriptor is made nonblocking. */
    int fd = q2_decoder(flags | O_NONBLOCK); if (fd < 0) return fd;
    struct q2_session *s = calloc(1, sizeof(*s));
    if (!s) { int e = errno; syscall(SYS_close, fd); return q2_error(e); }
    s->fd = fd; s->backend = q2_real_ioctl; s->query_minimum=q2_aux_minimum;
    int init = pthread_mutex_init(&s->mutex, NULL);
    if (init) { free(s); syscall(SYS_close, fd); return q2_error(init); }
    s->wake = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
    if (s->wake < 0) { int e = errno; pthread_mutex_destroy(&s->mutex); free(s); syscall(SYS_close, fd); return q2_error(e); }
    pthread_mutex_lock(&q2_lock); int result = q2_attach(fd, s); pthread_mutex_unlock(&q2_lock);
    if (result) { int e = errno; syscall(SYS_close, s->wake); pthread_mutex_destroy(&s->mutex); free(s); syscall(SYS_close, fd); return q2_error(e); }
    return fd;
}
static int q2_needs_mode(int flags) { return (flags & O_CREAT) || ((flags & O_TMPFILE) == O_TMPFILE); }
#define Q2_OPEN(name) int name(const char *path, int flags, ...) { mode_t mode = 0; if(q2_needs_mode(flags)) { va_list a; va_start(a,flags); mode=va_arg(a,unsigned int); va_end(a); } return q2_openat(AT_FDCWD,path,flags,mode); }
#define Q2_OPENAT(name) int name(int dir, const char *path, int flags, ...) { mode_t mode = 0; if(q2_needs_mode(flags)) { va_list a; va_start(a,flags); mode=va_arg(a,unsigned int); va_end(a); } return q2_openat(dir,path,flags,mode); }
Q2_OPEN(open) Q2_OPEN(open64) Q2_OPENAT(openat) Q2_OPENAT(openat64)
int __open_2(const char *path,int flags) { if(q2_needs_mode(flags)) abort(); return q2_openat(AT_FDCWD,path,flags,0); }
int __openat_2(int dir,const char *path,int flags) { if(q2_needs_mode(flags)) abort(); return q2_openat(dir,path,flags,0); }
int close(int fd) {
    pthread_mutex_lock(&q2_lock); struct q2_session *s=q2_lookup(fd);
    if(!s) {pthread_mutex_unlock(&q2_lock);return (int)syscall(SYS_close,fd);}
    /* Commit kernel descriptor close + registry detach atomically with respect
     * to dup2/dup3. A temporary file-description pin keeps final kernel release
     * outside the registry lock; failed pin leaves the original descriptor open. */
    int pinned=(int)syscall(SYS_fcntl,fd,F_DUPFD_CLOEXEC,0);
    if(pinned<0) {int error=errno;pthread_mutex_unlock(&q2_lock);return q2_error(error);}
    int result=(int)syscall(SYS_close,fd),error=errno;
    struct q2_session *dead=q2_detach(fd);pthread_mutex_unlock(&q2_lock);
    (void)syscall(SYS_close,pinned);q2_dispose(dead);errno=error;return result;
}
static int q2_duplicate(int fd, int target, int flags, int kind) {
    pthread_mutex_lock(&q2_lock); struct q2_session *s=q2_lookup(fd),*dead=NULL; int r,old=-1;
    /* dup2/dup3 implicitly close target. Keep its file description alive so a
     * final Venus release cannot sleep while holding the registry mutex. */
    if(kind && fd!=target && q2_lookup(target)) {
        old=(int)syscall(SYS_fcntl,target,F_DUPFD_CLOEXEC,0);
        if(old<0) {int error=errno;pthread_mutex_unlock(&q2_lock);return q2_error(error);}
    }
    if(kind==0) r=(int)syscall(SYS_dup,fd);
    else if(kind==1 && fd==target) r=syscall(SYS_fcntl,fd,F_GETFD)<0?-1:fd;
    else r=(int)syscall(SYS_dup3,fd,target,flags);
    if(r>=0 && !(kind==1 && fd==target)) {
        dead=q2_detach(r);
        if(s && q2_attach(r,s)) { int error=errno; syscall(SYS_close,r); r=q2_error(error); }
    }
    int error=errno; pthread_mutex_unlock(&q2_lock);
    if(old>=0) (void)syscall(SYS_close,old);
    q2_dispose(dead);
    errno=error; return r;
}
int dup(int fd) { return q2_duplicate(fd,0,0,0); }
int dup2(int fd,int target) { return q2_duplicate(fd,target,0,1); }
int dup3(int fd,int target,int flags) { return q2_duplicate(fd,target,flags,2); }
__attribute__((visibility("hidden"))) int q2_ioctl_forward(int fd, request_t request, uintptr_t argument) {
    pthread_mutex_lock(&q2_lock); struct q2_session *s=q2_lookup(fd);
    if (!s) { pthread_mutex_unlock(&q2_lock); return q2_real_ioctl(fd,(unsigned int)request,(void *)argument); }
    int pinned=(int)syscall(SYS_fcntl,fd,F_DUPFD_CLOEXEC,0);
    if(pinned<0) { int error=errno;pthread_mutex_unlock(&q2_lock);return q2_error(error); }
    s->refs++; pthread_mutex_unlock(&q2_lock);
    pthread_mutex_lock(&s->mutex);
    s->fd=pinned;
    /* Also enforce the nonblocking contract if a caller changed status flags
     * through an uninterposed fcntl. This fd shares the real file description. */
    int flags=(int)syscall(SYS_fcntl,pinned,F_GETFL),r;
    if(flags<0) r=-1;
    else if(!(flags&O_NONBLOCK) && syscall(SYS_fcntl,pinned,F_SETFL,flags|O_NONBLOCK)<0) r=-1;
    else r=q2_dispatch(s,(unsigned int)request,(void *)argument);
    int error=errno; pthread_mutex_unlock(&s->mutex);
    (void)syscall(SYS_close,pinned);
    pthread_mutex_lock(&q2_lock); struct q2_session *dead=q2_release(s); pthread_mutex_unlock(&q2_lock);
    q2_dispose(dead);
    fprintf(stderr,"Q2BRIDGE ioctl fd=%d req=0x%x return=%d errno=%d\n",fd,(unsigned)request,r,error);
    errno=error; return r;
}
static int q2_poll(struct pollfd *fds,nfds_t count,int timeout,const struct timespec *ts,const sigset_t *mask,int precise) {
    nfds_t doubled; size_t bytes;
    if (__builtin_mul_overflow(count,(nfds_t)2,&doubled) ||
        __builtin_mul_overflow((size_t)doubled,sizeof(struct pollfd),&bytes)) return q2_error(EINVAL);
    struct pollfd *copy=calloc(bytes?bytes:sizeof(*copy),1); if(!copy) return -1;
    memcpy(copy,fds,count*sizeof(*fds));
    for(nfds_t i=0;i<count;i++) copy[count+i].fd=-1;
    pthread_mutex_lock(&q2_lock);
    for(nfds_t i=0;i<count;i++) {
        struct q2_session *s=q2_lookup(fds[i].fd);
        if(s && (fds[i].events&POLLPRI)) {
            copy[count+i].fd=(int)syscall(SYS_fcntl,s->wake,F_DUPFD_CLOEXEC,0);
            if(copy[count+i].fd<0) {
                int error=errno;pthread_mutex_unlock(&q2_lock);
                for(nfds_t j=0;j<count;j++) if(copy[count+j].fd>=0) (void)syscall(SYS_close,copy[count+j].fd);
                free(copy);return q2_error(error);
            }
            copy[count+i].events=POLLIN;
        }
    }
    pthread_mutex_unlock(&q2_lock);
    int r;
    if(precise && q2_next_ppoll) r=q2_next_ppoll(copy,doubled,ts,mask);
    else if(!precise && q2_next_poll) r=q2_next_poll(copy,doubled,timeout);
    else r=q2_error(ENOSYS);
    int error=errno,ready=0;
    for(nfds_t i=0;i<count;i++) {
        if(r>=0) { fds[i].revents=copy[i].revents; if(copy[count+i].revents&POLLIN) fds[i].revents|=POLLPRI; if(fds[i].revents) ready++; }
        if(copy[count+i].fd>=0) (void)syscall(SYS_close,copy[count+i].fd);
    }
    free(copy); errno=error; return r<0?r:ready;
}
int poll(struct pollfd *fds,nfds_t count,int timeout) { return q2_poll(fds,count,timeout,NULL,NULL,0); }
int ppoll(struct pollfd *fds,nfds_t count,const struct timespec *timeout,const sigset_t *mask) { return q2_poll(fds,count,0,timeout,mask,1); }
#if defined(__aarch64__)
__asm__(".text\n.globl ioctl\n.type ioctl,%function\nioctl:\nb q2_ioctl_forward\n.size ioctl,.-ioctl\n");
#elif defined(__x86_64__)
__asm__(".text\n.globl ioctl\n.type ioctl,@function\nioctl:\njmp q2_ioctl_forward\n.size ioctl,.-ioctl\n");
#else
#error Supported trampoline architectures are AArch64 and x86_64
#endif
#endif
