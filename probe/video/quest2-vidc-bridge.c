/* SPDX-License-Identifier: MIT
 * Explicit, process-local Quest VIDC ABI adapter. This is NOT a decoder.
 * The unchanged donor drives OMX; Venus performs all actual decoding.
 *
 * Real event broker, acknowledged flush, WC/cached ION and DMA-buf queues.
 * Linear NV12/P010 have a translated crop metadata companion plane. Unknown
 * controls and video planes/offsets remain unsupported. Native firmware metadata
 * is an explicit candidate option with separate offset-zero DMA allocations.
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
#include <linux/dma-heap.h>
#include <linux/dma-buf.h>
#include <drm/msm_drm.h>
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
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif
#define Q2_EVENT_BASE 0x08001000U
#define Q2_FLUSH_DONE (Q2_EVENT_BASE + 1)
#define Q2_RECONFIG (Q2_EVENT_BASE + 3)
#define Q2_SYS_ERROR (Q2_EVENT_BASE + 5)
#define Q2_RELEASE_REFERENCE (Q2_EVENT_BASE + 6)
#define Q2_RELEASE_UNQUEUED (Q2_EVENT_BASE + 7)
#define Q2_OVERLOAD (Q2_EVENT_BASE + 8)
#define Q2_UNSUPPORTED (Q2_EVENT_BASE + 10)
#define Q2_CTRL_BASE 0x00992000U
#include "quest2-vidc-node.h"
#define Q2_CMD_FLUSH 4U
#define Q2_CMD_CONTINUE 5U
#define Q2_BUF_EOS 0x02000000U
#define Q2_BUF_CODECCONFIG 0x00020000U
#define Q2_NATIVE_CODECCONFIG 0x01000000U
#define Q2_PIX_FMT_P010_VENUS v4l2_fourcc('Q','P','1','0')
#define Q2_BUF_DATA_CORRUPT 0x00400000U
#define Q2_NATIVE_EXTRA_CTRL 0x00982f00U
#ifndef QUEST2_NATIVE_EXTRA_DEFAULT
#define QUEST2_NATIVE_EXTRA_DEFAULT 0
#endif

#ifdef __ANDROID__
typedef int request_t;
#else
typedef unsigned long request_t;
#endif
typedef int (*q2_backend)(int, unsigned long, void *);
struct q2_event_node { struct v4l2_event event; struct q2_event_node *next; };
struct q2_buffer_slot {
    struct v4l2_plane donor, extra;
    int pin,valid,queued,extra_fd;
    void *extra_map;
    int native_extra_fd;
    void *native_extra_map;
    uint32_t native_extra_size, native_extra_used;
};
struct q2_queue { unsigned count; int userptr; struct q2_buffer_slot *slots; };
struct q2_session {
    int fd, wake;
    pthread_mutex_t mutex;
    unsigned subscriptions, refs, streaming, queued;
    int source_subscribed, draining, metadata_plane;
    uint32_t sequence, codec_config_flag, native_extra_mask, native_extra_capacity;
    int native_extra_enabled;
    int layout_pending;
    uint32_t layout_stride,layout_scan,layout_width,layout_height,layout_format,layout_size;
    uint64_t node_token, node_usage;
    int node_bound, node_native, node_usage_valid;
    uint32_t source_changes;
    int (*native_layout_for_usage)(unsigned,unsigned,unsigned,uint64_t,unsigned,unsigned *,unsigned *,unsigned *);
    int (*native_layout_for_usage_v1)(unsigned,unsigned,unsigned,uint64_t,unsigned *,unsigned *,unsigned *);
    int (*native_layout)(unsigned,unsigned,unsigned,unsigned *,unsigned *,unsigned *);
    struct q2_event_node *head, *tail;
    q2_backend backend;
    int (*sync_buffer)(int);
    int (*cpu_access)(int, uint64_t);
    int (*allocate_extra)(size_t, int *);
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
    if (ctrl->id == 0x00992077U && request == VIDIOC_S_CTRL) {
        /* Downstream FRAME_RATE is Q16 fps; upstream Venus takes time/frame. */
        if (ctrl->value <= 0) return q2_error(EINVAL);
        struct v4l2_streamparm parm = {.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE};
        parm.parm.capture.timeperframe.numerator = 65536;
        parm.parm.capture.timeperframe.denominator = (uint32_t)ctrl->value;
        return s->backend(s->fd, VIDIOC_S_PARM, &parm);
    }
    if (ctrl->id == Q2_CTRL_BASE+17 && request == VIDIOC_S_CTRL && s->native_extra_enabled) {
        /* Downstream enum values, not HFI types. Firmware was enabled before
         * INIT; later OMX requests may acknowledge only that real mask. */
        unsigned bit=ctrl->value==29?1:ctrl->value==26?2:ctrl->value==27?4:0;
        if(ctrl->value==25 && s->metadata_plane) return 0;
        return bit && (s->native_extra_mask&bit)?0:q2_error(EOPNOTSUPP);
    }
    if (q2_private_control(ctrl->id)) return q2_error(EOPNOTSUPP);
    struct v4l2_control native = *ctrl;
    if (ctrl->id == V4L2_CID_MPEG_VIDEO_H264_LEVEL && request == VIDIOC_S_CTRL) {
        native.value = q2_level(ctrl->value, 1);
        if (native.value < 0) return q2_error(EINVAL);
    }
    int result = s->backend(s->fd, request, &native);
    int backend_error = errno;

    errno = backend_error;
    /* Venus CAPTURE reports EINVAL before HFI INIT; the auxiliary query must
     * still succeed on a real initialized session, never supply a guessed count. */
    if (result && request == VIDIOC_G_CTRL && s->query_minimum &&
        ((errno == EAGAIN && (ctrl->id == V4L2_CID_MIN_BUFFERS_FOR_OUTPUT ||
                             ctrl->id == V4L2_CID_MIN_BUFFERS_FOR_CAPTURE)) ||
         (errno == EINVAL && ctrl->id == V4L2_CID_MIN_BUFFERS_FOR_CAPTURE &&
          !s->streaming && !s->queued && !s->queues[0].count && !s->queues[1].count))) {
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
        if(slot->extra_map) {munmap(slot->extra_map,slot->extra.length);syscall(SYS_close,slot->extra_fd);}
        if(slot->valid && slot->pin>=0) (void)syscall(SYS_close,slot->pin);
        if(slot->native_extra_map) {
            munmap(slot->native_extra_map,slot->native_extra_size);
            syscall(SYS_close,slot->native_extra_fd);
        }
        if(slot->queued && s->queued) s->queued--;
    }
    free(q->slots);memset(q,0,sizeof(*q));
}
/* Only an explicitly linked private client supplies this allocator contract. */
static int q2_has_native_layout(const struct q2_session *s) {
    return s->native_layout || s->node_native;
}
static int q2_native_layout(struct q2_session *s,unsigned requested_format) {
    if(!q2_has_native_layout(s) || (!s->layout_pending &&
       (!requested_format || requested_format==s->layout_format)))return 0;
    if(s->node_native && (!s->node_usage_valid || (!s->native_layout_for_usage && !s->native_layout_for_usage_v1)))return q2_error(EAGAIN);
    if(s->queues[1].count || (s->streaming&2))return q2_error(EBUSY);
    struct v4l2_format f={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
    struct v4l2_selection crop={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,.target=V4L2_SEL_TGT_COMPOSE};
    if(s->backend(s->fd,VIDIOC_G_FMT,&f) || s->backend(s->fd,VIDIOC_G_SELECTION,&crop))return -1;
    unsigned stride=0,scan=0,size=0;
    if(crop.r.left || crop.r.top || crop.r.width<=0 || crop.r.height<=0)
        return q2_error(EOPNOTSUPP);
    const unsigned selected_format=requested_format?requested_format:f.fmt.pix_mp.pixelformat;
    if(selected_format!=f.fmt.pix_mp.pixelformat ||
       (s->layout_format && selected_format!=s->layout_format)) {
        /* G_FMT can normalize a new bit depth before S_FMT selects its format. */
        f.fmt.pix_mp.pixelformat=selected_format;
        f.fmt.pix_mp.height=crop.r.height;f.fmt.pix_mp.field=V4L2_FIELD_NONE;
        f.fmt.pix_mp.plane_fmt[0].bytesperline=0;
        f.fmt.pix_mp.plane_fmt[0].sizeimage=0;
        if(s->backend(s->fd,VIDIOC_S_FMT,&f))return -1;
        s->layout_pending=1;s->layout_stride=s->layout_scan=s->layout_size=0;
        if(f.fmt.pix_mp.pixelformat!=selected_format)return q2_error(EPROTO);
    }
    int layout_result;
    if(s->node_native && s->native_layout_for_usage)
        layout_result=s->native_layout_for_usage(crop.r.width,crop.r.height,f.fmt.pix_mp.pixelformat,
            s->node_usage,f.fmt.pix_mp.plane_fmt[0].sizeimage,&stride,&scan,&size);
    else if(s->node_native)
        layout_result=s->native_layout_for_usage_v1(crop.r.width,crop.r.height,f.fmt.pix_mp.pixelformat,
            s->node_usage,&stride,&scan,&size);
    else
        layout_result=s->native_layout(crop.r.width,crop.r.height,f.fmt.pix_mp.pixelformat,&stride,&scan,&size);
    if(layout_result)return q2_error(EOPNOTSUPP);
    /* Older NV12 services cannot request extra allocation height; never underallocate. */
    if(s->node_native && size<f.fmt.pix_mp.plane_fmt[0].sizeimage)return q2_error(EINVAL);
    const unsigned sample=f.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_NV12?1:
        f.fmt.pix_mp.pixelformat==V4L2_PIX_FMT_P010?2:0;
    if(!sample || stride<(uint64_t)crop.r.width*sample || stride>16384 || stride%128 ||
       scan<(uint32_t)crop.r.height || scan>8192 || scan%32 ||
       (uint64_t)stride*scan*3/2>size)return q2_error(EINVAL);
    f.fmt.pix_mp.height=scan;f.fmt.pix_mp.plane_fmt[0].bytesperline=stride;
    f.fmt.pix_mp.plane_fmt[0].sizeimage=size;
    if(s->backend(s->fd,VIDIOC_S_FMT,&f))return -1;
    if(f.fmt.pix_mp.pixelformat!=selected_format ||
       f.fmt.pix_mp.plane_fmt[0].bytesperline!=stride || f.fmt.pix_mp.height!=scan ||
       f.fmt.pix_mp.plane_fmt[0].sizeimage>size)return q2_error(EPROTO);
    s->layout_stride=stride;s->layout_scan=scan;s->layout_pending=0;
    s->layout_width=crop.r.width;s->layout_height=crop.r.height;
    s->layout_format=f.fmt.pix_mp.pixelformat;s->layout_size=size;

    return 0;
}
static int q2_reqbufs(struct q2_session *s,struct v4l2_requestbuffers *request) {
    int index=q2_queue_index(request->type);if(index<0) return q2_error(EOPNOTSUPP);
    struct q2_queue *q=&s->queues[index];

    if(index==1 && request->count && q2_native_layout(s,0))return -1;
    if(q->userptr && request->memory!=V4L2_MEMORY_USERPTR) return q2_error(EINVAL);
    if(request->memory!=V4L2_MEMORY_USERPTR) return s->backend(s->fd,VIDIOC_REQBUFS,request);
    struct v4l2_requestbuffers native=*request;native.memory=V4L2_MEMORY_DMABUF;
    int result=s->backend(s->fd,VIDIOC_REQBUFS,&native),error=result?errno:0;

    if(result) return q2_error(error);
    q2_queue_clear(s,q);
    if(native.count) {
        q->slots=calloc(native.count,sizeof(*q->slots));
        if(!q->slots) {
            int error=errno;struct v4l2_requestbuffers release=native;release.count=0;
            (void)s->backend(s->fd,VIDIOC_REQBUFS,&release);

            return q2_error(error);
        }
    }
    q->count=native.count;q->userptr=1;native.memory=V4L2_MEMORY_USERPTR;*request=native;
    if(index==1 && !native.count && q2_native_layout(s,0))return -1;
    return 0;
}
/* Native ACodec adjusts the output count after OMX has reserved empty slots.
 * Recreate only an untouched CAPTURE queue; imported/queued buffers stay busy. */
static int q2_set_format(struct q2_session *s,struct v4l2_format *format) {
    int result=s->backend(s->fd,VIDIOC_S_FMT,format),error=errno;
    if(!result || error!=EBUSY || !q2_has_native_layout(s) ||
       format->type!=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE || s->streaming || s->queued)
        return result;
    struct q2_queue *q=&s->queues[1];
    if(!q->userptr || !q->count || !q->slots)return q2_error(error);
    for(unsigned i=0;i<q->count;i++)
        if(q->slots[i].valid || q->slots[i].queued ||
           q->slots[i].extra_map || q->slots[i].native_extra_map)return q2_error(error);
    struct v4l2_requestbuffers release={.type=format->type,.memory=V4L2_MEMORY_DMABUF};
    if(s->backend(s->fd,VIDIOC_REQBUFS,&release))return -1;
    result=s->backend(s->fd,VIDIOC_S_FMT,format);error=errno;
    struct v4l2_requestbuffers restore={.type=format->type,.memory=V4L2_MEMORY_DMABUF,.count=q->count};
    int restored=s->backend(s->fd,VIDIOC_REQBUFS,&restore),restore_error=errno;

    if(restored || restore.count!=q->count) {
        if(!restored) {
            int freed=s->backend(s->fd,VIDIOC_REQBUFS,&release);
            if(freed)restore_error=errno;
            else restore_error=EPROTO;
        }
        q2_queue_clear(s,q);
        return q2_error(restore_error);
    }
    errno=error;return result;
}
/* Qualcomm USERPTR actually describes an ION dma-buf: reserved[0] is its fd,
 * reserved[1] is the allocation offset; userptr is a completion cookie. Keep
 * the original cookie and a real fd reference, not a copied/decoded fake frame.
 * Offset-zero and one plane are deliberately the only implemented layout:
 * current Venus capture submits the allocation base, not data_offset. */
#ifdef QUEST2_VPU_HWC_TRACE
#endif
static int q2_user_buffer(struct q2_session *s,unsigned long request,struct v4l2_buffer *buffer) {
    int index=q2_queue_index(buffer->type);if(index<0) return q2_error(EOPNOTSUPP);
    struct q2_queue *q=&s->queues[index];
    if(!q->userptr || !q->slots) return q2_error(EINVAL);
    if(!buffer->m.planes) return q2_error(EFAULT);
    if(buffer->length!=1) return q2_error(EOPNOTSUPP);
    struct v4l2_buffer native=*buffer;struct v4l2_plane planes[2]={{0}};
    unsigned native_planes=index==1 && s->native_extra_mask?2:1;
    native.memory=V4L2_MEMORY_DMABUF;native.m.planes=planes;native.length=native_planes;
    /* Query before DQBUF: an unsupported selection must not consume ownership. */
    struct v4l2_selection visible={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,
                                   .target=V4L2_SEL_TGT_COMPOSE};
    if(request==VIDIOC_DQBUF && index==1) {
        if(s->backend(s->fd,VIDIOC_G_SELECTION,&visible)) return -1;
        if(visible.r.left<0 || visible.r.top<0 || !visible.r.width || !visible.r.height)
            return q2_error(EPROTO);
    }
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
            if(index==0 && s->codec_config_flag==Q2_NATIVE_CODECCONFIG) allowed|=Q2_BUF_CODECCONFIG;
            if(buffer->flags&~allowed) return q2_error(EOPNOTSUPP);
            pin=(int)syscall(SYS_fcntl,(int)donor->reserved[0],F_DUPFD_CLOEXEC,0);
            if(pin<0) return -1;
            struct stat st;
            if(fstat(pin,&st)) {int error=errno;syscall(SYS_close,pin);return q2_error(error);}
            if(st.st_size<=0 || (uint64_t)st.st_size<donor->length) {
                syscall(SYS_close,pin);return q2_error(st.st_size<=0?EOPNOTSUPP:EINVAL);
            }
            if(request==VIDIOC_QBUF && s->sync_buffer && s->sync_buffer(pin)) {
                int error=errno;syscall(SYS_close,pin);return q2_error(error);
            }
            planes[0]=*donor;memset(planes[0].reserved,0,sizeof(planes[0].reserved));planes[0].m.fd=pin;
            native.flags&=~(Q2_BUF_EOS|Q2_BUF_CODECCONFIG);
            if(buffer->flags&Q2_BUF_EOS) native.flags|=V4L2_BUF_FLAG_LAST;
            if(buffer->flags&Q2_BUF_CODECCONFIG) native.flags|=s->codec_config_flag;
        }
    }
    if(native_planes==2 && (request==VIDIOC_QBUF || request==VIDIOC_PREPARE_BUF)) {
        if(!slot || !slot->native_extra_map) {
            if(pin>=0) syscall(SYS_close,pin);
            return q2_error(EINVAL);
        }
        planes[1].m.fd=slot->native_extra_fd;planes[1].length=slot->native_extra_size;
    }
    int result=s->backend(s->fd,request,&native),error=errno;
    if(result) {if(pin>=0) syscall(SYS_close,pin);return q2_error(error);}
    if(native.length!=native_planes || native.index>=q->count) {
        if(pin>=0) syscall(SYS_close,pin);
        return q2_error(EPROTO);
    }
    slot=&q->slots[native.index];
    if(request==VIDIOC_QBUF || request==VIDIOC_PREPARE_BUF) {
        if(slot->valid && slot->pin>=0) (void)syscall(SYS_close,slot->pin);
        slot->donor=buffer->m.planes[0];slot->pin=pin;slot->valid=1;
        if(request==VIDIOC_QBUF) {slot->queued=1;s->queued++;
            if(index==0 && (buffer->flags&Q2_BUF_EOS)) s->draining=1;}
#ifdef QUEST2_VPU_HWC_TRACE
#endif
    } else if(request==VIDIOC_DQBUF) {
        if(!slot->valid || !slot->queued) return q2_error(EPROTO);
        slot->queued=0;if(s->queued)s->queued--;
        slot->native_extra_used=0;
        if(native_planes==2) {
            if(planes[1].data_offset || planes[1].bytesused>slot->native_extra_size)
                native.flags|=V4L2_BUF_FLAG_ERROR;
            else slot->native_extra_used=planes[1].bytesused;
        }
        if(s->sync_buffer && s->sync_buffer(slot->pin)) {

            native.flags|=V4L2_BUF_FLAG_ERROR;
            (void)q2_enqueue(s,Q2_SYS_ERROR,NULL,0);
        }
#ifdef QUEST2_VPU_HWC_TRACE
#endif
        if(slot->pin>=0) (void)syscall(SYS_close,slot->pin);
        slot->pin=-1;
    }
    struct v4l2_plane translated=planes[0];
    if(slot->valid) {
        translated.m.userptr=slot->donor.m.userptr;
        memcpy(translated.reserved,slot->donor.reserved,sizeof(translated.reserved));
    } else {translated.m.userptr=0;memset(translated.reserved,0,sizeof(translated.reserved));}
    if(request==VIDIOC_DQBUF && index==1) {
        translated.reserved[2]=visible.r.left;translated.reserved[3]=visible.r.top;
        translated.reserved[4]=visible.r.width;translated.reserved[5]=visible.r.height;
    }
    if(native.flags&Q2_NATIVE_CODECCONFIG) {
        native.flags&=~Q2_NATIVE_CODECCONFIG;native.flags|=Q2_BUF_CODECCONFIG;
    }
    if(native.flags&V4L2_BUF_FLAG_LAST) {native.flags&=~V4L2_BUF_FLAG_LAST;
        if(request!=VIDIOC_DQBUF || s->draining) native.flags|=Q2_BUF_EOS;}
    if(native.flags&V4L2_BUF_FLAG_ERROR) native.flags|=Q2_BUF_DATA_CORRUPT;
    native.memory=V4L2_MEMORY_USERPTR;native.m.planes=buffer->m.planes;native.length=1;
    *buffer=native;buffer->m.planes[0]=translated;return 0;
}
/* Quest's binary consumes crop exclusively through its extradata plane. This
 * record translates real V4L2 COMPOSE geometry; it is not raw HFI extradata.
 * Optional MISR sets are absent. No HDR/interlace/statistics are invented. */
