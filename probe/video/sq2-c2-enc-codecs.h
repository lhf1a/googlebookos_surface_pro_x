#pragma once

#include <linux/videodev2.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

enum class Sq2EncCodec {
    H264,
    HEVC,
    VP8,
};

struct Sq2EncCodecInfo {
    Sq2EncCodec codec;
    const char *c2Name;
    const char *mime;
    uint32_t v4l2Format;
    uint32_t minWidth;
    uint32_t maxWidth;
    uint32_t stepWidth;
    uint32_t minHeight;
    uint32_t maxHeight;
    uint32_t stepHeight;
    uint32_t maxFps;
    uint32_t maxBitrate;
};

static inline int sq2OpenVenusEncoderNode(char *outPath, size_t outPathSize) {
    for (int i = 0; i < 64; ++i) {
        char path[64]{};
        snprintf(path, sizeof(path), "/dev/video%d", i);
        int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        v4l2_capability cap{};
        uint32_t caps = 0;
        bool isVenus = (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) &&
                       (strncmp(reinterpret_cast<const char *>(cap.driver), "qcom-venus", 10) == 0);
        if (isVenus) {
            caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
        }
        bool isEnc = false;
        if (isVenus && (caps & V4L2_CAP_VIDEO_M2M_MPLANE)) {
            v4l2_fmtdesc f{};
            f.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            for (f.index = 0; ioctl(fd, VIDIOC_ENUM_FMT, &f) == 0; ++f.index) {
                if (f.pixelformat == V4L2_PIX_FMT_NV12) {
                    isEnc = true;
                    break;
                }
            }
        }
        if (isEnc) {
            if (outPath && outPathSize > 0) {
                snprintf(outPath, outPathSize, "%s", path);
            }
            return fd;
        }
        close(fd);
    }
    return -1;
}

static inline std::vector<Sq2EncCodecInfo> sq2ProbeVenusEncoders() {
    std::vector<Sq2EncCodecInfo> result;
    int fd = sq2OpenVenusEncoderNode(nullptr, 0);
    if (fd < 0) return result;

    struct Candidate {
        Sq2EncCodec codec;
        const char *c2Name;
        const char *mime;
        uint32_t v4l2Fmt;
        uint32_t defaultMaxBitrate;
    };
    static constexpr Candidate kCandidates[] = {
        { Sq2EncCodec::H264, "c2.sq2.avc.encoder",  "video/avc",           V4L2_PIX_FMT_H264, 160000000u },
        { Sq2EncCodec::HEVC, "c2.sq2.hevc.encoder", "video/hevc",          V4L2_PIX_FMT_HEVC, 160000000u },
        { Sq2EncCodec::VP8,  "c2.sq2.vp8.encoder",  "video/x-vnd.on2.vp8", V4L2_PIX_FMT_VP8,  100000000u },
    };

    std::vector<uint32_t> capFmts;
    v4l2_fmtdesc fdsc{};
    fdsc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    for (fdsc.index = 0; ioctl(fd, VIDIOC_ENUM_FMT, &fdsc) == 0; ++fdsc.index) {
        capFmts.push_back(fdsc.pixelformat);
    }

    for (const auto &c : kCandidates) {
        bool found = false;
        for (uint32_t pf : capFmts) {
            if (pf == c.v4l2Fmt) { found = true; break; }
        }
        if (!found) continue;

        Sq2EncCodecInfo info{};
        info.codec = c.codec;
        info.c2Name = c.c2Name;
        info.mime = c.mime;
        info.v4l2Format = c.v4l2Fmt;
        info.minWidth = 96;
        info.maxWidth = 4096;
        info.stepWidth = 2;
        info.minHeight = 96;
        info.maxHeight = 4096;
        info.stepHeight = 2;
        info.maxFps = 120;
        info.maxBitrate = c.defaultMaxBitrate;

        v4l2_frmsizeenum fs{};
        fs.pixel_format = c.v4l2Fmt;
        if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) == 0) {
            if (fs.type == V4L2_FRMSIZE_TYPE_STEPWISE || fs.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
                if (fs.stepwise.min_width >= 16) info.minWidth = fs.stepwise.min_width;
                if (fs.stepwise.max_width >= info.minWidth) info.maxWidth = fs.stepwise.max_width;
                if (fs.stepwise.min_height >= 16) info.minHeight = fs.stepwise.min_height;
                if (fs.stepwise.max_height >= info.minHeight) info.maxHeight = fs.stepwise.max_height;
            }
        }
        v4l2_frmivalenum fi{};
        fi.pixel_format = c.v4l2Fmt;
        fi.width = info.minWidth;
        fi.height = info.minHeight;
        if (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fi) == 0 &&
            (fi.type == V4L2_FRMIVAL_TYPE_STEPWISE || fi.type == V4L2_FRMIVAL_TYPE_CONTINUOUS) &&
            fi.stepwise.min.numerator > 0 && fi.stepwise.min.denominator > 0) {
            uint32_t maxFps = fi.stepwise.min.denominator / fi.stepwise.min.numerator;
            if (maxFps > 0) info.maxFps = maxFps;
        }
        v4l2_queryctrl qc{};
        qc.id = V4L2_CID_MPEG_VIDEO_BITRATE;
        if (ioctl(fd, VIDIOC_QUERYCTRL, &qc) == 0 && qc.maximum > 32000) {
            info.maxBitrate = static_cast<uint32_t>(qc.maximum);
        }
        result.push_back(info);
    }

    close(fd);
    return result;
}
