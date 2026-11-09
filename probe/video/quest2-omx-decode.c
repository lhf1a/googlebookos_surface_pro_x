/* Standard non-secure OMX video client, not a decoder or a firmware loader.
 * An explicit invocation uses the selected vendor component and real buffers.
 * AU sizes/PTS come from the caller's demuxer. No compressed data is decoded here.
 */
#define _GNU_SOURCE
#include <OMX_Component.h>
#include "quest2-omx-session.h"
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef QUEST2_NATIVE_OUTPUT
#include "quest2-omx-native-output.h"
#endif
#ifdef QUEST2_NATIVE_HWC
#include "../graphics/quest2-vpu-hwc.h"
extern int quest2_capture_display(unsigned,int64_t,int *);
extern int quest2_capture_display_flush(int *);
extern int quest2_capture_queue(unsigned);
#endif

_Static_assert(sizeof(void *) == 8 && sizeof(OMX_U32) == 4, "64-bit OMX ABI required");

#define INIT(x) do { memset(&(x), 0, sizeof(x)); (x).nSize = sizeof(x); \
    (x).nVersion.s.nVersionMajor = 1; (x).nVersion.s.nVersionMinor = 1; \
    (x).nVersion.s.nRevision = 2; } while (0)
/* QCOM OMX extension: Venus linear NV12 (not UBWC); retain raw output until
 * returned storage geometry has been validated separately. */
#define Q2_COLOR_VENUS_NV12 ((OMX_COLOR_FORMATTYPE)0x7fa30c04)
enum ownership { APP, COMPONENT, READY, DISPLAY };
struct slot { OMX_BUFFERHEADERTYPE *header; enum ownership owner; uint64_t order; };
struct port { OMX_PARAM_PORTDEFINITIONTYPE definition; struct slot *slots; size_t count; };
struct au { size_t bytes; int64_t pts; };
struct client {
    OMX_COMPONENTTYPE *component;
    OMX_HANDLETYPE handle;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    OMX_COMMANDTYPE command;
    OMX_U32 argument;
    bool pending, reconfigure, crop_changed, cleaning;
    OMX_ERRORTYPE error;
    OMX_STATETYPE state;
    uint64_t sequence;
    struct port input, output;
    OMX_VIDEO_CODINGTYPE compression;
    OMX_CONFIG_RECTTYPE crop;
    OMX_INDEXTYPE metadata_indices[3];
    unsigned timeout_ms, requested_input_buffers;
    bool output_eos, event_eos;
#ifdef QUEST2_NATIVE_OUTPUT
    bool native_output;
    uint32_t native_usage;
#endif
#ifdef QUEST2_NATIVE_HWC
    struct slot *display_retired;
#endif
    size_t frames, linear_frames;
    const char *directory;
    FILE *metadata;
    quest2_omx_deliver deliver;
    void *opaque;
};

static OMX_ERRORTYPE event(OMX_HANDLETYPE h, OMX_PTR opaque, OMX_EVENTTYPE type,
                          OMX_U32 a, OMX_U32 b, OMX_PTR extra) {
    (void)h; (void)extra;
    struct client *c = opaque;
    printf("EVENT type=%u data1=0x%08x data2=0x%08x\n", (unsigned)type, a, b);
    pthread_mutex_lock(&c->mutex);
    if (type == OMX_EventError) c->error = (OMX_ERRORTYPE)a;
    if (type == OMX_EventCmdComplete && c->pending &&
        a == (OMX_U32)c->command && b == c->argument) {
        c->pending = false;
        if (a == OMX_CommandStateSet) c->state = (OMX_STATETYPE)b;
    }
    if (type == OMX_EventPortSettingsChanged && a == c->output.definition.nPortIndex) {
        bool metadata = false;
        for (unsigned i = 0; i < 3; i++)
            metadata |= c->metadata_indices[i] && b == (OMX_U32)c->metadata_indices[i];
        if (metadata) {
            /* Color/HDR updates do not change buffer geometry. Resetting the
             * port here discards queued frames; NDK probes verify their values. */
            printf("METADATA_CHANGED index=0x%08x buffers_preserved=1\n", b);
        } else if (b == OMX_IndexConfigCommonOutputCrop) c->crop_changed = true;
        else c->reconfigure = true;
    }
    if (type == OMX_EventBufferFlag && a == c->output.definition.nPortIndex && (b & OMX_BUFFERFLAG_EOS))
        c->event_eos = true;
    pthread_cond_broadcast(&c->changed);
    pthread_mutex_unlock(&c->mutex);
    return OMX_ErrorNone;
}

static OMX_ERRORTYPE returned(OMX_HANDLETYPE h, OMX_PTR opaque, OMX_BUFFERHEADERTYPE *b, bool output) {
    (void)h;
    struct client *c = opaque;
    struct slot *slot = b->pAppPrivate;
    pthread_mutex_lock(&c->mutex);
    slot->owner = output ? READY : APP;
    slot->order = ++c->sequence;
    pthread_cond_broadcast(&c->changed);
    pthread_mutex_unlock(&c->mutex);
    return OMX_ErrorNone;
}
static OMX_ERRORTYPE empty_done(OMX_HANDLETYPE h, OMX_PTR p, OMX_BUFFERHEADERTYPE *b) { return returned(h, p, b, false); }
static OMX_ERRORTYPE fill_done(OMX_HANDLETYPE h, OMX_PTR p, OMX_BUFFERHEADERTYPE *b) { return returned(h, p, b, true); }

