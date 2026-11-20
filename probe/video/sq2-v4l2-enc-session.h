#pragma once

#include "sq2-c2-enc-codecs.h"

#include <arm_neon.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

struct Sq2EncParams {
    Sq2EncCodec codec = Sq2EncCodec::H264;
    uint32_t width = 1280;
    uint32_t height = 720;
    uint32_t fps = 30;
    uint32_t bitrate = 4000000;
    uint32_t bitrateMode = V4L2_MPEG_VIDEO_BITRATE_MODE_VBR;
    uint32_t gopSize = 30;
    int32_t profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
    int32_t level = V4L2_MPEG_VIDEO_H264_LEVEL_5_1;
    bool prependHeader = true;
};

struct Sq2EncodedPacket {
    std::vector<uint8_t> data;
    int64_t timestampUs = 0;
    bool keyFrame = false;
    bool eos = false;
};

class Sq2V4l2EncSession {
public:
    Sq2V4l2EncSession() = default;
    ~Sq2V4l2EncSession() { closeSession(); }

    Sq2V4l2EncSession(const Sq2V4l2EncSession &) = delete;
    Sq2V4l2EncSession &operator=(const Sq2V4l2EncSession &) = delete;

    bool isOpen() const { return mFd >= 0 && mStreaming; }
    uint32_t outStride() const { return mOutStride; }
    uint32_t outCodedHeight() const { return mOutCodedHeight; }
    uint32_t outSizeImage() const { return mOutSizeImage; }

    bool openSession(const Sq2EncParams &p) {
        closeSession();

        char path[64]{};
        mFd = sq2OpenVenusEncoderNode(path, sizeof(path));
        if (mFd < 0) return false;
        mParams = p;

        v4l2_format outFmt{};
        outFmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        outFmt.fmt.pix_mp.width = p.width;
        outFmt.fmt.pix_mp.height = p.height;
        outFmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
        outFmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        if (ioctl(mFd, VIDIOC_S_FMT, &outFmt) < 0) {
            closeSession();
            return false;
        }

        mOutCodedWidth = outFmt.fmt.pix_mp.width;
        mOutCodedHeight = outFmt.fmt.pix_mp.height;
        mOutStride = outFmt.fmt.pix_mp.plane_fmt[0].bytesperline;
        if (mOutStride < mOutCodedWidth) mOutStride = (mOutCodedWidth + 127u) & ~127u;
        mOutSizeImage = outFmt.fmt.pix_mp.plane_fmt[0].sizeimage;

        v4l2_selection sel{};
        sel.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        sel.target = V4L2_SEL_TGT_CROP;
        sel.r = {0, 0, p.width, p.height};
        (void)ioctl(mFd, VIDIOC_S_SELECTION, &sel);

        v4l2_format capFmt{};
        capFmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        capFmt.fmt.pix_mp.width = p.width;
        capFmt.fmt.pix_mp.height = p.height;
        capFmt.fmt.pix_mp.pixelformat = toV4l2Format(p.codec);
        capFmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        if (ioctl(mFd, VIDIOC_S_FMT, &capFmt) < 0) {
            closeSession();
            return false;
        }

        v4l2_streamparm sp{};
        sp.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        sp.parm.output.timeperframe.numerator = 1;
        sp.parm.output.timeperframe.denominator = std::clamp(p.fps, 1u, 240u);
        (void)ioctl(mFd, VIDIOC_S_PARM, &sp);

        setCtrl(V4L2_CID_MPEG_VIDEO_BITRATE_MODE, static_cast<int32_t>(p.bitrateMode));
        setCtrl(V4L2_CID_MPEG_VIDEO_BITRATE,
                static_cast<int32_t>(std::clamp(p.bitrate, 32000u, 160000000u)));
        setCtrl(V4L2_CID_MPEG_VIDEO_GOP_SIZE,
                static_cast<int32_t>(std::max(1u, p.gopSize)));
        setCtrl(V4L2_CID_MPEG_VIDEO_HEADER_MODE,
                p.prependHeader ? V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME
                                : V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE);

        if (p.codec == Sq2EncCodec::H264) {
            if (!setCtrl(V4L2_CID_MPEG_VIDEO_H264_PROFILE, p.profile)) {
                setCtrl(V4L2_CID_MPEG_VIDEO_H264_PROFILE, V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);
            }
            if (!setCtrl(V4L2_CID_MPEG_VIDEO_H264_LEVEL, p.level)) {
                setCtrl(V4L2_CID_MPEG_VIDEO_H264_LEVEL, V4L2_MPEG_VIDEO_H264_LEVEL_5_1);
            }
        } else if (p.codec == Sq2EncCodec::HEVC) {
            setCtrl(V4L2_CID_MPEG_VIDEO_HEVC_PROFILE, V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN);
            if (!setCtrl(V4L2_CID_MPEG_VIDEO_HEVC_LEVEL, p.level)) {
                setCtrl(V4L2_CID_MPEG_VIDEO_HEVC_LEVEL, V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1);
            }
        }

        if (!allocMmapQueue(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 6, mOutBufs) ||
            !allocMmapQueue(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, 6, mCapBufs)) {
            closeSession();
            return false;
        }

        mOutFree.assign(mOutBufs.size(), true);
        for (uint32_t i = 0; i < mCapBufs.size(); ++i) {
            if (!qCapBuf(i)) {
                closeSession();
                return false;
            }
        }

        int capType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        int outType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        if (ioctl(mFd, VIDIOC_STREAMON, &capType) < 0 ||
            ioctl(mFd, VIDIOC_STREAMON, &outType) < 0) {
            closeSession();
            return false;
        }
        mStreaming = true;
        mEosSent = false;
        return true;
    }

