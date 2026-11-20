// SPDX-License-Identifier: GPL-2.0-only
#include "spx-capture.h"
#include <algorithm>
#include <array>
#include <cerrno>
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
struct FlockGuard {
    int fd = -1;
    explicit FlockGuard(int f) : fd(f) {
        if (fd >= 0) {
            int r;
            do { r = flock(fd, LOCK_EX); } while (r < 0 && errno == EINTR);
            check(r, "lock media graph");
        }
    }
    ~FlockGuard() {
        if (fd >= 0) flock(fd, LOCK_UN);
    }
    FlockGuard(const FlockGuard&) = delete;
    FlockGuard& operator=(const FlockGuard&) = delete;
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
bool tryFormat(const Entity& e, unsigned pad, unsigned code, unsigned w, unsigned h,
               v4l2_mbus_framefmt& out) {
    Fd fd; fd.openPath(device(e.d));
    v4l2_subdev_format f{};
    f.which = V4L2_SUBDEV_FORMAT_ACTIVE; f.pad = pad;
    f.format.code = code; f.format.width = w; f.format.height = h;
    f.format.field = V4L2_FIELD_NONE;
    if (call(fd.v, VIDIOC_SUBDEV_S_FMT, &f) < 0) return false;
    out = f.format;
    return true;
}
const char* sensorPrefix(Sensor sensor) {
    switch (sensor) {
        case Sensor::Front: return "ov5693 ";
        case Sensor::Rear: return "ov13858 ";
        case Sensor::FrontIr: return "ov7251 ";
    }
    throw std::runtime_error("unknown camera sensor");
}
bool hasLink(const MediaNodeSummary& src, unsigned srcPad, unsigned dstId) {
    for (const auto& [pad, sink] : src.links)
        if (pad == srcPad && sink == dstId) return true;
    return false;
}
const MediaNodeSummary* findNodeById(const std::vector<MediaNodeSummary>& nodes, unsigned id) {
    for (const auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}
const MediaNodeSummary* findNodeByName(const std::vector<MediaNodeSummary>& nodes, const char* name) {
    for (const auto& n : nodes) if (n.name == name) return &n;
    return nullptr;
}
int quantizeStep(int64_t target, int current, int lo, int hi, int step, bool up) {
    int64_t clamped = std::clamp<int64_t>(target, lo, hi);
    int64_t offset = clamped - lo;
    int64_t snapped = lo + ((offset + step / 2) / step) * step;
    if (snapped > hi) snapped = lo + (offset / step) * step;
    if (snapped < lo) snapped = lo;
    if (up && snapped <= current && current < hi) {
        int64_t next = int64_t(current) + step;
        snapped = std::min<int64_t>(hi, next);
    } else if (!up && snapped >= current && current > lo) {
        int64_t prev = int64_t(current) - step;
        snapped = std::max<int64_t>(lo, prev);
    }
    return int(snapped);
}
bool queryControlOptional(int fd, unsigned id, ControlRange& out) {
    v4l2_queryctrl q{};
    v4l2_control c{};
    q.id = id;
    c.id = id;
    if (call(fd, VIDIOC_QUERYCTRL, &q) < 0) {
        if (errno == EINVAL || errno == ENOTTY || errno == ENOENT) return false;
        check(-1, "query sensor control");
    }
    if (q.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY)) return false;
    check(call(fd, VIDIOC_G_CTRL, &c), "read sensor control");
    out = ControlRange{q.minimum, q.maximum, q.step, c.value};
    return true;
}
}

