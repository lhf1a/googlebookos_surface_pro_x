#define QUEST2_OMX_SESSION_ONLY
#include "quest2-omx-decode.c"
struct quest2_omx_session {
    struct client client;
    void *core;
    __typeof__(&OMX_Deinit) deinit;
    __typeof__(&OMX_FreeHandle) release;
    bool initialized, draining, eos_sent;
};
int quest2_omx_close(struct quest2_omx_session *s) {
    if (!s) return 0;
    struct client *c = &s->client;
    bool ok = cleanup(c);
    if (c->handle && (!s->release || !checked("OMX_FreeHandle", s->release(c->handle)))) {
        /* A live vendor callback may still refer to this session. Never free it. */
        return -EBUSY;
    }
    if (s->initialized && !checked("OMX_Deinit", s->deinit())) ok = false;
    if (s->core) dlclose(s->core);
    free(c->input.slots); free(c->output.slots);
    pthread_cond_destroy(&c->changed); pthread_mutex_destroy(&c->mutex);
    free(s);
    return ok ? 0 : -EIO;
}
int quest2_omx_open(struct quest2_omx_session **out, const char *library,
                    unsigned width, unsigned height, quest2_omx_deliver deliver, void *opaque) {
    if (!out || !library || library[0] != '/' || !deliver || !width || !height ||
        width > 4096 || height > 4096 || (width & 1) || (height & 1)) return -EINVAL;
    *out = NULL;
    struct quest2_omx_session *s = calloc(1, sizeof(*s));
    if (!s) return -ENOMEM;
    struct client *c = &s->client;
    c->timeout_ms = 5000; c->deliver = deliver; c->opaque = opaque;
    pthread_mutex_init(&c->mutex, NULL);
    pthread_condattr_t attr; pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&c->changed, &attr); pthread_condattr_destroy(&attr);
    s->core = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!s->core) { fprintf(stderr, "OMX_CORE_LOAD %s\n", dlerror()); goto fail; }
    __typeof__(&OMX_Init) init = dlsym(s->core, "OMX_Init");
    __typeof__(&OMX_GetHandle) get = dlsym(s->core, "OMX_GetHandle");
    s->deinit = dlsym(s->core, "OMX_Deinit"); s->release = dlsym(s->core, "OMX_FreeHandle");
    if (!init || !get || !s->deinit || !s->release) goto fail;
    if (!checked("OMX_Init", init())) goto fail;
    s->initialized = true;
    OMX_CALLBACKTYPE callbacks = {event, empty_done, fill_done};
    if (!checked("OMX_GetHandle", get(&c->handle, "OMX.qcom.video.decoder.avc", c, &callbacks)) || !c->handle) goto fail;
    c->component = c->handle;
    if (!discover(c, width, height, false, true) ||
        !send_command(c, OMX_CommandStateSet, OMX_StateIdle) ||
        !allocate(c, &c->input) || !allocate(c, &c->output) || !wait_command(c) ||
        !send_command(c, OMX_CommandStateSet, OMX_StateExecuting) || !wait_command(c) || !queue_outputs(c)) goto fail;
    *out = s;
    return 0;
fail:
    quest2_omx_close(s);
    return -EIO;
}
static int submit_buffer(struct quest2_omx_session *s, const uint8_t *data, size_t size, uint64_t token, bool eos) {
    struct client *c = &s->client;
    if (c->output_eos || token > INT64_MAX || (!eos && (!data || !size))) return -EINVAL;
    pthread_mutex_lock(&c->mutex);
    struct slot *in = NULL;
    if (c->error == OMX_ErrorNone) for (size_t i = 0; i < c->input.count; ++i)
        if (c->input.slots[i].owner == APP) { in = &c->input.slots[i]; break; }
    bool error = c->error != OMX_ErrorNone;
    if (in && size <= in->header->nAllocLen) in->owner = COMPONENT;
    pthread_mutex_unlock(&c->mutex);
    if (error) return -EIO;
    if (!in) return -EAGAIN;
    OMX_BUFFERHEADERTYPE *b = in->header;
    if (size > b->nAllocLen) return -EMSGSIZE;
    if (size) memcpy(b->pBuffer, data, size);
    b->nOffset = 0; b->nFilledLen = size; b->nTimeStamp = token;
    b->nFlags = eos ? OMX_BUFFERFLAG_EOS : OMX_BUFFERFLAG_ENDOFFRAME;
    OMX_ERRORTYPE result = c->component->EmptyThisBuffer(c->handle, b);
    if (result != OMX_ErrorNone) {
        pthread_mutex_lock(&c->mutex); in->owner = APP; pthread_mutex_unlock(&c->mutex);
        checked("EmptyThisBuffer", result); return -EIO;
    }
    return 0;
}
int quest2_omx_submit(struct quest2_omx_session *s, const uint8_t *data, size_t size, uint64_t token) {
    if (!s || s->draining) return -EPIPE;
    return submit_buffer(s, data, size, token, false);
}
int quest2_omx_drain(struct quest2_omx_session *s) {
    if (!s || s->draining) return -EPIPE;
    s->draining = true; return 0;
}
int quest2_omx_ended(const struct quest2_omx_session *s) { return s && s->client.output_eos; }
int quest2_omx_pump(struct quest2_omx_session *s, unsigned timeout_ms) {
    if (!s || timeout_ms > 1000) return -EINVAL;
    struct client *c = &s->client;
    for (unsigned n = 0; n < 64; ++n) {
        pthread_mutex_lock(&c->mutex);
        bool change = c->reconfigure, crop_change = c->crop_changed;
        c->reconfigure = c->crop_changed = false;
        struct slot *out = ready_output(c);
        bool error = c->error != OMX_ErrorNone;
        if (!n && !error && !change && !crop_change && !out && timeout_ms) {
            struct timespec until; clock_gettime(CLOCK_MONOTONIC, &until);
            until.tv_nsec += (long)timeout_ms * 1000000;
            until.tv_sec += until.tv_nsec / 1000000000; until.tv_nsec %= 1000000000;
            int r = pthread_cond_timedwait(&c->changed, &c->mutex, &until);
            pthread_mutex_unlock(&c->mutex);
            if (r && r != ETIMEDOUT) return -r;
            continue;
        }
        pthread_mutex_unlock(&c->mutex);
        if (error) return -EIO;
        if (out && (!save_output(c, out->header) ||
            (!change && !c->output_eos && !refill(c, out)))) return -EIO;
        if (change && !reconfigure_output(c)) return -EIO;
        if (crop_change && !crop(c)) return -EIO;
        if (!change && !crop_change && !out) break;
    }
    if (s->draining && !s->eos_sent) {
        int r = submit_buffer(s, NULL, 0, 0, true);
        if (r && r != -EAGAIN) return r;
        if (!r) s->eos_sent = true;
    }
    return 0;
}