    void closeSession() {
        if (mFd >= 0) {
            if (mStreaming) {
                int outType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
                int capType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
                ioctl(mFd, VIDIOC_STREAMOFF, &outType);
                ioctl(mFd, VIDIOC_STREAMOFF, &capType);
                mStreaming = false;
            }
            for (auto &b : mOutBufs) {
                if (b.ptr && b.ptr != MAP_FAILED) munmap(b.ptr, b.len);
            }
            for (auto &b : mCapBufs) {
                if (b.ptr && b.ptr != MAP_FAILED) munmap(b.ptr, b.len);
            }
            mOutBufs.clear();
            mCapBufs.clear();
            mOutFree.clear();
            close(mFd);
            mFd = -1;
        }
    }

    bool flushRestart() {
        if (mFd < 0) return false;
        Sq2EncParams copy = mParams;
        return openSession(copy);
    }

    bool updateBitrate(uint32_t bitrate) {
        if (mFd < 0) return false;
        mParams.bitrate = bitrate;
        return setCtrl(V4L2_CID_MPEG_VIDEO_BITRATE,
                       static_cast<int32_t>(std::clamp(bitrate, 32000u, 160000000u)));
    }

    bool requestKeyFrame() {
        if (mFd < 0) return false;
        return setCtrl(V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 1);
    }

    int acquireInputBuffer(int timeoutMs) {
        reclaimOutputBuffers();
        for (size_t i = 0; i < mOutFree.size(); ++i) {
            if (mOutFree[i]) return static_cast<int>(i);
        }
        pollfd pfd{mFd, POLLOUT | POLLIN, 0};
        if (poll(&pfd, 1, timeoutMs) > 0) {
            reclaimOutputBuffers();
            for (size_t i = 0; i < mOutFree.size(); ++i) {
                if (mOutFree[i]) return static_cast<int>(i);
            }
        }
        return -1;
    }

    void *inputBufferPtr(int idx) {
        if (idx < 0 || static_cast<size_t>(idx) >= mOutBufs.size()) return nullptr;
        return mOutBufs[idx].ptr;
    }

    size_t inputBufferLen(int idx) const {
        if (idx < 0 || static_cast<size_t>(idx) >= mOutBufs.size()) return 0;
        return mOutBufs[idx].len;
    }