static void q2_crop_extra(void *mapping, const struct v4l2_plane *plane,
                          const struct v4l2_format *format, uint32_t sequence) {
    uint32_t data[90]={0}; /* 336-byte indexed crop + 24-byte terminator */
    data[0]=336;data[2]=1;data[3]=0x7f100002;data[4]=316;
    data[5]=0x0700000f;data[6]=312;data[8]=1;
    for(unsigned i=0;i<4;i++)data[9+i]=plane->reserved[2+i];
    data[13]=format->fmt.pix_mp.width;data[14]=format->fmt.pix_mp.height;
    data[15]=sequence;
    data[16]=data[17]=format->fmt.pix_mp.pixelformat==V4L2_PIX_FMT_P010?10:8;
    data[84]=24;
    memcpy(mapping,data,sizeof(data));
}
/* Capacity is not record length: NONE ends the actual firmware chain.
 * Validate the full chain before exposing any bytes to the donor parser. */
static int q2_extra_records(const void *data,size_t bytes,size_t *used) {
    size_t at=0;
    while(bytes-at>=20) {
        uint32_t h[5];memcpy(h,(const char *)data+at,sizeof(h));
        if(h[0]<20 || (h[0]&3) || h[0]>bytes-at || h[4]>h[0]-20) return q2_error(EPROTO);
        if(!h[3]) {
            if(h[4]) return q2_error(EPROTO);
            *used=at+h[0];return 0;
        }
        unsigned minimum=h[3]==0x7f100006?56:h[3]==0x15?40:h[3]==0x16?8:0;
        if(h[4]<minimum) return q2_error(EPROTO);
        at+=h[0];
    }
    return q2_error(EPROTO);
}
static int q2_extra_access(struct q2_session *s,int fd,uint64_t flags) {
    if(s->cpu_access) return s->cpu_access(fd,flags);
    return s->sync_buffer?s->sync_buffer(fd):0;
}
static int q2_native_extra_prepare(struct q2_session *s,struct q2_buffer_slot *slot,uint32_t bytes) {
    if(!bytes || !s->allocate_extra || !s->cpu_access) return q2_error(ENODATA);
    long page=sysconf(_SC_PAGESIZE);
    if(page<=0 || ((unsigned long)page&((unsigned long)page-1)) ||
       bytes>UINT32_MAX-((unsigned long)page-1)) return q2_error(EOVERFLOW);
    uint32_t size=(bytes+(unsigned long)page-1)&~((unsigned long)page-1);
    if(slot->native_extra_map && slot->native_extra_size<size) return q2_error(ENOSPC);
    if(!slot->native_extra_map) {
        int fd=-1;
        if(s->allocate_extra(size,&fd)) return -1;
        void *mapping=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
        if(mapping==MAP_FAILED) {int error=errno;syscall(SYS_close,fd);return q2_error(error);}
        slot->native_extra_fd=fd;slot->native_extra_map=mapping;slot->native_extra_size=size;
    }
    if(q2_extra_access(s,slot->native_extra_fd,DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE)) return -1;
    memset(slot->native_extra_map,0,slot->native_extra_size);
    slot->native_extra_used=0;
    return q2_extra_access(s,slot->native_extra_fd,DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE);
}
static int q2_metadata_buffer(struct q2_session *s,unsigned long request,struct v4l2_buffer *buffer) {
    if(buffer->length!=2 || !buffer->m.planes) return q2_error(EINVAL);
    struct q2_queue *q=&s->queues[1];
    struct v4l2_format format={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
    void *mapping=NULL;int fd=-1;
    struct v4l2_plane extra={0};
    size_t required=360;
    if(s->native_extra_mask) {
        /* QBUF is legal before SOURCE_CHANGE, but G_FMT is not. Use the
         * last negotiated capacity, refreshed by real format/reconfig ioctls. */
        uint32_t capacity=s->native_extra_capacity;
        if(!capacity || capacity>UINT32_MAX-336U) return q2_error(ENODATA);
        required=336U+capacity;
    }
    if(request==VIDIOC_QBUF || request==VIDIOC_PREPARE_BUF) {
        if(buffer->index>=q->count || !q->slots || q->slots[buffer->index].queued) return q2_error(EINVAL);
        extra=buffer->m.planes[1];

        long page=sysconf(_SC_PAGESIZE);
        if(page<=0 || extra.reserved[0]>INT_MAX || extra.length<required ||
           extra.reserved[1]%(unsigned long)page || extra.data_offset || extra.bytesused>extra.length)
            return q2_error(EOPNOTSUPP);
        fd=(int)syscall(SYS_fcntl,(int)extra.reserved[0],F_DUPFD_CLOEXEC,0);
        if(fd<0) return -1;
        struct stat st;
        if(fstat(fd,&st)) {int error=errno;syscall(SYS_close,fd);return q2_error(error);}
        if(st.st_size<=0 || (uint64_t)extra.reserved[1]+extra.length>(uint64_t)st.st_size) {
            syscall(SYS_close,fd);return q2_error(EINVAL);
        }
        mapping=mmap(NULL,extra.length,PROT_READ|PROT_WRITE,MAP_SHARED,fd,extra.reserved[1]);
        if(mapping==MAP_FAILED) {int error=errno;syscall(SYS_close,fd);return q2_error(error);}
        if(s->native_extra_mask && q2_native_extra_prepare(s,&q->slots[buffer->index],
                                                    s->native_extra_capacity)) {
            int error=errno;munmap(mapping,extra.length);syscall(SYS_close,fd);return q2_error(error);
        }
    }
    if(request==VIDIOC_DQBUF) {
        if(s->backend(s->fd,VIDIOC_G_FMT,&format)) return -1;
        if((format.fmt.pix_mp.pixelformat!=V4L2_PIX_FMT_NV12 &&
            format.fmt.pix_mp.pixelformat!=V4L2_PIX_FMT_P010) ||
           !format.fmt.pix_mp.width || !format.fmt.pix_mp.height)
            return q2_error(EOPNOTSUPP);
    }
    struct v4l2_buffer native=*buffer;struct v4l2_plane plane=buffer->m.planes[0];
    native.length=1;native.m.planes=&plane;
    int r=q2_user_buffer(s,request,&native),error=errno;
    if(r) {
        if(mapping) {munmap(mapping,extra.length);syscall(SYS_close,fd);}
        return q2_error(error);
    }
    struct q2_buffer_slot *slot=&q->slots[native.index];
    if(mapping) {
        if(slot->extra_map) {munmap(slot->extra_map,slot->extra.length);syscall(SYS_close,slot->extra_fd);}
        slot->extra_map=mapping;slot->extra_fd=fd;slot->extra=extra;
    }
    size_t written=0;
    if(request==VIDIOC_DQBUF && slot->extra_map) {
        int donor_access=0,native_access=0,error=0;size_t used=0;
        if(q2_extra_access(s,slot->extra_fd,DMA_BUF_SYNC_START|DMA_BUF_SYNC_WRITE)) error=errno;
        else {
            donor_access=1;
            memset(slot->extra_map,0,slot->extra.length);
            uint32_t end=24;memcpy(slot->extra_map,&end,sizeof(end));
        }
        if(!error && slot->native_extra_used) {
            if(q2_extra_access(s,slot->native_extra_fd,DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ)) error=errno;
            else {
                native_access=1;
                if(q2_extra_records(slot->native_extra_map,slot->native_extra_used,&used)) error=errno;
                else if(used>slot->extra.length-336U) error=ENOSPC;

            }
        }
        if(!error) {
            q2_crop_extra(slot->extra_map,&plane,&format,native.sequence);written=360;
            if(used) {memcpy((char *)slot->extra_map+336,slot->native_extra_map,used);written=336+used;}
        }
        if(native_access && q2_extra_access(s,slot->native_extra_fd,DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ)) error=errno;
        if(donor_access && q2_extra_access(s,slot->extra_fd,DMA_BUF_SYNC_END|DMA_BUF_SYNC_WRITE)) error=errno;
        if(error) {
            native.flags|=Q2_BUF_DATA_CORRUPT;written=0;
            (void)q2_enqueue(s,Q2_SYS_ERROR,NULL,0);
        }

    }
    native.length=2;native.m.planes=buffer->m.planes;*buffer=native;
    buffer->m.planes[0]=plane;buffer->m.planes[1]=slot->extra;
    buffer->m.planes[1].bytesused=(uint32_t)written;
    return 0;
}
static int q2_source_event(struct q2_session *s, const struct v4l2_event *native) {
    if (native->type != V4L2_EVENT_SOURCE_CHANGE ||
        native->u.src_change.changes != V4L2_EVENT_SRC_CH_RESOLUTION) return q2_error(EOPNOTSUPP);
    uint32_t data[8];memcpy(data,native->u.data,sizeof(data));

    /* Only paired-kernel HFI metadata is accepted. NV12 alone does not prove
     * the coded bit depth, progressive flag or stream colour primaries. */
    if(data[1]!=0x51324d31 || (data[2]&7)!=7 || !data[3] || !data[4] ||
       (data[5]!=0 && data[5]!=0x20002) || data[6]>1) return q2_error(EOPNOTSUPP);
    /* Downstream initializes absent colour_space to 0 (unspecified); VP8
     * firmware legitimately omits it. Do not infer a colour standard. */
    uint32_t donor[]={data[3],data[4],data[5]==0x20002?1U:0U,data[6],
                      data[2]&8 ? data[7] : 0};
    if(s->source_changes<UINT32_MAX)s->source_changes++;
    if(q2_has_native_layout(s)){s->layout_pending=1;s->layout_stride=s->layout_scan=0;}
    /* Upstream requires CAPTURE setup/restart even when old storage fits. */
    return q2_enqueue(s,Q2_RECONFIG,donor,5);
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
        if (query->id == V4L2_CID_MPEG_VIDEO_H264_PROFILE) {
            struct v4l2_queryctrl native = *query;
            if (s->backend(s->fd, request, &native)) return -1;
            if (native.minimum < 0 || native.maximum >= 32 || native.minimum > native.maximum)
                return q2_error(EOVERFLOW);
            uint32_t supported = 0;
            for (int i = native.minimum; i <= native.maximum; i++) {
                struct v4l2_querymenu menu = {.id = native.id, .index = (uint32_t)i};
                if (!s->backend(s->fd, VIDIOC_QUERYMENU, &menu)) supported |= 1U << i;
                else if (errno != EINVAL) return -1;
            }
            /* Quest reads QUERYCTRL offset 56 (flags) as a supported-profile mask. */
            native.flags = supported;
            *query = native; return 0;
        }
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
            if (s->queued || s->streaming) {
                /* The paired Venus handler waits for the real HFI acknowledgement
                 * and returns software-owned buffers through the real VB2 queue. */
                if (s->backend(s->fd,request,cmd)) return -1;
            }
            if (request == VIDIOC_TRY_DECODER_CMD) return 0;
            if (cmd->flags&1) s->draining=0;
            return q2_enqueue(s, Q2_FLUSH_DONE, &cmd->flags, 1);
        }
        if (cmd->cmd == Q2_CMD_CONTINUE) return q2_error(EOPNOTSUPP);
    }
    if (request == VIDIOC_REQBUFS) return q2_reqbufs(s,argument);
    if ((request==VIDIOC_QBUF || request==VIDIOC_PREPARE_BUF || request==VIDIOC_QUERYBUF || request==VIDIOC_DQBUF) &&
        s->metadata_plane && ((struct v4l2_buffer *)argument)->type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
        ((struct v4l2_buffer *)argument)->memory==V4L2_MEMORY_USERPTR)
        return q2_metadata_buffer(s,request,argument);
    if(request==VIDIOC_G_FMT && ((struct v4l2_format *)argument)->type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
       s->layout_pending && !s->queues[1].count && !(s->streaming&2) && q2_native_layout(s,0))return -1;
    if(request==VIDIOC_S_FMT || request==VIDIOC_TRY_FMT) {
        struct v4l2_format *f=argument;
        if(f->type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
           f->fmt.pix_mp.pixelformat==Q2_PIX_FMT_P010_VENUS)
            f->fmt.pix_mp.pixelformat=V4L2_PIX_FMT_P010;
        if(request==VIDIOC_S_FMT && f->type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && s->layout_stride) {
            /* Bit-depth selection can follow SOURCE_CHANGE's initial NV12 layout. */
            if(f->fmt.pix_mp.pixelformat!=s->layout_format &&
               q2_native_layout(s,f->fmt.pix_mp.pixelformat))return -1;
            f->fmt.pix_mp.height=s->layout_scan;
            f->fmt.pix_mp.plane_fmt[0].bytesperline=s->layout_stride;
            f->fmt.pix_mp.plane_fmt[0].sizeimage=s->layout_size;
        }
    }
    if ((request==VIDIOC_S_FMT || request==VIDIOC_TRY_FMT) && s->metadata_plane) {
        struct v4l2_format *f=argument;
        if(f->type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && f->fmt.pix_mp.num_planes==2) {
            f->fmt.pix_mp.num_planes=s->native_extra_mask?2:1;
            memset(&f->fmt.pix_mp.plane_fmt[1],0,sizeof(f->fmt.pix_mp.plane_fmt[1]));
        }
    }
    if ((request == VIDIOC_QBUF || request == VIDIOC_PREPARE_BUF || request == VIDIOC_QUERYBUF || request == VIDIOC_DQBUF) &&
        ((struct v4l2_buffer *)argument)->memory == V4L2_MEMORY_USERPTR) return q2_user_buffer(s,request,argument);
    /* Standard requests retain their real errno. Private unknown ioctl numbers
     * are never converted to success, including VIDIOC_{REGISTER,RELEASE}_BUFFER. */
    int result = request==VIDIOC_S_FMT ? q2_set_format(s,argument) :
        s->backend(s->fd, request, argument); int saved = errno;
    if(!result && request==VIDIOC_S_FMT && s->native_extra_enabled) {
        struct v4l2_format *f=argument;
        if(f->type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
            uint32_t mask=f->fmt.pix_mp.pixelformat==V4L2_PIX_FMT_HEVC?7:
                          f->fmt.pix_mp.pixelformat==V4L2_PIX_FMT_H264?1:0;
            if(mask!=s->native_extra_mask) {
                struct v4l2_control extra={.id=Q2_NATIVE_EXTRA_CTRL,.value=(int)mask};
                if(s->backend(s->fd,VIDIOC_S_CTRL,&extra)) return -1;
                s->native_extra_mask=mask;
            }
        }
    }
    if (!result && request == VIDIOC_DECODER_CMD) {
        uint32_t cmd=((struct v4l2_decoder_cmd *)argument)->cmd;
        if(cmd==V4L2_DEC_CMD_STOP) s->draining=1;
        else if(cmd==V4L2_DEC_CMD_START) s->draining=0;
    }
    if (!result && (request==VIDIOC_G_FMT || request==VIDIOC_S_FMT || request==VIDIOC_TRY_FMT)) {
        struct v4l2_format *f=argument;
        struct v4l2_pix_format_mplane *p=&f->fmt.pix_mp;
        if(f->type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && s->native_extra_mask &&
           request!=VIDIOC_TRY_FMT) {
            if(p->num_planes!=2) return q2_error(EPROTO);
            s->native_extra_capacity=p->plane_fmt[1].sizeimage;
        }
        /* Both linear formats use the real byte stride and 32-aligned height.
         * QP10 and P010 name the same Venus linear layout in these paired drivers. */
        if(f->type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
           (p->pixelformat==V4L2_PIX_FMT_NV12 || p->pixelformat==V4L2_PIX_FMT_P010) && p->num_planes==(s->native_extra_mask?2:1) &&
           p->height && p->height<=UINT16_MAX && !(p->height&31) &&
           p->plane_fmt[0].bytesperline>=(uint64_t)p->width*(p->pixelformat==V4L2_PIX_FMT_P010?2:1) &&
           (uint64_t)p->plane_fmt[0].bytesperline*p->height*3/2<=p->plane_fmt[0].sizeimage)
        {
            p->plane_fmt[0].reserved[0]=p->height;
            uint64_t need=s->native_extra_mask?336ULL+p->plane_fmt[1].sizeimage:360;
            long page=sysconf(_SC_PAGESIZE);
            if(page<=0 || ((unsigned long)page&((unsigned long)page-1)) ||
               need>UINT32_MAX-((unsigned long)page-1)) return q2_error(EOVERFLOW);
            p->num_planes=2;p->plane_fmt[1].sizeimage=(need+page-1)&~((uint64_t)page-1);
            if(request!=VIDIOC_TRY_FMT)s->metadata_plane=1;
            if(p->pixelformat==V4L2_PIX_FMT_P010)p->pixelformat=Q2_PIX_FMT_P010_VENUS;
        }
    }
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
static int q2_native_extra_enabled;
static uint64_t q2_next_node_token;
static _Thread_local struct { uint64_t token; unsigned opens; } q2_node_scope;
static int (*q2_next_ioctl)(int, request_t, ...);
static int (*q2_next_poll)(struct pollfd *, nfds_t, int);
static int (*q2_next_ppoll)(struct pollfd *, nfds_t, const struct timespec *, const sigset_t *);
static int q2_real_ioctl(int fd, unsigned long request, void *argument) {
    return q2_next_ioctl ? q2_next_ioctl(fd, (request_t)request, argument) : (int)syscall(SYS_ioctl, fd, request, argument);
}
#include "../graphics/quest2-system-ion.h"
int ion_open(void) {
    if (!q2_enabled) {
        int (*next)(void)=dlsym(RTLD_NEXT,"ion_open");
        return next ? next() : q2_error(ENOSYS);
    }
    return (int)syscall(SYS_openat,AT_FDCWD,"/dev/dma_heap/system",O_RDONLY|O_CLOEXEC,0);
}
int ion_close(int fd) {
    if (!q2_enabled) {
        int (*next)(int)=dlsym(RTLD_NEXT,"ion_close");
        return next ? next(fd) : q2_error(ENOSYS);
    }
    return (int)syscall(SYS_close,fd);
}
int ion_alloc_fd(int fd,size_t len,size_t align,unsigned mask,unsigned flags,int *handle) {
    if (!q2_enabled) {
        int (*next)(int,size_t,size_t,unsigned,unsigned,int *)=dlsym(RTLD_NEXT,"ion_alloc_fd");
        if (next) return next(fd,len,align,mask,flags,handle);
        errno=ENOSYS;return -ENOSYS;
    }
    return q2_system_ion_alloc(fd,len,align,mask,flags,handle);
}

