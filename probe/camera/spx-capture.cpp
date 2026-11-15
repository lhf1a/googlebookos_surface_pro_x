// SPDX-License-Identifier: GPL-2.0-only
#include "spx-capture.h"
#include <cerrno>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <glob.h>
#include <linux/media.h>
#include <linux/media-bus-format.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace spx {
namespace {
int call(int fd, unsigned long op, void* arg) {
    int r;
    do { r = ioctl(fd, op, arg); } while (r < 0 && errno == EINTR);
    return r;
}
void check(int r, const char* operation) {
    if (r < 0) throw std::runtime_error(std::string(operation) + ": " + strerror(errno));
}
struct Fd {
    int v = -1;
    ~Fd() { if (v >= 0) close(v); }
    void openPath(const std::string& path) {
        v = open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        check(v, path.c_str());
    }
};
std::string device(const media_entity_desc& e) {
    std::ifstream in("/sys/dev/char/" + std::to_string(e.dev.major) + ":" +
                     std::to_string(e.dev.minor) + "/uevent");
    std::string line;
    while (std::getline(in, line))
        if (line.rfind("DEVNAME=", 0) == 0) return "/dev/" + line.substr(8);
    throw std::runtime_error("media entity has no character device");
}
struct Entity {
    media_entity_desc d{};
    std::vector<media_link_desc> links;
};
std::vector<Entity> entities(int fd) {
    std::vector<Entity> result;
    media_entity_desc e{};
    e.id = MEDIA_ENT_ID_FLAG_NEXT;
    while (call(fd, MEDIA_IOC_ENUM_ENTITIES, &e) == 0) {
        Entity item; item.d = e; item.links.resize(e.links);
        std::vector<media_pad_desc> pads(e.pads);
        media_links_enum ls{};
        ls.entity = e.id; ls.pads = pads.data(); ls.links = item.links.data();
        check(call(fd, MEDIA_IOC_ENUM_LINKS, &ls), "enumerate links");
        result.push_back(item);
        e.id |= MEDIA_ENT_ID_FLAG_NEXT;
    }
    if (errno != EINVAL) check(-1, "enumerate entities");
    return result;
}
const Entity& named(const std::vector<Entity>& es, const char* name, bool prefix = false) {
    for (const auto& e : es)
        if (prefix ? std::string(e.d.name).rfind(name, 0) == 0 : !strcmp(name, e.d.name)) return e;
    throw std::runtime_error(std::string("missing camera entity: ") + name);
}
const Entity& byId(const std::vector<Entity>& es, unsigned id) {
    for (const auto& e : es) if (e.d.id == id) return e;
    throw std::runtime_error("missing linked camera entity");
}
void link(int fd, unsigned src, unsigned sp, unsigned dst, unsigned dp, bool enable) {
    media_link_desc l{};
    l.source.entity = src; l.source.index = sp; l.source.flags = MEDIA_PAD_FL_SOURCE;
    l.sink.entity = dst; l.sink.index = dp; l.sink.flags = MEDIA_PAD_FL_SINK;
    l.flags = enable ? MEDIA_LNK_FL_ENABLED : 0;
    check(call(fd, MEDIA_IOC_SETUP_LINK, &l), "configure media link");
}
v4l2_mbus_framefmt format(const Entity& e, unsigned pad, unsigned code, unsigned w, unsigned h) {
    Fd fd; fd.openPath(device(e.d));
    v4l2_subdev_format f{};
    f.which = V4L2_SUBDEV_FORMAT_ACTIVE; f.pad = pad;
    f.format.code = code; f.format.width = w; f.format.height = h;
    f.format.field = V4L2_FIELD_NONE;
    check(call(fd.v, VIDIOC_SUBDEV_S_FMT, &f), "configure subdevice format");
    return f.format;
}
}
struct Capture::State {
    Fd media, video, sensor;
    unsigned aeFrames = 0;
    void* map[4]{};
    size_t length[4]{};
    unsigned count = 0, width = 0, height = 0, stride = 0;
    bool streaming = false, front = false;
    ~State() {
        auto type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (streaming) call(video.v, VIDIOC_STREAMOFF, &type);
        for (unsigned i = 0; i < 4; ++i) if (map[i]) munmap(map[i], length[i]);
        if (video.v >= 0) {
            v4l2_requestbuffers req{};
            req.type = type; req.memory = V4L2_MEMORY_MMAP;
            call(video.v, VIDIOC_REQBUFS, &req);
        }
    }
};
Capture::Capture(bool front, unsigned width, unsigned height, unsigned maxFps) : s(new State) {
    s->front = front;
    glob_t paths{};
    int gr = glob("/dev/media*", 0, nullptr, &paths);
    if (gr) { globfree(&paths); throw std::runtime_error("no media devices"); }
    for (size_t i = 0; i < paths.gl_pathc; ++i) {
        int fd = open(paths.gl_pathv[i], O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        media_device_info info{};
        if (call(fd, MEDIA_IOC_DEVICE_INFO, &info) == 0 && !strcmp(info.driver, "qcom-camss")) {
            s->media.v = fd; break;
        }
        close(fd);
    }
    globfree(&paths);
    if (s->media.v < 0) throw std::runtime_error("CAMSS not registered");
    check(flock(s->media.v, LOCK_EX | LOCK_NB), "camera pipeline busy");
    auto es = entities(s->media.v);
    const auto& sensor = named(es, front ? "ov5693 " : "ov13858 ", true);
    unsigned phyId = 0;
    for (const auto& l : sensor.links)
        if (l.source.entity == sensor.d.id && (l.flags & MEDIA_LNK_FL_IMMUTABLE))
            phyId = l.sink.entity;
    s->sensor.openPath(device(sensor.d));
    const auto& phy = byId(es, phyId);
    const auto& csid = named(es, "msm_csid0");
    const auto& vfe = named(es, "msm_vfe0_rdi0");
    const auto& video = named(es, "msm_vfe0_video0");
    // Disconnect the preceding camera before selecting this camera.
    for (const auto& e : es) for (const auto& l : e.links)
        if (l.sink.entity == csid.d.id && (l.flags & MEDIA_LNK_FL_ENABLED) &&
            !(l.flags & MEDIA_LNK_FL_IMMUTABLE))
            link(s->media.v, l.source.entity, l.source.index, l.sink.entity, l.sink.index, false);
    link(s->media.v, phy.d.id, 1, csid.d.id, 0, true);
    link(s->media.v, csid.d.id, 1, vfe.d.id, 0, true);
    unsigned code = front ? MEDIA_BUS_FMT_SBGGR10_1X10 : MEDIA_BUS_FMT_SGRBG10_1X10;
    auto actual = format(sensor, 0, code, width, height);
    if (actual.code != code) throw std::runtime_error("sensor changed Bayer format");
    width = actual.width; height = actual.height;
    if(maxFps) {
        v4l2_ext_control rate{};rate.id=V4L2_CID_PIXEL_RATE;
        v4l2_ext_controls controls{};controls.count=1;controls.controls=&rate;
        check(call(s->sensor.v,VIDIOC_G_EXT_CTRLS,&controls),"read pixel rate");
        v4l2_control blank{};blank.id=V4L2_CID_HBLANK;
        check(call(s->sensor.v,VIDIOC_G_CTRL,&blank),"read horizontal blanking");
        v4l2_queryctrl limits{};limits.id=V4L2_CID_VBLANK;
        check(call(s->sensor.v,VIDIOC_QUERYCTRL,&limits),"query vertical blanking");
        if(rate.value64<=0 || blank.value<0)throw std::runtime_error("invalid sensor timing");
        uint64_t divisor=(uint64_t(width)+blank.value)*maxFps;
        int64_t lines=(uint64_t(rate.value64)+divisor-1)/divisor;
        blank.id=V4L2_CID_VBLANK;
        blank.value=std::clamp<int64_t>(lines-height,limits.minimum,limits.maximum);
        // Match the advertised video ceiling rather than feeding the encoder a 60fps binned mode.
        check(call(s->sensor.v,VIDIOC_S_CTRL,&blank),"set sensor frame rate");
    }
    for (const auto* e : {&phy, &csid, &vfe}) {
        auto in = format(*e, 0, code, width, height);
        auto out = format(*e, 1, code, width, height);
        if (in.width != width || in.height != height || in.code != code ||
            out.width != width || out.height != height || out.code != code)
            throw std::runtime_error("receiver format mismatch");
    }
    s->video.openPath(device(video.d));
    v4l2_format f{};
    f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    f.fmt.pix_mp.width = width; f.fmt.pix_mp.height = height;
    f.fmt.pix_mp.pixelformat = front ? V4L2_PIX_FMT_SBGGR10P : V4L2_PIX_FMT_SGRBG10P;
    f.fmt.pix_mp.field = V4L2_FIELD_NONE;
    check(call(s->video.v, VIDIOC_S_FMT, &f), "capture format");
    if (f.fmt.pix_mp.num_planes != 1 || f.fmt.pix_mp.width != width ||
        f.fmt.pix_mp.height != height || width % 4 ||
        f.fmt.pix_mp.pixelformat != (front ? V4L2_PIX_FMT_SBGGR10P : V4L2_PIX_FMT_SGRBG10P))
        throw std::runtime_error("unsupported capture layout");
    s->width = width; s->height = height; s->stride = f.fmt.pix_mp.plane_fmt[0].bytesperline;
    if (s->stride < width / 4 * 5) throw std::runtime_error("short RAW10 stride");
    v4l2_requestbuffers req{};
    req.count = 4; req.type = f.type; req.memory = V4L2_MEMORY_MMAP;
    check(call(s->video.v, VIDIOC_REQBUFS, &req), "request capture buffers");
    if (req.count < 2 || req.count > 4) throw std::runtime_error("unsupported buffer count");
    s->count = req.count;
    for (unsigned i = 0; i < s->count; ++i) {
        v4l2_plane plane{};
        v4l2_buffer b{};
        b.type = f.type; b.memory = V4L2_MEMORY_MMAP; b.index = i;
        b.length = 1; b.m.planes = &plane;
        check(call(s->video.v, VIDIOC_QUERYBUF, &b), "query capture buffer");
        if (plane.length < size_t(s->stride) * height) throw std::runtime_error("short DMA buffer");
        void* p = mmap(nullptr, plane.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                       s->video.v, plane.m.mem_offset);
        if (p == MAP_FAILED) check(-1, "map capture buffer");
        s->map[i] = p; s->length[i] = plane.length;
        check(call(s->video.v, VIDIOC_QBUF, &b), "queue capture buffer");
    }
    auto type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    check(call(s->video.v, VIDIOC_STREAMON, &type), "start camera");
    s->streaming = true;
}
Capture::~Capture() = default;
Frame Capture::next(int timeoutMs) {
    pollfd p{s->video.v, POLLIN, 0};
    int r = poll(&p, 1, timeoutMs);
    if (!r) throw std::runtime_error("camera frame timeout");
    check(r, "wait camera frame");
    v4l2_plane plane{};
    v4l2_buffer b{};
    b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; b.memory = V4L2_MEMORY_MMAP;
    b.length = 1; b.m.planes = &plane;
    check(call(s->video.v, VIDIOC_DQBUF, &b), "dequeue camera frame");
    if (b.index >= s->count || plane.data_offset > plane.bytesused ||
        plane.bytesused > s->length[b.index] ||
        plane.bytesused - plane.data_offset < size_t(s->stride) * s->height ||
        (b.flags & V4L2_BUF_FLAG_ERROR))
        throw std::runtime_error("invalid camera frame");
    Frame f{s->width, s->height, s->stride, b.sequence,
            uint64_t(b.timestamp.tv_sec) * 1000000000 + uint64_t(b.timestamp.tv_usec) * 1000,
            s->front, {}};
    auto* start = static_cast<uint8_t*>(s->map[b.index]) + plane.data_offset;
    f.raw10.assign(start, start + size_t(s->stride) * s->height);
    check(call(s->video.v, VIDIOC_QBUF, &b), "return camera buffer");
    return f;
}
void Capture::autoExposure(float mean) {
    // Allow the sensor's queued exposure update to reach the image before adjusting again.
    if (++s->aeFrames % 4 || !std::isfinite(mean) || mean < 0) return;
    float ratio = std::clamp(.18f / std::max(mean,.001f), .65f, 1.5f);
    if (ratio > .9f && ratio < 1.1f) return;
    auto get = [&](unsigned id, v4l2_queryctrl& q, v4l2_control& c) {
        q.id = id; c.id = id;
        check(call(s->sensor.v, VIDIOC_QUERYCTRL, &q), "query sensor control");
        check(call(s->sensor.v, VIDIOC_G_CTRL, &c), "read sensor control");
    };
    v4l2_queryctrl eq{}, gq{}; v4l2_control e{}, g{};
    get(V4L2_CID_EXPOSURE,eq,e); get(V4L2_CID_ANALOGUE_GAIN,gq,g);
    // Native unity gains are 1 (OV5693) and 128 (OV13858).
    int unity = s->front ? 1 : 128;
    int newExposure = std::clamp(int(std::lround(e.value * ratio)),eq.minimum,eq.maximum);
    if ((ratio > 1 && e.value == eq.maximum) || (ratio < 1 && g.value > unity)) {
        g.value = std::clamp(int(std::lround(g.value * ratio)),std::max(gq.minimum,unity),
                             std::min(gq.maximum,unity*8));
        check(call(s->sensor.v,VIDIOC_S_CTRL,&g),"set analogue gain");
    } else {
        e.value = newExposure;
        check(call(s->sensor.v,VIDIOC_S_CTRL,&e),"set exposure");
    }
}

}