    bool queueInputFrame(int idx, int64_t timestampUs, bool forceSync) {
        if (!mStreaming || idx < 0 || static_cast<size_t>(idx) >= mOutBufs.size()) return false;
        if (forceSync) requestKeyFrame();
        v4l2_plane pl{};
        pl.length = static_cast<uint32_t>(mOutBufs[idx].len);
        pl.bytesused = std::min(mOutSizeImage, static_cast<uint32_t>(mOutBufs[idx].len));
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = static_cast<uint32_t>(idx);
        b.length = 1;
        b.m.planes = &pl;
        b.timestamp.tv_sec = timestampUs / 1000000LL;
        b.timestamp.tv_usec = timestampUs % 1000000LL;
        if (ioctl(mFd, VIDIOC_QBUF, &b) < 0) return false;
        mOutFree[idx] = false;
        return true;
    }

    bool queueEos(int64_t timestampUs) {
        if (!mStreaming) return false;
        if (mEosSent) return true;
        int idx = acquireInputBuffer(500);
        if (idx < 0) return false;
        v4l2_plane pl{};
        pl.length = static_cast<uint32_t>(mOutBufs[idx].len);
        pl.bytesused = 0;
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = static_cast<uint32_t>(idx);
        b.length = 1;
        b.m.planes = &pl;
        b.flags = V4L2_BUF_FLAG_LAST;
        b.timestamp.tv_sec = timestampUs / 1000000LL;
        b.timestamp.tv_usec = timestampUs % 1000000LL;
        if (ioctl(mFd, VIDIOC_QBUF, &b) < 0) return false;
        mOutFree[idx] = false;
        mEosSent = true;
        return true;
    }

    // Returns 1 if packet popped, 0 if none ready, -1 on error.
    int dequeueOutput(Sq2EncodedPacket &pkt, int timeoutMs) {
        reclaimOutputBuffers();
        if (!mStreaming) return -1;
        auto tryDq = [&]() -> int {
            v4l2_plane pl{};
            v4l2_buffer b{};
            b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            b.memory = V4L2_MEMORY_MMAP;
            b.length = 1;
            b.m.planes = &pl;
            if (ioctl(mFd, VIDIOC_DQBUF, &b) < 0) {
                if (errno == EAGAIN) return 0;
                if (errno == EPIPE) return 2;
                fprintf(stderr, "SQ2_VENC DQBUF err errno=%d (%s)\n", errno, strerror(errno));
                return -1;
            }
            if (b.flags & V4L2_BUF_FLAG_ERROR) {
                fprintf(stderr, "SQ2_VENC DQBUF FLAG_ERROR flags=0x%x bytes=%u\n", b.flags, pl.bytesused);
                qCapBuf(b.index);
                return -1;
            }
            const uint8_t *src = static_cast<const uint8_t *>(mCapBufs[b.index].ptr) + pl.data_offset;
            uint32_t payload = (pl.bytesused > pl.data_offset) ? (pl.bytesused - pl.data_offset) : 0;
            pkt.data.assign(src, src + payload);
            pkt.timestampUs = static_cast<int64_t>(b.timestamp.tv_sec) * 1000000LL + b.timestamp.tv_usec;
            pkt.keyFrame = (b.flags & V4L2_BUF_FLAG_KEYFRAME) != 0;
            pkt.eos = (b.flags & V4L2_BUF_FLAG_LAST) != 0;
            qCapBuf(b.index);
            return 1;
        };
        int rc = tryDq();
        if (rc != 0 || timeoutMs <= 0) return rc;
        pollfd pfd{mFd, POLLIN, 0};
        if (poll(&pfd, 1, timeoutMs) > 0) {
            return tryDq();
        }
        return 0;
    }