/* Inactivity timeout belongs to this diagnostic process, not to product policy.
 * --timeout-ms 0 disables it; an external supervisor can bound a stuck vendor call.
 */
static bool pause_locked(struct client *c) {
    if (c->error != OMX_ErrorNone && !c->cleaning) return false;
    int result;
    if (!c->timeout_ms) result = pthread_cond_wait(&c->changed, &c->mutex);
    else {
        struct timespec until;
        clock_gettime(CLOCK_MONOTONIC, &until);
        until.tv_sec += c->timeout_ms / 1000;
        until.tv_nsec += (long)(c->timeout_ms % 1000) * 1000000;
        if (until.tv_nsec >= 1000000000) { ++until.tv_sec; until.tv_nsec -= 1000000000; }
        result = pthread_cond_timedwait(&c->changed, &c->mutex, &until);
    }
    if (result) fprintf(stderr, "WAIT_ERROR=%d\n", result);
    return result == 0 && (c->error == OMX_ErrorNone || c->cleaning);
}
static bool checked(const char *operation, OMX_ERRORTYPE result) {
    printf("API %s=0x%08x\n", operation, (unsigned)result);
    return result == OMX_ErrorNone;
}
static bool send_command(struct client *c, OMX_COMMANDTYPE command, OMX_U32 argument) {
    pthread_mutex_lock(&c->mutex);
    if (c->pending || (c->error != OMX_ErrorNone && !c->cleaning)) { pthread_mutex_unlock(&c->mutex); return false; }
    c->command = command; c->argument = argument; c->pending = true;
    pthread_mutex_unlock(&c->mutex);
    printf("COMMAND command=%u argument=%u\n", (unsigned)command, argument);
    if (!checked("SendCommand", c->component->SendCommand(c->handle, command, argument, NULL))) {
        pthread_mutex_lock(&c->mutex); c->pending = false; pthread_mutex_unlock(&c->mutex);
        return false;
    }
    return true;
}
static bool wait_command(struct client *c) {
    bool ok = true;
    pthread_mutex_lock(&c->mutex);
    while (c->pending && ok) ok = pause_locked(c);
    ok = ok && (c->error == OMX_ErrorNone || c->cleaning);
    pthread_mutex_unlock(&c->mutex);
    return ok;
}
static bool definition(struct client *c, struct port *port) {
    OMX_U32 index = port->definition.nPortIndex;
    INIT(port->definition); port->definition.nPortIndex = index;
    if (!checked("GetParameter(PortDefinition)", c->component->GetParameter(c->handle,
        OMX_IndexParamPortDefinition, &port->definition))) return false;
    OMX_PARAM_PORTDEFINITIONTYPE *p = &port->definition;
    printf("PORT index=%u direction=%u count=%u min=%u size=%u enabled=%u width=%u height=%u stride=%d slice=%u color=0x%x compression=%u\n",
        p->nPortIndex, (unsigned)p->eDir, p->nBufferCountActual, p->nBufferCountMin, p->nBufferSize,
        p->bEnabled, p->format.video.nFrameWidth, p->format.video.nFrameHeight,
        p->format.video.nStride, p->format.video.nSliceHeight, (unsigned)p->format.video.eColorFormat,
        (unsigned)p->format.video.eCompressionFormat);
    return p->eDomain == OMX_PortDomainVideo && p->nBufferCountActual >= p->nBufferCountMin &&
           p->nBufferCountActual > 0 && p->nBufferSize > 0;
}
static bool crop(struct client *c) {
    INIT(c->crop); c->crop.nPortIndex = c->output.definition.nPortIndex;
    OMX_ERRORTYPE result = c->component->GetConfig(c->handle, OMX_IndexConfigCommonOutputCrop, &c->crop);
    printf("API GetConfig(OutputCrop)=0x%08x\n", (unsigned)result);
    if (result == OMX_ErrorUnsupportedIndex || result == OMX_ErrorNotImplemented) {
        c->crop.nLeft = c->crop.nTop = 0;
        c->crop.nWidth = c->output.definition.format.video.nFrameWidth;
        c->crop.nHeight = c->output.definition.format.video.nFrameHeight;
    } else if (result != OMX_ErrorNone) return false;
    printf("CROP left=%d top=%d width=%u height=%u\n", c->crop.nLeft, c->crop.nTop, c->crop.nWidth, c->crop.nHeight);
    return true;
}
static bool allocate(struct client *c, struct port *port) {
#ifdef QUEST2_NATIVE_OUTPUT
    if(c->native_output && port==&c->output &&
       !q2_omx_native_config(&port->definition,&c->crop,c->native_usage))return false;
#endif
    port->count = port->definition.nBufferCountActual;
    port->slots = calloc(port->count, sizeof(*port->slots));
    if (!port->slots) { port->count = 0; return false; }
    for (size_t i = 0; i < port->count; ++i) {
        struct slot *slot = &port->slots[i];
#ifdef QUEST2_NATIVE_OUTPUT
        if(c->native_output && port==&c->output) {
            if(!q2_omx_native_allocate(c->handle,c->component,&port->definition,
                                     (unsigned)i,slot,&slot->header))return false;
        } else
#endif
        if (!checked("AllocateBuffer", c->component->AllocateBuffer(c->handle, &slot->header,
            port->definition.nPortIndex, slot, port->definition.nBufferSize))) return false;
        printf("BUFFER port=%u index=%zu allocated=%u\n", port->definition.nPortIndex, i, slot->header->nAllocLen);
        if (!slot->header->pBuffer || slot->header->nAllocLen < port->definition.nBufferSize) return false;
    }
    return true;
}
static struct slot *ready_output(struct client *c) {
    struct slot *next = NULL;
    for (size_t i = 0; i < c->output.count; ++i) {
        struct slot *s = &c->output.slots[i];
        if (s->header && s->owner == READY && (!next || s->order < next->order)) next = s;
    }
    if (next) next->owner = APP;
    return next;
}
static FILE *frame_file(struct client *c, const char *suffix) {
    char *path = NULL;
    if (asprintf(&path, "%s/frame-%06zu.%s", c->directory, c->frames, suffix) < 0) return NULL;
    FILE *f = fopen(path, "wb"); free(path); return f;
}
/* Only strip documented linear row padding. This is not video decoding, UBWC
 * reconstruction, or an assumption that nFilledLen is a tightly packed image.
 */