PipelineSelection selectPipeline(Sensor sensor,
                                 unsigned phyId,
                                 const std::vector<MediaNodeSummary>& nodes) {
    const auto* phy = findNodeById(nodes, phyId);
    if (!phy) throw std::runtime_error("missing CSIPHY entity");

    struct Candidate { const char* csid; const char* vfe; const char* video; };
    static constexpr std::array<Candidate, 1> kFrontRoutes{{
        {"msm_csid0", "msm_vfe0_rdi0", "msm_vfe0_video0"},
    }};
    static constexpr std::array<Candidate, 2> kRearRoutes{{
        {"msm_csid1", "msm_vfe1_rdi0", "msm_vfe1_video0"},
        {"msm_csid0", "msm_vfe0_rdi0", "msm_vfe0_video0"},
    }};
    static constexpr std::array<Candidate, 4> kIrRoutes{{
        {"msm_csid3", "msm_vfe3_rdi0", "msm_vfe3_video0"},
        {"msm_csid2", "msm_vfe2_rdi0", "msm_vfe2_video0"},
        {"msm_csid1", "msm_vfe1_rdi0", "msm_vfe1_video0"},
        {"msm_csid0", "msm_vfe0_rdi0", "msm_vfe0_video0"},
    }};

    const Candidate* begin = nullptr;
    const Candidate* end = nullptr;
    switch (sensor) {
        case Sensor::Front:
            begin = kFrontRoutes.data(); end = begin + kFrontRoutes.size(); break;
        case Sensor::Rear:
            begin = kRearRoutes.data(); end = begin + kRearRoutes.size(); break;
        case Sensor::FrontIr:
            begin = kIrRoutes.data(); end = begin + kIrRoutes.size(); break;
    }

    for (const Candidate* it = begin; it != end; ++it) {
        const auto* csid = findNodeByName(nodes, it->csid);
        const auto* vfe = findNodeByName(nodes, it->vfe);
        const auto* video = findNodeByName(nodes, it->video);
        if (!csid || !vfe || !video) continue;
        if (!hasLink(*phy, 1, csid->id)) continue;
        if (!hasLink(*csid, 1, vfe->id)) continue;
        if (!hasLink(*vfe, 1, video->id)) continue;
        return PipelineSelection{csid->id, vfe->id, video->id,
                                 csid->name, vfe->name, video->name};
    }
    throw std::runtime_error("no usable CAMSS pipeline for camera sensor");
}

AutoExposureDecision computeAutoExposure(Sensor sensor,
                                         float linearMean,
                                         const ControlRange& exposure,
                                         const ControlRange& gain) {
    if (!std::isfinite(linearMean) || linearMean < 0.f) return {};
    if (exposure.minimum > exposure.maximum || gain.minimum > gain.maximum) return {};

    // Native 1x analogue gain floors: OV5693=16 (Q4), OV13858=128 (Q7), OV7251=16 (Q4).
    const int nominalUnity = (sensor == Sensor::Rear) ? 128 : 16;
    const int minGain = std::clamp(std::max(gain.minimum, nominalUnity),
                                   gain.minimum, gain.maximum);
    const int maxGain = (minGain > 0)
        ? int(std::min<int64_t>(gain.maximum, int64_t(minGain) * 8))
        : gain.maximum;
    const int minExp = (exposure.maximum >= 1) ? std::max(exposure.minimum, 1) : exposure.minimum;
    const int maxExp = exposure.maximum;
    const int expStep = std::max(1, exposure.step);
    const int gainStep = std::max(1, gain.step);

    // Guard against drivers initializing analogue gain below unity (e.g. OV13858 ANA_GAIN_MIN=0)
    // or exposure below 1 line, which would otherwise stall multiplicative scaling at 0.
    if (gain.value < minGain)
        return {AutoExposureDecision::Action::SetGain, minGain};
    if (exposure.value < minExp)
        return {AutoExposureDecision::Action::SetExposure, minExp};

    const float ratio = std::clamp(.18f / std::max(linearMean, .001f), .65f, 1.5f);
    if (ratio > .9f && ratio < 1.1f) return {};

    const bool adjustGain = (ratio > 1.f && exposure.value >= maxExp) ||
                            (ratio < 1.f && gain.value > minGain);
    if (adjustGain) {
        const int64_t target = std::llround(double(gain.value) * double(ratio));
        const int next = quantizeStep(target, gain.value, minGain, maxGain, gainStep, ratio > 1.f);
        if (next == gain.value) return {};
        return {AutoExposureDecision::Action::SetGain, next};
    }

    const int64_t target = std::llround(double(exposure.value) * double(ratio));
    const int next = quantizeStep(target, exposure.value, minExp, maxExp, expStep, ratio > 1.f);
    if (next == exposure.value) return {};
    return {AutoExposureDecision::Action::SetExposure, next};
}