static int q2_aux_minimum(struct q2_session *s,uint32_t id,int *answer) {
    if (!q2_aux_enabled) return q2_error(EAGAIN);
    struct v4l2_format formats[2]={{.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE},{.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE}};
    for(unsigned i=0;i<2;i++) {
        int r=q2_real_ioctl(s->fd,VIDIOC_G_FMT,&formats[i]);int error=errno;

        if(r) return q2_error(error);
        if(!formats[i].fmt.pix_mp.width || !formats[i].fmt.pix_mp.height || !formats[i].fmt.pix_mp.pixelformat) return q2_error(EPROTO);
    }
    char proc[64],node[PATH_MAX];snprintf(proc,sizeof(proc),"/proc/self/fd/%d",s->fd);
    ssize_t n=readlink(proc,node,sizeof(node)-1);if(n<0) return -1;
    node[n]=0;
    int aux=(int)syscall(SYS_openat,AT_FDCWD,node,O_RDWR|O_NONBLOCK|O_CLOEXEC,0);

    if(aux<0) return -1;
    int result=-1,error=0;
    for(unsigned i=0;i<2;i++) {
        struct v4l2_format expected=formats[i];
        int r=q2_real_ioctl(aux,VIDIOC_S_FMT,&formats[i]);error=errno;

        if(r) goto done;
        if(formats[i].fmt.pix_mp.width!=expected.fmt.pix_mp.width || formats[i].fmt.pix_mp.height!=expected.fmt.pix_mp.height ||
           formats[i].fmt.pix_mp.pixelformat!=expected.fmt.pix_mp.pixelformat) {error=ERANGE;goto done;}
    }
    if(s->native_extra_mask) {
        struct v4l2_control extra={.id=Q2_NATIVE_EXTRA_CTRL,.value=(int)s->native_extra_mask};
        if(q2_real_ioctl(aux,VIDIOC_S_CTRL,&extra)) {error=errno;goto done;}
    }
    struct v4l2_requestbuffers buffers={.count=1,.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,.memory=V4L2_MEMORY_DMABUF};
    int r=q2_real_ioctl(aux,VIDIOC_REQBUFS,&buffers);error=errno;

    if(r) goto done;
    struct v4l2_control control={.id=id};
    r=q2_real_ioctl(aux,VIDIOC_G_CTRL,&control);error=errno;

    if(r) goto done;
    if(control.value<=0) {error=EPROTO;goto done;}
    *answer=control.value;result=0;
done:
    r=(int)syscall(SYS_close,aux);

    if(r && !result) {error=errno;result=-1;}
    errno=error;return result;
}
static struct q2_session *q2_lookup(int fd) {
    for (struct q2_fd *p = q2_fds; p; p = p->next) if (p->fd == fd) return p->session;
    return NULL;
}
static int q2_cpu_access(int fd,uint64_t flags) {
    struct dma_buf_sync sync={.flags=flags};int r;
    do {r=(int)syscall(SYS_ioctl,fd,DMA_BUF_IOCTL_SYNC,&sync);} while(r<0 && errno==EINTR);
    return r;
}
static int q2_allocate_extra(size_t size,int *fd) {
    int heap=(int)syscall(SYS_openat,AT_FDCWD,"/dev/dma_heap/system",O_RDONLY|O_CLOEXEC,0);
    if(heap<0) return -1;
    struct dma_heap_allocation_data alloc={.len=size,.fd_flags=O_RDWR|O_CLOEXEC};
    int r=q2_real_ioctl(heap,DMA_HEAP_IOCTL_ALLOC,&alloc),error=errno;
    syscall(SYS_close,heap);
    if(r) return q2_error(error);
    *fd=(int)alloc.fd;return 0;
}
static int q2_sync_buffer(int fd) {
    struct dma_buf_sync sync={.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_RW};
    int r;
    do { r=(int)syscall(SYS_ioctl,fd,DMA_BUF_IOCTL_SYNC,&sync); } while(r<0 && errno==EINTR);
    if(r) return r;
    sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_RW;
    do { r=(int)syscall(SYS_ioctl,fd,DMA_BUF_IOCTL_SYNC,&sync); } while(r<0 && errno==EINTR);
    return r;
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
uint64_t quest2_vidc_node_begin(void) {
    if(q2_node_scope.token) {errno=EBUSY;return 0;}
    if(!q2_enabled) {errno=EOPNOTSUPP;return 0;}
    pthread_mutex_lock(&q2_lock);
    uint64_t token=q2_next_node_token==UINT64_MAX?0:++q2_next_node_token;
    pthread_mutex_unlock(&q2_lock);
    if(!token) {errno=EOVERFLOW;return 0;}
    q2_node_scope.token=token;q2_node_scope.opens=0;return token;
}
/* Called once after a real decoder open is attached, never for aliases/aux fds. */
static void q2_node_open(struct q2_session *s) {
    s->node_token=q2_node_scope.token;
    if(s->node_token && q2_node_scope.opens<2)q2_node_scope.opens++;
}
int quest2_vidc_node_end(uint64_t token) {
    uint64_t active=q2_node_scope.token;unsigned opens=q2_node_scope.opens;
    q2_node_scope.token=0;q2_node_scope.opens=0;
    int error=(!active || active!=token)?EINVAL:opens!=1?EPROTO:0;
    struct q2_session *found=NULL;
    pthread_mutex_lock(&q2_lock);
    for(struct q2_fd *p=q2_fds;active && p;p=p->next)if(p->session->node_token==active) {
        if(found && found!=p->session)error=EPROTO;
        found=p->session;
    }
    if(!error && !found)error=ENODEV;
    for(struct q2_fd *p=q2_fds;active && p;p=p->next)if(p->session->node_token==active) {
        p->session->node_bound=!error;
        if(error)p->session->node_token=0;
    }
    pthread_mutex_unlock(&q2_lock);
    return error?q2_error(error):0;
}
/* s->fd can be a closed previous ioctl pin. Always pin a live registry alias. */
static struct q2_session *q2_node_pin(uint64_t token,int *fd) {
    if(!token) {errno=EINVAL;return NULL;}
    pthread_mutex_lock(&q2_lock);
    struct q2_session *s=NULL;
    for(struct q2_fd *p=q2_fds;p;p=p->next)
        if(p->session->node_token==token && p->session->node_bound) {
            *fd=(int)syscall(SYS_fcntl,p->fd,F_DUPFD_CLOEXEC,0);
            if(*fd>=0) {s=p->session;s->refs++;}
            int error=errno;pthread_mutex_unlock(&q2_lock);errno=error;return s;
        }
    pthread_mutex_unlock(&q2_lock);errno=ENODEV;return NULL;
}
static void q2_node_unpin(struct q2_session *s,int fd) {
    int error=errno;(void)syscall(SYS_close,fd);
    pthread_mutex_lock(&q2_lock);struct q2_session *dead=q2_release(s);pthread_mutex_unlock(&q2_lock);
    q2_dispose(dead);errno=error;
}
int quest2_vidc_node_configure(uint64_t token,int native,uint64_t usage,int usage_valid) {
    if((native!=0 && native!=1) || (usage_valid!=0 && usage_valid!=1))return q2_error(EINVAL);
    if(!native || !usage_valid)usage=0;
    if(!native)usage_valid=0;
    int fd=-1;struct q2_session *s=q2_node_pin(token,&fd);if(!s)return -1;
    pthread_mutex_lock(&s->mutex);s->fd=fd;int result=0;
    if(s->node_native!=native || s->node_usage!=usage || s->node_usage_valid!=usage_valid) {
        int busy=s->streaming || s->queued;
        const struct q2_queue *q=&s->queues[1];
        if(q->count && !q->slots)busy=1;
        for(unsigned i=0;!busy && i<q->count;i++)
            busy=q->slots[i].valid || q->slots[i].queued || q->slots[i].extra_map || q->slots[i].native_extra_map;
        if(busy)result=q2_error(EBUSY);
        else {
            int (*callback)(unsigned,unsigned,unsigned,uint64_t,unsigned,unsigned*,unsigned*,unsigned*)=NULL;
            int (*legacy)(unsigned,unsigned,unsigned,uint64_t,unsigned*,unsigned*,unsigned*)=NULL;
            if(native) {
                callback=dlsym(RTLD_DEFAULT,"quest2_omx_capture_layout_for_usage_v2");
                if(!callback)legacy=dlsym(RTLD_DEFAULT,"quest2_omx_capture_layout_for_usage");
            }
            if(native && !callback && !legacy)result=q2_error(ENOSYS);
            else {
                s->node_native=native;s->node_usage=usage;s->node_usage_valid=usage_valid;
                s->native_layout_for_usage=callback;s->native_layout_for_usage_v1=legacy;s->native_layout=NULL;
                if(s->layout_stride || s->source_changes)s->layout_pending=native;
                if(!native)s->layout_pending=0;
                s->layout_stride=s->layout_scan=s->layout_width=s->layout_height=s->layout_format=s->layout_size=0;
            }
        }
    }
    int error=errno;pthread_mutex_unlock(&s->mutex);q2_node_unpin(s,fd);errno=error;return result;
}
int quest2_vidc_node_get_layout(uint64_t token,struct quest2_vidc_node_layout *layout) {
    if(!layout)return q2_error(EINVAL);
    int fd=-1;struct q2_session *s=q2_node_pin(token,&fd);if(!s)return -1;
    pthread_mutex_lock(&s->mutex);s->fd=fd;int result=0;
    if(!s->node_native || !s->node_usage_valid)result=q2_error(EINVAL);
    else if(s->layout_pending)result=q2_native_layout(s,0);
    if(!result && !s->layout_stride)result=q2_error(s->source_changes?EPROTO:EAGAIN);
    if(!result)*layout=(struct quest2_vidc_node_layout){
        s->layout_width,s->layout_height,s->layout_format,s->layout_stride,s->layout_scan,s->layout_size,s->node_usage};
    int error=errno;pthread_mutex_unlock(&s->mutex);q2_node_unpin(s,fd);errno=error;return result;
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
        if (h264) { selected = fd;  break; }
        (void)syscall(SYS_close, fd);
    }
    closedir(dir); if (selected < 0) errno = failure; return selected;
}
__attribute__((constructor)) static void q2_initialize(void) {
    q2_next_ioctl = dlsym(RTLD_NEXT, "ioctl"); q2_next_poll = dlsym(RTLD_NEXT, "poll"); q2_next_ppoll = dlsym(RTLD_NEXT, "ppoll");
    const char *enabled = getenv("QUEST2_VIDC_BRIDGE"); q2_enabled = enabled && !strcmp(enabled, "1");
    const char *aux = getenv("QUEST2_VIDC_AUX_CAPS"); q2_aux_enabled = aux && !strcmp(aux, "1");
    const char *extra=getenv("QUEST2_VIDC_NATIVE_EXTRA");
    q2_native_extra_enabled=extra?!strcmp(extra,"1"):QUEST2_NATIVE_EXTRA_DEFAULT;
}
static int q2_openat(int dir, const char *path, int flags, mode_t mode) {
    if (!q2_enabled || !path || (strcmp(path, "/dev/video32") && strcmp(path, "/dev/video/q6_dec")))
        return (int)syscall(SYS_openat, dir, path, flags, mode);
    if (flags & (O_CREAT | O_TRUNC)) return q2_error(EINVAL);
    /* The donor polls then drains DQBUF until EAGAIN. On upstream Venus a
     * blocking fd can sleep in that drain, preventing the other queue's QBUF.
     * Only this private redirected descriptor is made nonblocking. */
    int fd = q2_decoder(flags | O_NONBLOCK); if (fd < 0) return fd;
    uint32_t codec_config_flag=0;
    int capfd=(int)syscall(SYS_openat,AT_FDCWD,"/sys/module/venus_dec/parameters/sq2_omx_config_flag",O_RDONLY|O_CLOEXEC,0);
    if(capfd>=0) {
        char text[32]={0};ssize_t n=read(capfd,text,sizeof(text)-1);
        syscall(SYS_close,capfd);
        if(n>0) {char *end=NULL;unsigned long flag=strtoul(text,&end,10);
            if(flag==Q2_NATIVE_CODECCONFIG && (*end=='\n'||!*end)) codec_config_flag=(uint32_t)flag;}
    }
    struct q2_session *s = calloc(1, sizeof(*s));
    if (!s) { int e = errno; syscall(SYS_close, fd); return q2_error(e); }
    s->codec_config_flag=codec_config_flag;
    s->native_extra_enabled=q2_native_extra_enabled;
    const char *layout=getenv("QUEST2_VIDC_NATIVE_LAYOUT");
    if(!q2_node_scope.token && layout && !strcmp(layout,"1")) {
        s->native_layout=dlsym(RTLD_DEFAULT,"quest2_omx_capture_layout");
        if(!s->native_layout){free(s);syscall(SYS_close,fd);return q2_error(ENOSYS);}
    }
    s->cpu_access=q2_cpu_access;s->allocate_extra=q2_allocate_extra;
    s->fd = fd; s->backend = q2_real_ioctl; s->sync_buffer=q2_sync_buffer; s->query_minimum=q2_aux_enabled ? q2_aux_minimum : NULL;
    int init = pthread_mutex_init(&s->mutex, NULL);
    if (init) { free(s); syscall(SYS_close, fd); return q2_error(init); }
    s->wake = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
    if (s->wake < 0) { int e = errno; pthread_mutex_destroy(&s->mutex); free(s); syscall(SYS_close, fd); return q2_error(e); }
    pthread_mutex_lock(&q2_lock); int result = q2_attach(fd, s);
    if(!result)q2_node_open(s);
    pthread_mutex_unlock(&q2_lock);
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