static bool save_linear_output(struct client *c, OMX_BUFFERHEADERTYPE *b) {
    if (b->nOffset > b->nAllocLen || b->nFilledLen > b->nAllocLen - b->nOffset) return false;
    if (b->nFlags & OMX_BUFFERFLAG_EOS) c->output_eos = true;
    if (!b->nFilledLen) return true;
    OMX_VIDEO_PORTDEFINITIONTYPE *v = &c->output.definition.format.video;
    printf("FRAME index=%zu pts=%" PRId64 " bytes=%u offset=%u allocated=%u flags=0x%x width=%u height=%u stride=%d slice=%u color=0x%x\n",
        c->frames, (int64_t)b->nTimeStamp, b->nFilledLen, b->nOffset, b->nAllocLen, b->nFlags,
        v->nFrameWidth, v->nFrameHeight, v->nStride, v->nSliceHeight, (unsigned)v->eColorFormat);
    bool ok = !(b->nFlags & OMX_BUFFERFLAG_DATACORRUPT);
    if (!c->deliver) {
        FILE *raw = frame_file(c, "raw");
        if (!raw) return false;
        ok = fwrite(b->pBuffer + b->nOffset, 1, b->nFilledLen, raw) == b->nFilledLen && ok;
        if (fclose(raw)) ok = false;
    }
    bool linear = v->eColorFormat == OMX_COLOR_FormatYUV420SemiPlanar ||
                  v->eColorFormat == Q2_COLOR_VENUS_NV12;
    size_t stride = v->nStride > 0 ? (size_t)v->nStride : 0;
    size_t slice = v->nSliceHeight, width = c->crop.nWidth, height = c->crop.nHeight;
    size_t left = c->crop.nLeft >= 0 ? (size_t)c->crop.nLeft : SIZE_MAX;
    size_t top = c->crop.nTop >= 0 ? (size_t)c->crop.nTop : SIZE_MAX;
    /* Bounds and even chroma alignment follow actual returned storage geometry. */
    linear = linear && stride && slice && width && height && !(width & 1) && !(height & 1) &&
        !(left & 1) && !(top & 1) && left <= stride && width <= stride - left &&
        top <= slice && height <= slice - top && slice <= SIZE_MAX / stride;
    size_t y_size = linear ? stride * slice : 0;
    size_t last_uv = 0;
    if (linear) {
        size_t uv_row = top / 2 + height / 2 - 1;
        linear = y_size <= b->nFilledLen && left + width <= SIZE_MAX - y_size;
        linear = linear && uv_row <= (SIZE_MAX - y_size - left - width) / stride;
        if (linear) last_uv = y_size + uv_row * stride + left + width;
        linear = linear && last_uv <= b->nFilledLen;
    }
    if (c->deliver) {
        /* The C2 adapter currently exports uncropped-origin linear NV12 only. */
        if (!ok || !linear || left || top || b->nTimeStamp < 0) return false;
        struct quest2_omx_frame frame = {b->pBuffer + b->nOffset, b->nFilledLen,
            width, height, stride, slice, (uint64_t)b->nTimeStamp};
        if (c->deliver(c->opaque, &frame)) return false;
        ++c->frames; ++c->linear_frames;
        return true;
    }
    if (linear) {
        FILE *nv12 = frame_file(c, "nv12");
        if (!nv12) return false;
        const unsigned char *base = b->pBuffer + b->nOffset;
        for (size_t y = 0; y < height; ++y)
            if (fwrite(base + (top + y) * stride + left, 1, width, nv12) != width) ok = false;
        for (size_t y = 0; y < height / 2; ++y)
            if (fwrite(base + y_size + (top / 2 + y) * stride + left, 1, width, nv12) != width) ok = false;
        if (fclose(nv12)) ok = false;
        ++c->linear_frames;
    } else fprintf(stderr, "FRAME_NOT_LINEAR_NV12 index=%zu; raw retained, no fabricated conversion\n", c->frames);
    fprintf(c->metadata, "%zu\t%" PRId64 "\t%u\t%u\t%d\t%u\t%u\t%u\t0x%x\t%d\n",
        c->frames, (int64_t)b->nTimeStamp, b->nFilledLen, b->nFlags, v->nStride,
        v->nSliceHeight, c->crop.nWidth, c->crop.nHeight, (unsigned)v->eColorFormat, linear);
    if (fflush(c->metadata)) ok = false;
    ++c->frames;
    return ok && !(b->nFlags & OMX_BUFFERFLAG_DATACORRUPT);
}
static bool save_output(struct client *c, OMX_BUFFERHEADERTYPE *b) {
#ifdef QUEST2_NATIVE_HWC
    c->display_retired=NULL;
#endif
#ifdef QUEST2_NATIVE_OUTPUT
    if(c->native_output && b->nFilledLen) {
        unsigned index=0;
        while(index<c->output.count && c->output.slots[index].header!=b)index++;
        if(index==c->output.count || b->pAppPrivate!=&c->output.slots[index])return false;
        struct q2_omx_native_read read={.fd=-1};
        if(!q2_omx_native_read(index,b,&read))return false;
        OMX_VIDEO_PORTDEFINITIONTYPE original=c->output.definition.format.video;
        OMX_CONFIG_RECTTYPE original_crop=c->crop;
        /* Diagnostic CPU view only: the component header remains an opaque handle. */
        c->output.definition.format.video.nStride=read.stride;
        c->output.definition.format.video.nSliceHeight=read.scan;
        c->crop.nLeft=c->crop.nTop=0;c->crop.nWidth=read.width;c->crop.nHeight=read.height;
        bool ok=save_linear_output(c,&read.header);
        c->crop=original_crop;
        c->output.definition.format.video=original;
        ok=q2_omx_native_read_end(&read) && ok;
#ifdef QUEST2_NATIVE_HWC
        if(ok) {
            int retired=-1;
            if(quest2_capture_display(index,(int64_t)b->nTimeStamp,&retired))return false;
            pthread_mutex_lock(&c->mutex);
            bool valid=c->output.slots[index].owner==APP && retired>=-1 &&
                (retired<0 || ((unsigned)retired<c->output.count && (unsigned)retired!=index &&
                 c->output.slots[retired].owner==DISPLAY));
            if(valid) {
                c->output.slots[index].owner=DISPLAY;
                if(retired>=0) {
                    c->output.slots[retired].owner=APP;
                    c->display_retired=&c->output.slots[retired];
                }
            }
            pthread_mutex_unlock(&c->mutex);
            if(!valid)return false;
        }
#endif
        return ok;
    }
#endif
    return save_linear_output(c,b);
}
static bool refill(struct client *c, struct slot *slot) {
#ifdef QUEST2_NATIVE_HWC
    pthread_mutex_lock(&c->mutex);bool reusable=slot->owner==APP;pthread_mutex_unlock(&c->mutex);
    if(!reusable){fputs("REFUSE_REFILL_NON_APP_BUFFER\n",stderr);return false;}
    if(c->native_output) {
        size_t index=0;
        while(index<c->output.count && slot!=&c->output.slots[index])++index;
        if(index==c->output.count || quest2_capture_queue((unsigned)index))return false;
    }
#endif
    OMX_BUFFERHEADERTYPE *b = slot->header;
    b->nFilledLen = b->nOffset = b->nFlags = 0;
    pthread_mutex_lock(&c->mutex); slot->owner = COMPONENT; pthread_mutex_unlock(&c->mutex);
    OMX_ERRORTYPE result = c->component->FillThisBuffer(c->handle, b);
    if (result != OMX_ErrorNone) {
        pthread_mutex_lock(&c->mutex); slot->owner = APP; pthread_mutex_unlock(&c->mutex);
    }
    return checked("FillThisBuffer", result);
}
static bool queue_outputs(struct client *c) {
    for (size_t i = 0; i < c->output.count; ++i) if (!refill(c, &c->output.slots[i])) return false;
    return true;
}
static bool free_port(struct client *c, struct port *port) {
    bool ok = true;
#ifdef QUEST2_NATIVE_HWC
    if(c->native_output && port==&c->output) {
        int released=-1;
        if(quest2_capture_display_flush(&released))return false;
        pthread_mutex_lock(&c->mutex);
        bool valid=released>=-1 && (released<0 || ((unsigned)released<port->count &&
            port->slots[released].owner==DISPLAY));
        if(valid && released>=0)port->slots[released].owner=APP;
        c->display_retired=NULL;
        pthread_mutex_unlock(&c->mutex);
        if(!valid)return false;
    }
#endif
    for (size_t i = 0; i < port->count; ++i) {
        struct slot *slot = &port->slots[i];
        if (!slot->header) continue;
        pthread_mutex_lock(&c->mutex);
        bool owned = slot->owner != COMPONENT && slot->owner != DISPLAY;
        pthread_mutex_unlock(&c->mutex);
        if (!owned) { fprintf(stderr, "REFUSE_FREE_COMPONENT_OWNED_BUFFER\n"); ok = false; continue; }
        if (!checked("FreeBuffer", c->component->FreeBuffer(c->handle, port->definition.nPortIndex, slot->header))) ok = false;
        else slot->header = NULL;
    }
#ifdef QUEST2_NATIVE_OUTPUT
    if(ok && c->native_output && port==&c->output)quest2_capture_finish();
#endif
    if (ok) { free(port->slots); port->slots = NULL; port->count = 0; }
    return ok;
}
static bool reconfigure_output(struct client *c) {
    printf("OUTPUT_RECONFIGURE_BEGIN\n");
    if (!send_command(c, OMX_CommandPortDisable, c->output.definition.nPortIndex)) return false;
    for (;;) {
        pthread_mutex_lock(&c->mutex);
        struct slot *ready = ready_output(c);
        size_t owned = 0;
        for (size_t i = 0; i < c->output.count; ++i) owned += c->output.slots[i].owner == COMPONENT;
        if (!ready && owned && !pause_locked(c)) { pthread_mutex_unlock(&c->mutex); return false; }
        pthread_mutex_unlock(&c->mutex);
        if (ready && !save_output(c, ready->header)) return false;
        if (!ready && !owned) break;
    }
    if (!free_port(c, &c->output) || !wait_command(c) || !definition(c, &c->output) || !crop(c)) return false;
#ifdef QUEST2_NATIVE_OUTPUT
    if(c->native_output && !checked("SetParameter(NativeOutputPort)",c->component->SetParameter(
        c->handle,OMX_IndexParamPortDefinition,&c->output.definition)))return false;
#endif
    if (!send_command(c, OMX_CommandPortEnable, c->output.definition.nPortIndex) ||
        !allocate(c, &c->output) || !wait_command(c) || !queue_outputs(c)) return false;
    puts("OUTPUT_RECONFIGURE_DONE"); return true;
}