struct Capture::State {
    Fd media, video, sensor;
    unsigned aeFrames = 0;
    void* map[4]{};
    size_t length[4]{};
    unsigned count = 0, width = 0, height = 0, stride = 0;
    uint32_t pixelFormat = 0;
    Sensor sensorKind = Sensor::Rear;
    bool streaming = false;
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

Capture::Capture(bool front, unsigned width, unsigned height, unsigned maxFps)
    : Capture(front ? Sensor::Front : Sensor::Rear, width, height, maxFps) {}

Capture::Capture(Sensor sensorKind, unsigned width, unsigned height, unsigned maxFps) : s(new State) {
    s->sensorKind = sensorKind;
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

    // Serialize media-graph inspection and link changes, while holding a persistent
    // exclusive lock on the chosen VFE video node so independent multi-IFE pipelines
    // do not block each other and single-IFE contention fails before touching links.
    FlockGuard graphLock(s->media.v);
    auto es = entities(s->media.v);
    const auto& sensor = named(es, sensorPrefix(sensorKind), true);
    unsigned phyId = 0;
    for (const auto& l : sensor.links)
        if (l.source.entity == sensor.d.id && (l.flags & MEDIA_LNK_FL_IMMUTABLE))
            phyId = l.sink.entity;
    if (!phyId) throw std::runtime_error("sensor has no CSIPHY link");

    std::vector<MediaNodeSummary> summaries;
    summaries.reserve(es.size());
    for (const auto& e : es) {
        MediaNodeSummary node;
        node.id = e.d.id;
        node.name = e.d.name;
        for (const auto& l : e.links)
            if (l.source.entity == e.d.id)
                node.links.emplace_back(l.source.index, l.sink.entity);
        summaries.push_back(std::move(node));
    }

    auto route = selectPipeline(sensorKind, phyId, summaries);
    const auto& phy = byId(es, phyId);
    const auto& csid = byId(es, route.csidId);
    const auto& vfe = byId(es, route.vfeId);
    const auto& video = byId(es, route.videoId);

    s->video.openPath(device(video.d));
    check(flock(s->video.v, LOCK_EX | LOCK_NB), "camera pipeline busy");
    s->sensor.openPath(device(sensor.d));

    // Disconnect conflicting dynamic links touching this CSIPHY/CSID/VFE before selecting our route.
    for (const auto& e : es) {
        for (const auto& l : e.links) {
            if (!(l.flags & MEDIA_LNK_FL_ENABLED) || (l.flags & MEDIA_LNK_FL_IMMUTABLE)) continue;
            bool conflict =
                (l.sink.entity == csid.d.id && l.source.entity != phy.d.id) ||
                (l.source.entity == phy.d.id && l.sink.entity != csid.d.id) ||
                (l.sink.entity == vfe.d.id && l.source.entity != csid.d.id) ||
                (l.source.entity == csid.d.id && l.sink.entity != vfe.d.id);
            if (conflict)
                link(s->media.v, l.source.entity, l.source.index, l.sink.entity, l.sink.index, false);
        }
    }
    link(s->media.v, phy.d.id, 1, csid.d.id, 0, true);
    link(s->media.v, csid.d.id, 1, vfe.d.id, 0, true);

    const unsigned sensorCode = (sensorKind == Sensor::Front)   ? MEDIA_BUS_FMT_SBGGR10_1X10
                              : (sensorKind == Sensor::Rear)    ? MEDIA_BUS_FMT_SGRBG10_1X10
                                                                : MEDIA_BUS_FMT_Y10_1X10;
    auto actual = format(sensor, 0, sensorCode, width, height);
    if (actual.code != sensorCode) throw std::runtime_error("sensor changed pixel format");
    width = actual.width; height = actual.height;

    if (maxFps) {
        v4l2_ext_control rate{}; rate.id = V4L2_CID_PIXEL_RATE;
        v4l2_ext_controls controls{}; controls.count = 1; controls.controls = &rate;
        v4l2_control hblank{}; hblank.id = V4L2_CID_HBLANK;
        v4l2_queryctrl limits{}; limits.id = V4L2_CID_VBLANK;
        bool hasTiming =
            call(s->sensor.v, VIDIOC_G_EXT_CTRLS, &controls) == 0 &&
            call(s->sensor.v, VIDIOC_G_CTRL, &hblank) == 0 &&
            call(s->sensor.v, VIDIOC_QUERYCTRL, &limits) == 0;
        if (!hasTiming && sensorKind != Sensor::FrontIr)
            throw std::runtime_error("read sensor timing controls failed");
        if (hasTiming) {
            if (rate.value64 <= 0 || hblank.value < 0 || limits.minimum > limits.maximum)
                throw std::runtime_error("invalid sensor timing");
            uint64_t divisor = (uint64_t(width) + uint64_t(hblank.value)) * maxFps;
            if (!divisor) throw std::runtime_error("invalid sensor timing");
            int64_t lines = int64_t((uint64_t(rate.value64) + divisor - 1) / divisor);
            v4l2_control vblank{};
            vblank.id = V4L2_CID_VBLANK;
            vblank.value = int(std::clamp<int64_t>(lines - int64_t(height),
                                                   limits.minimum, limits.maximum));
            // Match the advertised video ceiling rather than feeding the encoder a 60fps binned mode.
            check(call(s->sensor.v, VIDIOC_S_CTRL, &vblank), "set sensor frame rate");
        }
    }

    // Configure CSIPHY and CSID input pads with the sensor's wire format.
    for (unsigned pad : {0u, 1u}) {
        auto fmt = format(phy, pad, sensorCode, width, height);
        if (fmt.width != width || fmt.height != height || fmt.code != sensorCode)
            throw std::runtime_error("receiver format mismatch");
    }
    auto csidIn = format(csid, 0, sensorCode, width, height);
    if (csidIn.width != width || csidIn.height != height || csidIn.code != sensorCode)
        throw std::runtime_error("receiver format mismatch");

    unsigned vfeCode = sensorCode;
    uint32_t pixFmt = (sensorKind == Sensor::Front) ? V4L2_PIX_FMT_SBGGR10P
                                                    : V4L2_PIX_FMT_SGRBG10P;
    if (sensorKind == Sensor::FrontIr) {
        // Prefer hardware-unpacked 16-bit Y10 (PLAIN16 LE) when CSID supports Y10_2X8_PADHI_LE,
        // and fall back cleanly to packed MIPI Y10P on kernels without CSID Y10 unpack support.
        v4l2_mbus_framefmt csidOut{};
        if (tryFormat(csid, 1, MEDIA_BUS_FMT_Y10_2X8_PADHI_LE, width, height, csidOut) &&
            csidOut.code == MEDIA_BUS_FMT_Y10_2X8_PADHI_LE &&
            csidOut.width == width && csidOut.height == height) {
            vfeCode = MEDIA_BUS_FMT_Y10_2X8_PADHI_LE;
            pixFmt = V4L2_PIX_FMT_Y10;
        } else {
            csidOut = format(csid, 1, MEDIA_BUS_FMT_Y10_1X10, width, height);
            if (csidOut.width != width || csidOut.height != height ||
                csidOut.code != MEDIA_BUS_FMT_Y10_1X10)
                throw std::runtime_error("receiver format mismatch");
            vfeCode = MEDIA_BUS_FMT_Y10_1X10;
            pixFmt = V4L2_PIX_FMT_Y10P;
        }
    } else {
        auto csidOut = format(csid, 1, sensorCode, width, height);
        if (csidOut.width != width || csidOut.height != height || csidOut.code != sensorCode)
            throw std::runtime_error("receiver format mismatch");
    }

    for (unsigned pad : {0u, 1u}) {
        auto fmt = format(vfe, pad, vfeCode, width, height);
        if (fmt.width != width || fmt.height != height || fmt.code != vfeCode)
            throw std::runtime_error("receiver format mismatch");
    }

    v4l2_format f{};
    f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    f.fmt.pix_mp.width = width; f.fmt.pix_mp.height = height;
    f.fmt.pix_mp.pixelformat = pixFmt;
    f.fmt.pix_mp.field = V4L2_FIELD_NONE;
    check(call(s->video.v, VIDIOC_S_FMT, &f), "capture format");
    const bool packed10 = (pixFmt != V4L2_PIX_FMT_Y10);
    if (f.fmt.pix_mp.num_planes != 1 || f.fmt.pix_mp.width != width ||
        f.fmt.pix_mp.height != height || (packed10 && (width % 4)) ||
        f.fmt.pix_mp.pixelformat != pixFmt)
        throw std::runtime_error("unsupported capture layout");
    s->width = width; s->height = height;
    s->stride = f.fmt.pix_mp.plane_fmt[0].bytesperline;
    s->pixelFormat = pixFmt;
    const unsigned minStride = packed10 ? (width / 4 * 5) : (width * 2);
    if (s->stride < minStride) throw std::runtime_error("short capture stride");

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

std::unique_ptr<Capture> Capture::openIr(unsigned width, unsigned height, unsigned maxFps) {
    return std::make_unique<Capture>(Sensor::FrontIr, width, height, maxFps);
}

Sensor Capture::sensor() const { return s->sensorKind; }

uint32_t Capture::pixelFormat() const { return s->pixelFormat; }

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
            s->sensorKind != Sensor::Rear, {}, s->pixelFormat};
    auto* start = static_cast<uint8_t*>(s->map[b.index]) + plane.data_offset;
    f.raw10.assign(start, start + size_t(s->stride) * s->height);
    check(call(s->video.v, VIDIOC_QBUF, &b), "return camera buffer");
    return f;
}

