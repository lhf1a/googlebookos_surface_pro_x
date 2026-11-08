#pragma once
#include "sq2-v4l2-session.h"
#include "quest2-omx-session.h"
// Keep the existing V4L2 codecs until each OEM codec has its own validation.
class Sq2OmxSession {
    Sq2V4L2Session native_;
    quest2_omx_session *omx_ = nullptr;
    Sq2V4L2Session::Deliver deliver_;
    bool failed_ = false;
    static int deliver(void *opaque, const quest2_omx_frame *f) {
        auto *self = static_cast<Sq2OmxSession *>(opaque);
        if (f->bytes > UINT32_MAX) return -EOVERFLOW;
        Sq2Geometry g{f->width, f->height, f->stride, f->slice, uint32_t(f->bytes), 1};
        self->deliver_({{-1, f->bytes, {}}, g, f->token, uint32_t(f->bytes), 0, 0, f->data});
        return 0;
    }
public:
    using State = Sq2V4L2Session::State;
    using Allocate = Sq2V4L2Session::Allocate;
    using Deliver = Sq2V4L2Session::Deliver;
    ~Sq2OmxSession() { close(); }
    State state() const {
        if (!omx_) return native_.state();
        return failed_ ? State::Failed : quest2_omx_ended(omx_) ? State::Ended : State::Running;
    }
    int open(const char *device, uint32_t codec, uint32_t width, uint32_t height,
             Allocate allocate, Deliver sink, bool reuse = false, uint32_t format = V4L2_PIX_FMT_NV12) {
        if (codec != V4L2_PIX_FMT_H264)
            return native_.open(device, codec, width, height, std::move(allocate), std::move(sink), reuse, format);
        if (omx_ || format != V4L2_PIX_FMT_NV12) return -EINVAL;
        const char *library = getenv("SQ2_OMX_CORE");
        if (!library || !*library) return -ENOENT;
        deliver_ = std::move(sink); failed_ = false;
        __android_log_print(ANDROID_LOG_INFO, "SQ2C2Lifecycle", "BACKEND OMX.qcom.video.decoder.avc core=%s", library);
        return quest2_omx_open(&omx_, library, width, height, deliver, this);
    }
    int submit(const uint8_t *p, size_t n, uint64_t token) {
        return omx_ ? quest2_omx_submit(omx_, p, n, token) : native_.submit(p, n, token);
    }
    int requestDrain() { return omx_ ? quest2_omx_drain(omx_) : native_.requestDrain(); }
    int pump(int ms) {
        if (!omx_) return native_.pump(ms);
        int r = quest2_omx_pump(omx_, ms); if (r) failed_ = true; return r;
    }
    int close() {
        if (!omx_) return native_.close();
        int r = quest2_omx_close(omx_);
        if (r == -EBUSY) return r;
        omx_ = nullptr; deliver_ = {}; failed_ = false; return r;
    }
};