#ifndef QUEST2_OMX_SESSION_ONLY
static bool read_aus(const char *path, struct au **rows, size_t *count, uint64_t *total) {
    FILE *f = fopen(path, "r"); if (!f) return false;
    char *line = NULL; size_t capacity = 0; bool ok = true;
    while (getline(&line, &capacity, f) >= 0) {
        if (line[0] == '#' || line[0] == '\n') continue;
        uint64_t bytes; int64_t pts; char extra;
        if (sscanf(line, "%" SCNu64 " %" SCNd64 " %c", &bytes, &pts, &extra) != 2 ||
            !bytes || bytes > SIZE_MAX || UINT64_MAX - *total < bytes || *count == SIZE_MAX / sizeof(**rows)) { ok = false; break; }
        struct au *next = realloc(*rows, (*count + 1) * sizeof(**rows));
        if (!next) { ok = false; break; }
        *rows = next; next[*count] = (struct au){(size_t)bytes, pts}; ++*count; *total += bytes;
    }
    if (ferror(f)) ok = false;
    free(line); fclose(f); return ok && *count;
}
static bool decode(struct client *c, FILE *input, struct au *aus, size_t count) {
    size_t index = 0, consumed = 0; bool sent_eos = false;
    int64_t eos_pts = 0;
    for (size_t i = 0; i < count; ++i) if (aus[i].pts > eos_pts) eos_pts = aus[i].pts;
    if (eos_pts < INT64_MAX) ++eos_pts;
    if (!queue_outputs(c)) return false;
    while (!c->output_eos) {
        pthread_mutex_lock(&c->mutex);
        bool change = c->reconfigure, crop_change = c->crop_changed;
        c->reconfigure = c->crop_changed = false;
        struct slot *out = ready_output(c), *in = NULL;
        if (!sent_eos) for (size_t i = 0; i < c->input.count; ++i)
            if (c->input.slots[i].owner == APP) { in = &c->input.slots[i]; break; }
        bool error = c->error != OMX_ErrorNone;
        if (!error && !change && !crop_change && !out && !in) {
            bool ok = pause_locked(c); pthread_mutex_unlock(&c->mutex);
            if (!ok) return false;
            continue;
        }
        pthread_mutex_unlock(&c->mutex);
        if (error) return false;
        if (out) {
            if (!save_output(c, out->header)) return false;
#ifdef QUEST2_NATIVE_HWC
            struct slot *reusable=c->native_output && out->header->nFilledLen ? c->display_retired : out;
            if (!change && !c->output_eos && reusable && !refill(c,reusable)) return false;
#else
            if (!change && !c->output_eos && !refill(c, out)) return false;
#endif
        }
        if (change && !reconfigure_output(c)) return false;
        if (crop_change && !crop(c)) return false;
        if (c->output_eos) break;
        if (in) {
            OMX_BUFFERHEADERTYPE *b = in->header;
            b->nOffset = b->nFilledLen = b->nFlags = 0;
            if (index < count) {
                size_t chunk = aus[index].bytes - consumed;
                if (chunk > b->nAllocLen) chunk = b->nAllocLen;
                if (!chunk || fread(b->pBuffer, 1, chunk, input) != chunk) return false;
                b->nFilledLen = (OMX_U32)chunk; b->nTimeStamp = aus[index].pts;
                consumed += chunk;
                if (consumed == aus[index].bytes) { b->nFlags = OMX_BUFFERFLAG_ENDOFFRAME; ++index; consumed = 0; }
            } else { b->nFlags = OMX_BUFFERFLAG_EOS; b->nTimeStamp = eos_pts; sent_eos = true; }
            printf("ETB au_completed=%zu bytes=%u pts=%" PRId64 " flags=0x%x\n", index, b->nFilledLen, (int64_t)b->nTimeStamp, b->nFlags);
            pthread_mutex_lock(&c->mutex); in->owner = COMPONENT; pthread_mutex_unlock(&c->mutex);
            OMX_ERRORTYPE result = c->component->EmptyThisBuffer(c->handle, b);
            if (result != OMX_ErrorNone) {
                pthread_mutex_lock(&c->mutex); in->owner = APP; pthread_mutex_unlock(&c->mutex);
                checked("EmptyThisBuffer", result); return false;
            }
        }
    }
    printf("TRANSPORT au_sent=%zu au_total=%zu eos_sent=%d output_eos=%d frames=%zu linear_nv12=%zu\n",
        index, count, sent_eos, c->output_eos, c->frames, c->linear_frames);
    return index == count && sent_eos && c->output_eos && c->frames > 0;
}