void Capture::autoExposure(float mean) {
    // Allow the sensor's queued exposure update to reach the image before adjusting again.
    if (++s->aeFrames % 4 || !std::isfinite(mean) || mean < 0.f) return;
    ControlRange expRange{}, gainRange{};
    if (!queryControlOptional(s->sensor.v, V4L2_CID_EXPOSURE, expRange)) return;
    unsigned gainCid = (s->sensorKind == Sensor::FrontIr) ? V4L2_CID_GAIN : V4L2_CID_ANALOGUE_GAIN;
    if (!queryControlOptional(s->sensor.v, gainCid, gainRange)) {
        unsigned altCid = (gainCid == V4L2_CID_GAIN) ? V4L2_CID_ANALOGUE_GAIN : V4L2_CID_GAIN;
        if (!queryControlOptional(s->sensor.v, altCid, gainRange)) return;
        gainCid = altCid;
    }
    auto decision = computeAutoExposure(s->sensorKind, mean, expRange, gainRange);
    if (decision.action == AutoExposureDecision::Action::SetGain) {
        v4l2_control c{gainCid, decision.value};
        check(call(s->sensor.v, VIDIOC_S_CTRL, &c), "set analogue gain");
    } else if (decision.action == AutoExposureDecision::Action::SetExposure) {
        v4l2_control c{V4L2_CID_EXPOSURE, decision.value};
        check(call(s->sensor.v, VIDIOC_S_CTRL, &c), "set exposure");
    }
}

}