    void fillNv12FromRgba(void *dstBuf, const uint8_t *rPtr, const uint8_t *gPtr, const uint8_t *bPtr,
                          int32_t colInc, int32_t rowInc, uint32_t width, uint32_t height) const {
        uint8_t *dstY = static_cast<uint8_t *>(dstBuf);
        uint8_t *dstUV = dstY + static_cast<size_t>(mOutStride) * mOutCodedHeight;
        uint32_t w = std::min(width, mParams.width);
        uint32_t h = std::min(height, mParams.height);

        const ptrdiff_t dG = gPtr - rPtr;
        const ptrdiff_t dB = bPtr - rPtr;
        const bool packedRgba = (colInc == 4 && dG == 1 && dB == 2);
        const bool packedBgra = (colInc == 4 && (rPtr - bPtr) == 2 && (gPtr - bPtr) == 1);

        if (packedRgba || packedBgra) {
            const uint8_t *base = packedRgba ? rPtr : bPtr;
            for (uint32_t y = 0; y < h; y += 2) {
                const uint8_t *src0 = base + static_cast<ptrdiff_t>(y) * rowInc;
                const uint8_t *src1 = (y + 1 < h) ? (src0 + rowInc) : src0;
                uint8_t *yRow0 = dstY + static_cast<size_t>(y) * mOutStride;
                uint8_t *yRow1 = (y + 1 < mOutCodedHeight) ? (yRow0 + mOutStride) : nullptr;
                uint8_t *uvRow = dstUV + static_cast<size_t>(y >> 1) * mOutStride;

                uint32_t x = 0;
                for (; x + 16 <= w; x += 16) {
                    uint8x16x4_t p0 = vld4q_u8(src0 + x * 4);
                    uint8x16x4_t p1 = vld4q_u8(src1 + x * 4);
                    uint8x16_t r0 = packedRgba ? p0.val[0] : p0.val[2];
                    uint8x16_t g0 = p0.val[1];
                    uint8x16_t b0 = packedRgba ? p0.val[2] : p0.val[0];
                    uint8x16_t r1 = packedRgba ? p1.val[0] : p1.val[2];
                    uint8x16_t g1 = p1.val[1];
                    uint8x16_t b1 = packedRgba ? p1.val[2] : p1.val[0];

                    // Y = ((66*R + 129*G + 25*B + 128) >> 8) + 16 (BT.601 limited)
                    auto calcY = [](uint8x16_t r, uint8x16_t g, uint8x16_t b) -> uint8x16_t {
                        uint16x8_t lo = vmlal_u8(vdupq_n_u16(128), vget_low_u8(r), vdup_n_u8(66));
                        lo = vmlal_u8(lo, vget_low_u8(g), vdup_n_u8(129));
                        lo = vmlal_u8(lo, vget_low_u8(b), vdup_n_u8(25));
                        uint16x8_t hi = vmlal_u8(vdupq_n_u16(128), vget_high_u8(r), vdup_n_u8(66));
                        hi = vmlal_u8(hi, vget_high_u8(g), vdup_n_u8(129));
                        hi = vmlal_u8(hi, vget_high_u8(b), vdup_n_u8(25));
                        uint8x8_t lo8 = vshrn_n_u16(lo, 8);
                        uint8x8_t hi8 = vshrn_n_u16(hi, 8);
                        return vaddq_u8(vcombine_u8(lo8, hi8), vdupq_n_u8(16));
                    };

                    vst1q_u8(yRow0 + x, calcY(r0, g0, b0));
                    if (yRow1 && y + 1 < h) vst1q_u8(yRow1 + x, calcY(r1, g1, b1));

                    // 2x2 average for chroma (8 pairs -> 8 U, 8 V)
                    uint16x8_t rAvg2 = vpaddlq_u8(r0);
                    rAvg2 = vpadalq_u8(rAvg2, r1);
                    uint16x8_t gAvg2 = vpaddlq_u8(g0);
                    gAvg2 = vpadalq_u8(gAvg2, g1);
                    uint16x8_t bAvg2 = vpaddlq_u8(b0);
                    bAvg2 = vpadalq_u8(bAvg2, b1);
                    uint16x8_t rAvg = vrshrq_n_u16(rAvg2, 2);
                    uint16x8_t gAvg = vrshrq_n_u16(gAvg2, 2);
                    uint16x8_t bAvg = vrshrq_n_u16(bAvg2, 2);

                    int16x8_t rs = vreinterpretq_s16_u16(rAvg);
                    int16x8_t gs = vreinterpretq_s16_u16(gAvg);
                    int16x8_t bs = vreinterpretq_s16_u16(bAvg);

                    int16x8_t u16 = vsubq_s16(vsubq_s16(vmulq_n_s16(bs, 112), vmulq_n_s16(rs, 38)),
                                              vmulq_n_s16(gs, 74));
                    u16 = vaddq_s16(u16, vdupq_n_s16(128));
                    int16x8_t v16 = vsubq_s16(vsubq_s16(vmulq_n_s16(rs, 112), vmulq_n_s16(gs, 94)),
                                              vmulq_n_s16(bs, 18));
                    v16 = vaddq_s16(v16, vdupq_n_s16(128));

                    uint8x8_t u8 = vreinterpret_u8_s8(vadd_s8(vshrn_n_s16(u16, 8), vdup_n_s8(-128)));
                    uint8x8_t v8 = vreinterpret_u8_s8(vadd_s8(vshrn_n_s16(v16, 8), vdup_n_s8(-128)));
                    uint8x8x2_t uv{u8, v8};
                    vst2_u8(uvRow + x, uv);
                }

                for (; x + 1 < w; x += 2) {
                    const uint8_t *p00 = src0 + x * 4;
                    const uint8_t *p01 = src0 + (x + 1) * 4;
                    const uint8_t *p10 = src1 + x * 4;
                    const uint8_t *p11 = src1 + (x + 1) * 4;
                    auto getR = [&](const uint8_t *q) -> int { return packedRgba ? q[0] : q[2]; };
                    auto getG = [&](const uint8_t *q) -> int { return q[1]; };
                    auto getB = [&](const uint8_t *q) -> int { return packedRgba ? q[2] : q[0]; };
                    auto luma = [&](const uint8_t *q) -> uint8_t {
                        return static_cast<uint8_t>(((66 * getR(q) + 129 * getG(q) + 25 * getB(q) + 128) >> 8) + 16);
                    };
                    yRow0[x] = luma(p00);
                    yRow0[x + 1] = luma(p01);
                    if (yRow1 && y + 1 < h) {
                        yRow1[x] = luma(p10);
                        yRow1[x + 1] = luma(p11);
                    }
                    int rA = (getR(p00) + getR(p01) + getR(p10) + getR(p11) + 2) >> 2;
                    int gA = (getG(p00) + getG(p01) + getG(p10) + getG(p11) + 2) >> 2;
                    int bA = (getB(p00) + getB(p01) + getB(p10) + getB(p11) + 2) >> 2;
                    int u = ((-38 * rA - 74 * gA + 112 * bA + 128) >> 8) + 128;
                    int v = ((112 * rA - 94 * gA - 18 * bA + 128) >> 8) + 128;
                    uvRow[x] = static_cast<uint8_t>(std::clamp(u, 0, 255));
                    uvRow[x + 1] = static_cast<uint8_t>(std::clamp(v, 0, 255));
                }
            }
            return;
        }

        // Generic stride/offset fallback
        for (uint32_t y = 0; y < h; ++y) {
            uint8_t *yRow = dstY + static_cast<size_t>(y) * mOutStride;
            uint8_t *uvRow = ((y & 1u) == 0) ? (dstUV + static_cast<size_t>(y >> 1) * mOutStride) : nullptr;
            for (uint32_t x = 0; x < w; ++x) {
                ptrdiff_t off = static_cast<ptrdiff_t>(y) * rowInc + static_cast<ptrdiff_t>(x) * colInc;
                int r = rPtr[off], g = gPtr[off], b = bPtr[off];
                yRow[x] = static_cast<uint8_t>(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
                if (uvRow && (x & 1u) == 0 && x + 1 < w) {
                    int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                    int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                    uvRow[x] = static_cast<uint8_t>(std::clamp(u, 0, 255));
                    uvRow[x + 1] = static_cast<uint8_t>(std::clamp(v, 0, 255));
                }
            }
        }
    }

    void fillNv12FromYuv(void *dstBuf, const uint8_t *yPtr, const uint8_t *uPtr, const uint8_t *vPtr,
                         int32_t yRowInc, int32_t uRowInc, int32_t vRowInc,
                         int32_t uColInc, int32_t vColInc, uint32_t width, uint32_t height) const {
        uint8_t *dstY = static_cast<uint8_t *>(dstBuf);
        uint8_t *dstUV = dstY + static_cast<size_t>(mOutStride) * mOutCodedHeight;
        uint32_t w = std::min(width, mParams.width);
        uint32_t h = std::min(height, mParams.height);

        for (uint32_t y = 0; y < h; ++y) {
            memcpy(dstY + static_cast<size_t>(y) * mOutStride,
                   yPtr + static_cast<ptrdiff_t>(y) * yRowInc, w);
        }
        uint32_t uvH = h / 2;
        uint32_t uvW = w / 2;
        if (uColInc == 2 && vColInc == 2 && uPtr + 1 == vPtr && uRowInc == vRowInc) {
            for (uint32_t y = 0; y < uvH; ++y) {
                memcpy(dstUV + static_cast<size_t>(y) * mOutStride,
                       uPtr + static_cast<ptrdiff_t>(y) * uRowInc, uvW * 2);
            }
            return;
        }
        for (uint32_t y = 0; y < uvH; ++y) {
            uint8_t *uvRow = dstUV + static_cast<size_t>(y) * mOutStride;
            const uint8_t *uRow = uPtr + static_cast<ptrdiff_t>(y) * uRowInc;
            const uint8_t *vRow = vPtr + static_cast<ptrdiff_t>(y) * vRowInc;
            for (uint32_t x = 0; x < uvW; ++x) {
                uvRow[2 * x] = uRow[static_cast<ptrdiff_t>(x) * uColInc];
                uvRow[2 * x + 1] = vRow[static_cast<ptrdiff_t>(x) * vColInc];
            }
        }
    }

private:
    struct MmapBuf { void *ptr = nullptr; size_t len = 0; };

    static uint32_t toV4l2Format(Sq2EncCodec c) {
        switch (c) {
            case Sq2EncCodec::H264: return V4L2_PIX_FMT_H264;
            case Sq2EncCodec::HEVC: return V4L2_PIX_FMT_HEVC;
            case Sq2EncCodec::VP8:  return V4L2_PIX_FMT_VP8;
        }
        return V4L2_PIX_FMT_H264;
    }

    bool setCtrl(uint32_t id, int32_t val) {
        v4l2_control c{id, val};
        return ioctl(mFd, VIDIOC_S_CTRL, &c) == 0;
    }

    bool allocMmapQueue(uint32_t type, uint32_t count, std::vector<MmapBuf> &bufs) {
        v4l2_requestbuffers req{};
        req.type = type;
        req.memory = V4L2_MEMORY_MMAP;
        req.count = count;
        if (ioctl(mFd, VIDIOC_REQBUFS, &req) < 0 || req.count == 0) return false;
        bufs.resize(req.count);
        for (uint32_t i = 0; i < req.count; ++i) {
            v4l2_plane pl{};
            v4l2_buffer b{};
            b.type = type;
            b.memory = V4L2_MEMORY_MMAP;
            b.index = i;
            b.length = 1;
            b.m.planes = &pl;
            if (ioctl(mFd, VIDIOC_QUERYBUF, &b) < 0) return false;
            void *p = mmap(nullptr, pl.length, PROT_READ | PROT_WRITE, MAP_SHARED, mFd, pl.m.mem_offset);
            if (p == MAP_FAILED) return false;
            bufs[i] = {p, pl.length};
        }
        return true;
    }

    bool qCapBuf(uint32_t idx) {
        v4l2_plane pl{};
        pl.length = static_cast<uint32_t>(mCapBufs[idx].len);
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = idx;
        b.length = 1;
        b.m.planes = &pl;
        return ioctl(mFd, VIDIOC_QBUF, &b) == 0;
    }

    void reclaimOutputBuffers() {
        while (mFd >= 0 && mStreaming) {
            v4l2_plane pl{};
            v4l2_buffer b{};
            b.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            b.memory = V4L2_MEMORY_MMAP;
            b.length = 1;
            b.m.planes = &pl;
            if (ioctl(mFd, VIDIOC_DQBUF, &b) < 0) break;
            if (b.index < mOutFree.size()) mOutFree[b.index] = true;
        }
    }

    int mFd = -1;
    bool mStreaming = false;
    bool mEosSent = false;
    Sq2EncParams mParams{};
    uint32_t mOutCodedWidth = 0;
    uint32_t mOutCodedHeight = 0;
    uint32_t mOutStride = 0;
    uint32_t mOutSizeImage = 0;
    std::vector<MmapBuf> mOutBufs;
    std::vector<MmapBuf> mCapBufs;
    std::vector<bool> mOutFree;
};