#endif
static bool discover(struct client *c, unsigned width, unsigned height, bool native_color, bool venus_nv12) {
    const char *extensions[] = {"OMX.google.android.index.describeColorAspects",
        "OMX.google.android.index.describeHDRStaticInfo", "OMX.google.android.index.describeHDR10PlusInfo"};
    for (unsigned i = 0; i < 3; i++) {
        OMX_INDEXTYPE index = 0;
        if (c->component->GetExtensionIndex &&
            c->component->GetExtensionIndex(c->handle, (OMX_STRING)extensions[i], &index) == OMX_ErrorNone)
            c->metadata_indices[i] = index;
    }
    OMX_PORT_PARAM_TYPE ports; INIT(ports);
    if (!checked("GetParameter(VideoInit)", c->component->GetParameter(c->handle, OMX_IndexParamVideoInit, &ports))) return false;
    bool input = false, output = false;
    if (ports.nPorts > UINT32_MAX - ports.nStartPortNumber) return false;
    for (OMX_U32 i = ports.nStartPortNumber; i < ports.nStartPortNumber + ports.nPorts; ++i) {
        struct port p = {0}; p.definition.nPortIndex = i;
        if (!definition(c, &p)) return false;
        if (p.definition.eDir == OMX_DirInput && !input) { c->input = p; input = true; }
        if (p.definition.eDir == OMX_DirOutput && !output) { c->output = p; output = true; }
    }
    if (!input || !output || c->input.definition.format.video.eCompressionFormat != (c->compression ? c->compression : OMX_VIDEO_CodingAVC)) return false;
    if (width || height) {
        if (!width || !height) return false;
        c->input.definition.format.video.nFrameWidth = width;
        c->input.definition.format.video.nFrameHeight = height;
    }
    if (c->requested_input_buffers) {
        if (c->requested_input_buffers < c->input.definition.nBufferCountMin) {
            fprintf(stderr, "INPUT_BUFFERS_BELOW_MIN requested=%u min=%u\n",
                    c->requested_input_buffers, c->input.definition.nBufferCountMin);
            return false;
        }
        c->input.definition.nBufferCountActual = c->requested_input_buffers;
    }
    /* OEM SetParameter allocates V4L2 slots: combine geometry/count before S_FMT is busy. */
    if ((width || height || c->requested_input_buffers) &&
        !checked("SetParameter(InputPortDefinition)", c->component->SetParameter(c->handle,
            OMX_IndexParamPortDefinition, &c->input.definition))) return false;
#ifdef QUEST2_NATIVE_OUTPUT
    if(c->native_output && !q2_omx_native_enable(c->handle,c->component,
        c->output.definition.nPortIndex,&c->native_usage))return false;
#endif
    if (!native_color) {
        OMX_VIDEO_PARAM_PORTFORMATTYPE format; INIT(format);
        format.nPortIndex = c->output.definition.nPortIndex;
        format.eCompressionFormat = OMX_VIDEO_CodingUnused;
        format.eColorFormat = venus_nv12 ? Q2_COLOR_VENUS_NV12 : OMX_COLOR_FormatYUV420SemiPlanar;
        if (!checked(venus_nv12 ? "SetParameter(OutputVenusNV12)" : "SetParameter(OutputLinearNV12)", c->component->SetParameter(c->handle,
            OMX_IndexParamVideoPortFormat, &format))) return false;
    }
    if (!definition(c, &c->input) || !definition(c, &c->output)) return false;
    if (c->requested_input_buffers &&
        c->input.definition.nBufferCountActual != c->requested_input_buffers) {
        fprintf(stderr, "INPUT_BUFFERS_NOT_ACCEPTED requested=%u actual=%u\n",
                c->requested_input_buffers, c->input.definition.nBufferCountActual);
        return false;
    }
#ifdef QUEST2_NATIVE_OUTPUT
    if(c->native_output && !checked("SetParameter(NativeOutputPort)",c->component->SetParameter(
        c->handle,OMX_IndexParamPortDefinition,&c->output.definition)))return false;
#endif
    return crop(c);
}
static bool cleanup(struct client *c) {
    if (!c->handle) return true;
    pthread_mutex_lock(&c->mutex); c->cleaning = true; bool pending = c->pending; pthread_mutex_unlock(&c->mutex);
    /* A failed pending transition cannot safely be overwritten by another one. */
    if (pending) { fprintf(stderr, "CLEANUP_PENDING_COMMAND: releasing through OMX_FreeHandle only\n"); return false; }
    OMX_STATETYPE state = OMX_StateInvalid;
    if (!checked("GetState(cleanup)", c->component->GetState(c->handle, &state))) return false;
    if (state == OMX_StateExecuting || state == OMX_StatePause) {
        if (!send_command(c, OMX_CommandStateSet, OMX_StateIdle) || !wait_command(c)) return false;
        state = OMX_StateIdle;
    }
    if (state == OMX_StateIdle) {
        if (!send_command(c, OMX_CommandStateSet, OMX_StateLoaded)) return false;
        bool in = free_port(c, &c->input), out = free_port(c, &c->output);
        if (!in || !out || !wait_command(c)) return false;
        state = OMX_StateLoaded;
    }
    if (state == OMX_StateLoaded) return free_port(c, &c->input) && free_port(c, &c->output);
    return false;
}
#ifndef QUEST2_OMX_SESSION_ONLY
static int usage(const char *name) {
    fprintf(stderr, "Usage: %s --library ABS_LIB --input ELEMENTARY_STREAM --aus SIZE_PTS_FILE --output FRESH_DIR [--component NAME] [--width W --height H] [--native-color | --venus-nv12] [--timeout-ms N] [--input-buffers N]\n", name);
    fprintf(stderr, "AU file: decimal bytes and signed microsecond PTS per line, in compressed decode order. Default requests linear NV12. This command initializes hardware and submits frames; it never installs a service or firmware.\n");
#ifdef QUEST2_NATIVE_OUTPUT
    fputs("--native-buffers: original gralloc UseBuffer2 and GPU validation; requires private native-layout bridge.\n",stderr);
#endif
    return 64;
}
int main(int argc, char **argv) {
    const char *library = NULL, *input_path = NULL, *au_path = NULL, *output = NULL;
    const char *name = "OMX.qcom.video.decoder.avc";
    unsigned width = 0, height = 0, timeout = 30000, input_buffers = 0; bool native_color = false, venus_nv12 = false;
#ifdef QUEST2_NATIVE_OUTPUT
    bool native_output=false;
#endif
    setvbuf(stdout, NULL, _IONBF, 0); setvbuf(stderr, NULL, _IONBF, 0);
    for (int i = 1; i < argc; ++i) {
#ifdef QUEST2_NATIVE_OUTPUT
        if(!strcmp(argv[i],"--native-buffers")){native_output=true;venus_nv12=true;continue;}
#endif
        if (!strcmp(argv[i], "--native-color")) { native_color = true; continue; }
        if (!strcmp(argv[i], "--venus-nv12")) { venus_nv12 = true; continue; }
        if (i + 1 >= argc) return usage(argv[0]);
        const char *key = argv[i++], *value = argv[i];
        if (!strcmp(key, "--library")) library = value;
        else if (!strcmp(key, "--input")) input_path = value;
        else if (!strcmp(key, "--aus")) au_path = value;
        else if (!strcmp(key, "--output")) output = value;
        else if (!strcmp(key, "--component")) name = value;
        else if (!strcmp(key, "--width") || !strcmp(key, "--height") || !strcmp(key, "--timeout-ms") || !strcmp(key, "--input-buffers")) {
            char *end; errno = 0; unsigned long n = strtoul(value, &end, 10);
            if (!*value || *end || errno || n > UINT_MAX) return usage(argv[0]);
            if (!strcmp(key, "--width")) width = (unsigned)n;
            else if (!strcmp(key, "--height")) height = (unsigned)n;
            else if (!strcmp(key, "--input-buffers")) {
                if (!n || value[0] == '-') return usage(argv[0]);
                input_buffers = (unsigned)n;
            } else timeout = (unsigned)n;
        } else return usage(argv[0]);
    }
    if ((native_color && venus_nv12) || !library || library[0] != '/' || !input_path || !au_path || !output || strstr(name, ".secure")) return usage(argv[0]);
    struct au *aus = NULL; size_t count = 0; uint64_t total = 0;
    if (!read_aus(au_path, &aus, &count, &total)) { fprintf(stderr, "Invalid AU manifest\n"); free(aus); return 65; }
    FILE *input = fopen(input_path, "rb"); struct stat info;
    if (!input || fstat(fileno(input), &info) || info.st_size < 0 || (uint64_t)info.st_size != total) {
        fprintf(stderr, "Input size does not equal AU manifest\n"); if (input) fclose(input); free(aus); return 65;
    }
    if (mkdir(output, 0700)) { perror("fresh output directory"); fclose(input); free(aus); return 66; }
    struct client c = { .timeout_ms = timeout, .requested_input_buffers = input_buffers, .directory = output, .compression = OMX_VIDEO_CodingAVC };
#ifdef QUEST2_NATIVE_OUTPUT
    c.native_output=native_output;
#endif
    if (!strcmp(name,"OMX.qcom.video.decoder.hevc")) c.compression=OMX_VIDEO_CodingHEVC;
    else if (!strcmp(name,"OMX.qcom.video.decoder.vp8")) c.compression=OMX_VIDEO_CodingVP8;
    else if (!strcmp(name,"OMX.qcom.video.decoder.vp9")) c.compression=OMX_VIDEO_CodingVP9;
    else if (!strcmp(name,"OMX.qcom.video.decoder.mpeg2")) c.compression=OMX_VIDEO_CodingMPEG2;
    pthread_mutex_init(&c.mutex, NULL);
    pthread_condattr_t attr; pthread_condattr_init(&attr); pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&c.changed, &attr); pthread_condattr_destroy(&attr);
    int result = 66;
    bool initialized = false;
    void *core = NULL;
    __typeof__(&OMX_Init) init = NULL;
    __typeof__(&OMX_Deinit) deinit = NULL;
    __typeof__(&OMX_GetHandle) get = NULL;
    __typeof__(&OMX_FreeHandle) release = NULL;
    char *metadata_path = NULL;
    if (asprintf(&metadata_path, "%s/frames.tsv", output) < 0) goto finish;
    c.metadata = fopen(metadata_path, "w"); free(metadata_path); if (!c.metadata) goto finish;
    fputs("frame\tpts_us\tfilled_bytes\tflags\tstride\tslice\twidth\theight\tcolor\tlinear_nv12\n", c.metadata);
    core = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!core) { fprintf(stderr, "DLOPEN_ERROR %s\n", dlerror()); result = 2; goto finish; }
    init = dlsym(core, "OMX_Init"); deinit = dlsym(core, "OMX_Deinit");
    get = dlsym(core, "OMX_GetHandle"); release = dlsym(core, "OMX_FreeHandle");
    if (!init || !deinit || !get || !release) { fprintf(stderr, "Missing core API\n"); result = 3; goto finish; }
    if (!checked("OMX_Init", init())) { result = 4; goto finish; }
    initialized = true;
    OMX_CALLBACKTYPE callbacks = {event, empty_done, fill_done};
    result = 5;
    if (checked("OMX_GetHandle", get(&c.handle, (OMX_STRING)name, &c, &callbacks)) && c.handle) {
        c.component = c.handle;
        if (discover(&c, width, height, native_color, venus_nv12) && send_command(&c, OMX_CommandStateSet, OMX_StateIdle) &&
            allocate(&c, &c.input) && allocate(&c, &c.output) && wait_command(&c) &&
            send_command(&c, OMX_CommandStateSet, OMX_StateExecuting) && wait_command(&c) && decode(&c, input, aus, count)) result = 0;
        bool cleaned = cleanup(&c);
        bool released = checked("OMX_FreeHandle", release(c.handle));
        if (!cleaned || !released) result = 6;
        printf("CLEANUP buffers_and_states=%d handle_released=%d\n", cleaned, released);
        if (!released) { fflush(NULL); _Exit(result); }
#ifdef QUEST2_NATIVE_OUTPUT
        if(c.native_output)quest2_capture_finish();
#ifdef QUEST2_NATIVE_HWC
        quest2_hwc_video_finish();
#endif
#endif
    }
finish:
    if (initialized && !checked("OMX_Deinit", deinit())) result = 7;
    if (core) dlclose(core);
    fclose(input); free(aus); if (c.metadata) fclose(c.metadata);
    free(c.input.slots); free(c.output.slots);
    pthread_cond_destroy(&c.changed); pthread_mutex_destroy(&c.mutex);
    printf("RESULT exit=%d frames=%zu linear_nv12=%zu output_eos=%d service_replaced=0 firmware_replaced=0\n", result, c.frames, c.linear_frames, c.output_eos);
    return result;
}

#endif
