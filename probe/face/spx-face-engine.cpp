// SPDX-License-Identifier: GPL-2.0-only
#include "spx-face-engine.h"
#include "spx-face-crypto.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

namespace spx::face {
namespace {

constexpr char kStoreMagic[8] = {'S', 'P', 'X', 'F', '0', '0', '0', '1'};
constexpr uint32_t kStoreVersion = 1u;
constexpr int kAlignedSize = 112;
constexpr float kPi = 3.14159265358979323846f;

int64_t defaultBootTimeMs() {
    struct timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000LL;
}

bool mkdirRecursive(const std::string& path, mode_t mode) {
    if (path.empty()) return false;
    std::string cur;
    size_t pos = 0;
    if (path[0] == '/') {
        cur = "/";
        pos = 1;
    }
    while (pos <= path.size()) {
        size_t next = path.find('/', pos);
        std::string part =
            (next == std::string::npos) ? path.substr(pos) : path.substr(pos, next - pos);
        if (!part.empty()) {
            if (cur.size() > 1 || cur.empty()) cur += '/';
            if (cur == "/") {
                cur += part;
            } else {
                cur += part;
            }
            if (mkdir(cur.c_str(), mode) != 0 && errno != EEXIST) {
                return false;
            }
        }
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    chmod(path.c_str(), mode);
    return true;
}

bool readExactFile(const std::string& path, std::vector<uint8_t>& out, size_t maxBytes) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size < 0 || static_cast<size_t>(st.st_size) > maxBytes) {
        close(fd);
        return false;
    }
    out.resize(static_cast<size_t>(st.st_size));
    size_t off = 0;
    while (off < out.size()) {
        ssize_t r = read(fd, out.data() + off, out.size() - off);
        if (r < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return false;
        }
        if (r == 0) break;
        off += static_cast<size_t>(r);
    }
    close(fd);
    return off == out.size();
}

bool writeFileAtomic(const std::string& path, const uint8_t* data, size_t len, mode_t mode) {
    std::string tmp = path + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) return false;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmp.c_str());
            return false;
        }
        off += static_cast<size_t>(w);
    }
    if (fchmod(fd, mode) != 0 || fsync(fd) != 0) {
        close(fd);
        unlink(tmp.c_str());
        return false;
    }
    close(fd);
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

template <typename T>
inline void appendPod(std::vector<uint8_t>& dst, const T& v) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    dst.insert(dst.end(), p, p + sizeof(T));
}

template <typename T>
inline bool readPod(const uint8_t* src, size_t len, size_t& off, T& out) {
    if (off + sizeof(T) > len) return false;
    std::memcpy(&out, src + off, sizeof(T));
    off += sizeof(T);
    return true;
}

class IntegralImage {
public:
    unsigned w = 0;
    unsigned h = 0;
    std::vector<uint32_t> sum;
    std::vector<uint64_t> sqSum;

    void build(const uint8_t* gray, unsigned width, unsigned height) {
        w = width;
        h = height;
        const size_t stride = w + 1u;
        sum.assign(stride * (h + 1u), 0u);
        sqSum.assign(stride * (h + 1u), 0ull);
        for (unsigned y = 0; y < h; ++y) {
            uint32_t rowSum = 0;
            uint64_t rowSq = 0;
            const uint8_t* srow = gray + static_cast<size_t>(y) * w;
            uint32_t* drow = sum.data() + static_cast<size_t>(y + 1u) * stride;
            const uint32_t* prow = sum.data() + static_cast<size_t>(y) * stride;
            uint64_t* dqrow = sqSum.data() + static_cast<size_t>(y + 1u) * stride;
            const uint64_t* pqrow = sqSum.data() + static_cast<size_t>(y) * stride;
            for (unsigned x = 0; x < w; ++x) {
                const uint32_t v = srow[x];
                rowSum += v;
                rowSq += static_cast<uint64_t>(v) * v;
                drow[x + 1u] = prow[x + 1u] + rowSum;
                dqrow[x + 1u] = pqrow[x + 1u] + rowSq;
            }
        }
    }

    inline uint64_t rectSum(int x0, int y0, int x1, int y1) const {
        x0 = std::clamp(x0, 0, static_cast<int>(w));
        x1 = std::clamp(x1, 0, static_cast<int>(w));
        y0 = std::clamp(y0, 0, static_cast<int>(h));
        y1 = std::clamp(y1, 0, static_cast<int>(h));
        if (x1 <= x0 || y1 <= y0) return 0;
        const size_t s = w + 1u;
        return static_cast<uint64_t>(sum[y1 * s + x1]) + sum[y0 * s + x0] -
               sum[y0 * s + x1] - sum[y1 * s + x0];
    }

    inline float rectMean(int x0, int y0, int x1, int y1) const {
        x0 = std::clamp(x0, 0, static_cast<int>(w));
        x1 = std::clamp(x1, 0, static_cast<int>(w));
        y0 = std::clamp(y0, 0, static_cast<int>(h));
        y1 = std::clamp(y1, 0, static_cast<int>(h));
        if (x1 <= x0 || y1 <= y0) return 0.0f;
        const float area = static_cast<float>((x1 - x0) * (y1 - y0));
        return static_cast<float>(rectSum(x0, y0, x1, y1)) / area;
    }

    inline float rectStdDev(int x0, int y0, int x1, int y1) const {
        x0 = std::clamp(x0, 0, static_cast<int>(w));
        x1 = std::clamp(x1, 0, static_cast<int>(w));
        y0 = std::clamp(y0, 0, static_cast<int>(h));
        y1 = std::clamp(y1, 0, static_cast<int>(h));
        if (x1 <= x0 || y1 <= y0) return 0.0f;
        const size_t s = w + 1u;
        const double area = static_cast<double>((x1 - x0) * (y1 - y0));
        const double m1 = static_cast<double>(rectSum(x0, y0, x1, y1)) / area;
        const uint64_t s2 =
            sqSum[y1 * s + x1] + sqSum[y0 * s + x0] - sqSum[y0 * s + x1] - sqSum[y1 * s + x0];
        const double var = std::max(0.0, static_cast<double>(s2) / area - m1 * m1);
        return static_cast<float>(std::sqrt(var));
    }
};

float sampleSubblockSymmetry(const IntegralImage& ii, int bx, int by, int bw, int bh) {
    constexpr int kGrid = 8;
    float leftVals[kGrid * (kGrid / 2)]{};
    float rightVals[kGrid * (kGrid / 2)]{};
    int count = 0;
    double sumL = 0.0, sumR = 0.0;
    for (int r = 0; r < kGrid; ++r) {
        const int y0 = by + (r * bh) / kGrid;
        const int y1 = by + ((r + 1) * bh) / kGrid;
        for (int c = 0; c < kGrid / 2; ++c) {
            const int lx0 = bx + (c * bw) / kGrid;
            const int lx1 = bx + ((c + 1) * bw) / kGrid;
            const int rx0 = bx + ((kGrid - 1 - c) * bw) / kGrid;
            const int rx1 = bx + ((kGrid - c) * bw) / kGrid;
            const float lv = ii.rectMean(lx0, y0, lx1, y1);
            const float rv = ii.rectMean(rx0, y0, rx1, y1);
            leftVals[count] = lv;
            rightVals[count] = rv;
            sumL += lv;
            sumR += rv;
            ++count;
        }
    }
    const double meanL = sumL / count;
    const double meanR = sumR / count;
    double num = 0.0, denL = 0.0, denR = 0.0;
    for (int i = 0; i < count; ++i) {
        const double dl = leftVals[i] - meanL;
        const double dr = rightVals[i] - meanR;
        num += dl * dr;
        denL += dl * dl;
        denR += dr * dr;
    }
    const double denom = std::sqrt(denL * denR);
    if (denom < 1e-3) return 0.0f;
    return static_cast<float>(num / denom);
}

Point2f findWeightedMinPatch(const uint8_t* gray,
                             unsigned width,
                             unsigned height,
                             int x0,
                             int y0,
                             int x1,
                             int y1,
                             float priorX,
                             float priorY,
                             float sigmaScale) {
    x0 = std::clamp(x0, 2, static_cast<int>(width) - 3);
    x1 = std::clamp(x1, x0 + 1, static_cast<int>(width) - 2);
    y0 = std::clamp(y0, 2, static_cast<int>(height) - 3);
    y1 = std::clamp(y1, y0 + 1, static_cast<int>(height) - 2);

    float bestCost = std::numeric_limits<float>::max();
    float bestX = priorX;
    float bestY = priorY;
    const float invSig2 = 1.0f / std::max(4.0f, sigmaScale * sigmaScale);

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            uint32_t sum = 0;
            for (int dy = -2; dy <= 2; ++dy) {
                const uint8_t* row = gray + static_cast<size_t>(y + dy) * width;
                for (int dx = -2; dx <= 2; ++dx) {
                    sum += row[x + dx];
                }
            }
            const float mean = static_cast<float>(sum) / 25.0f;
            const float dx = static_cast<float>(x) - priorX;
            const float dy = static_cast<float>(y) - priorY;
            const float cost = mean + 35.0f * (dx * dx + dy * dy) * invSig2;
            if (cost < bestCost) {
                bestCost = cost;
                bestX = static_cast<float>(x);
                bestY = static_cast<float>(y);
            }
        }
    }
    return {bestX, bestY};
}

Point2f findWeightedMaxPatch(const uint8_t* gray,
                             unsigned width,
                             unsigned height,
                             int x0,
                             int y0,
                             int x1,
                             int y1,
                             float priorX,
                             float priorY,
                             float sigmaScale) {
    x0 = std::clamp(x0, 2, static_cast<int>(width) - 3);
    x1 = std::clamp(x1, x0 + 1, static_cast<int>(width) - 2);
    y0 = std::clamp(y0, 2, static_cast<int>(height) - 3);
    y1 = std::clamp(y1, y0 + 1, static_cast<int>(height) - 2);

    float bestScore = -std::numeric_limits<float>::max();
    float bestX = priorX;
    float bestY = priorY;
    const float invSig2 = 1.0f / std::max(4.0f, sigmaScale * sigmaScale);

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            uint32_t sum = 0;
            for (int dy = -2; dy <= 2; ++dy) {
                const uint8_t* row = gray + static_cast<size_t>(y + dy) * width;
                for (int dx = -2; dx <= 2; ++dx) {
                    sum += row[x + dx];
                }
            }
            const float mean = static_cast<float>(sum) / 25.0f;
            const float dx = static_cast<float>(x) - priorX;
            const float dy = static_cast<float>(y) - priorY;
            const float score = mean - 28.0f * (dx * dx + dy * dy) * invSig2;
            if (score > bestScore) {
                bestScore = score;
                bestX = static_cast<float>(x);
                bestY = static_cast<float>(y);
            }
        }
    }
    return {bestX, bestY};
}

inline float sampleBilinear(const uint8_t* img, unsigned w, unsigned h, float x, float y) {
    x = std::clamp(x, 0.0f, static_cast<float>(w - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(h - 1));
    const unsigned x0 = static_cast<unsigned>(x);
    const unsigned y0 = static_cast<unsigned>(y);
    const unsigned x1 = std::min(x0 + 1u, w - 1u);
    const unsigned y1 = std::min(y0 + 1u, h - 1u);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float v00 = img[static_cast<size_t>(y0) * w + x0];
    const float v10 = img[static_cast<size_t>(y0) * w + x1];
    const float v01 = img[static_cast<size_t>(y1) * w + x0];
    const float v11 = img[static_cast<size_t>(y1) * w + x1];
    const float top = v00 + fx * (v10 - v00);
    const float bot = v01 + fx * (v11 - v01);
    return top + fy * (bot - top);
}

void gaussianBlur112(const std::array<float, kAlignedSize * kAlignedSize>& src,
                     std::array<float, kAlignedSize * kAlignedSize>& dst,
                     float sigma) {
    const int radius = std::max(1, static_cast<int>(std::ceil(sigma * 2.5f)));
    std::vector<float> kernel(static_cast<size_t>(2 * radius + 1));
    float ksum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float w = std::exp(-0.5f * (i * i) / (sigma * sigma));
        kernel[static_cast<size_t>(i + radius)] = w;
        ksum += w;
    }
    for (float& w : kernel) w /= ksum;

    std::array<float, kAlignedSize * kAlignedSize> tmp{};
    for (int y = 0; y < kAlignedSize; ++y) {
        const float* srow = src.data() + y * kAlignedSize;
        float* drow = tmp.data() + y * kAlignedSize;
        for (int x = 0; x < kAlignedSize; ++x) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                const int sx = std::clamp(x + k, 0, kAlignedSize - 1);
                acc += srow[sx] * kernel[static_cast<size_t>(k + radius)];
            }
            drow[x] = acc;
        }
    }
    for (int y = 0; y < kAlignedSize; ++y) {
        float* drow = dst.data() + y * kAlignedSize;
        for (int x = 0; x < kAlignedSize; ++x) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                const int sy = std::clamp(y + k, 0, kAlignedSize - 1);
                acc += tmp[sy * kAlignedSize + x] * kernel[static_cast<size_t>(k + radius)];
            }
            drow[x] = acc;
        }
    }
}

void tanTriggsNormalize(std::array<float, kAlignedSize * kAlignedSize>& patch) {
    constexpr size_t N = kAlignedSize * kAlignedSize;
    for (size_t i = 0; i < N; ++i) {
        patch[i] = std::pow(std::max(0.0f, patch[i]) / 255.0f, 0.2f);
    }
    std::array<float, N> g1{};
    std::array<float, N> g2{};
    gaussianBlur112(patch, g1, 1.0f);
    gaussianBlur112(patch, g2, 2.0f);
    for (size_t i = 0; i < N; ++i) {
        patch[i] = g1[i] - g2[i];
    }

    double powSum = 0.0;
    for (float v : patch) powSum += std::pow(static_cast<double>(std::abs(v)), 0.1);
    const double d1 = std::max(1e-6, std::pow(powSum / N, 10.0));
    for (float& v : patch) v = static_cast<float>(v / d1);

    constexpr double kTau = 10.0;
    double powSum2 = 0.0;
    for (float v : patch) {
        const double tv = std::min(std::abs(static_cast<double>(v)), kTau);
        powSum2 += std::pow(tv, 0.1);
    }
    const double d2 = std::max(1e-6, std::pow(powSum2 / N, 10.0));
    for (float& v : patch) {
        v = static_cast<float>(kTau * std::tanh(static_cast<double>(v) / (d2 * kTau)));
    }
}

void buildNormalized32x32FacePatch(const IrFrameStats& frame,
                                   const FaceBox& box,
                                   std::array<uint8_t, 32 * 32>& outPatch) {
    for (int y = 0; y < 32; ++y) {
        const float sy = box.y + (static_cast<float>(y) + 0.5f) * box.h / 32.0f;
        for (int x = 0; x < 32; ++x) {
            const float sx = box.x + (static_cast<float>(x) + 0.5f) * box.w / 32.0f;
            const float val = sampleBilinear(frame.gray8.data(), frame.width, frame.height, sx, sy);
            outPatch[static_cast<size_t>(y) * 32u + x] =
                static_cast<uint8_t>(std::clamp(std::lround(val), 0L, 255L));
        }
    }
}

void l2Normalize(Embedding& emb) {
    double sq = 0.0;
    for (float v : emb) sq += static_cast<double>(v) * v;
    const float inv = (sq > 1e-12) ? static_cast<float>(1.0 / std::sqrt(sq)) : 0.0f;
    for (float& v : emb) v *= inv;
}

}  // namespace

bool OptionalModelWeights::loadFromFile(const std::string& path) {
    unload();
    std::vector<uint8_t> buf;
    if (!readExactFile(path, buf, 4u * 1024u * 1024u)) return false;
    // Binary format: magic "SPXM0001" (8B) + uint32 dim + mean[dim] + proj[dim * dim]
    constexpr size_t expectedSize =
        8u + 4u + (kEmbeddingDim + kEmbeddingDim * kEmbeddingDim) * sizeof(float);
    if (buf.size() != expectedSize || std::memcmp(buf.data(), "SPXM0001", 8) != 0) {
        return false;
    }
    uint32_t dim = 0;
    std::memcpy(&dim, buf.data() + 8, sizeof(dim));
    if (dim != kEmbeddingDim) return false;
    mean.resize(kEmbeddingDim);
    proj.resize(kEmbeddingDim * kEmbeddingDim);
    std::memcpy(mean.data(), buf.data() + 12, kEmbeddingDim * sizeof(float));
    std::memcpy(proj.data(),
                buf.data() + 12 + kEmbeddingDim * sizeof(float),
                kEmbeddingDim * kEmbeddingDim * sizeof(float));
    loaded = true;
    return true;
}

void OptionalModelWeights::unload() {
    mean.clear();
    mean.shrink_to_fit();
    proj.clear();
    proj.shrink_to_fit();
    loaded = false;
}

void FaceSessionAccumulator::reset(unsigned requiredConsecutiveFrames) {
    minConsecutiveFrames = std::max(1u, requiredConsecutiveFrames);
    frameCount = 0;
    consecutiveLiveFrames = 0;
    hasPrevPatch = false;
    prevPatch.fill(0);
    hasPrevEmbedding = false;
    prevEmbedding.fill(0.0f);
}

FaceDetection detectFaceNir(const IrFrameStats& frame) {
    FaceDetection res;
    if (frame.width < 32 || frame.height < 32 ||
        frame.gray8.size() < static_cast<size_t>(frame.width) * frame.height) {
        res.quality = FaceQuality::NOT_DETECTED;
        return res;
    }

    if (frame.p99 < 24 && frame.darkRatio > 0.90f) {
        res.quality = FaceQuality::TOO_DARK;
        return res;
    }
    if (frame.saturatedRatio > 0.70f) {
        res.quality = FaceQuality::TOO_BRIGHT;
        return res;
    }

    // Downsample 2x when frame >= 320px to keep multi-scale sliding-window scan < 4ms.
    const unsigned scale = (frame.width >= 320 && frame.height >= 320) ? 2u : 1u;
    const unsigned dw = frame.width / scale;
    const unsigned dh = frame.height / scale;
    std::vector<uint8_t> small(static_cast<size_t>(dw) * dh);
    if (scale == 2u) {
        for (unsigned y = 0; y < dh; ++y) {
            const uint8_t* r0 = frame.gray8.data() + static_cast<size_t>(y * 2u) * frame.width;
            const uint8_t* r1 = r0 + frame.width;
            uint8_t* d = small.data() + static_cast<size_t>(y) * dw;
            for (unsigned x = 0; x < dw; ++x) {
                const unsigned sum =
                    r0[x * 2u] + r0[x * 2u + 1u] + r1[x * 2u] + r1[x * 2u + 1u];
                d[x] = static_cast<uint8_t>(sum >> 2);
            }
        }
    } else {
        std::copy_n(frame.gray8.data(), small.size(), small.begin());
    }

    IntegralImage ii;
    ii.build(small.data(), dw, dh);

    const unsigned minDim = std::min(dw, dh);
    const int minSize = std::max(28, static_cast<int>(std::lround(minDim * 0.22f)));
    const int maxSize = std::min<int>(minDim - 4, static_cast<int>(std::lround(minDim * 0.88f)));

    struct Candidate {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        float score = -1e9f;
    };
    Candidate best;

    for (int bw = minSize; bw <= maxSize; bw = std::max(bw + 4, static_cast<int>(bw * 1.14f))) {
        const int bh = std::min<int>(dh - 2, static_cast<int>(std::lround(bw * 1.15f)));
        const int step = std::max(3, bw / 11);
        for (int by = 1; by + bh < static_cast<int>(dh); by += step) {
            for (int bx = 1; bx + bw < static_cast<int>(dw); bx += step) {
                const float coreStd =
                    ii.rectStdDev(bx + bw / 6, by + bh / 6, bx + (5 * bw) / 6, by + (5 * bh) / 6);
                if (coreStd < 12.0f) continue;

                const float coreMean =
                    ii.rectMean(bx + bw / 5, by + bh / 5, bx + (4 * bw) / 5, by + (4 * bh) / 5);
                const float eyeBand = ii.rectMean(
                    bx + (14 * bw) / 100, by + (22 * bh) / 100, bx + (86 * bw) / 100, by + (42 * bh) / 100);
                const float cheekBand = ii.rectMean(
                    bx + (16 * bw) / 100, by + (44 * bh) / 100, bx + (84 * bw) / 100, by + (65 * bh) / 100);
                if (cheekBand <= eyeBand + 2.0f) continue;

                const float leftEye = ii.rectMean(
                    bx + (16 * bw) / 100, by + (22 * bh) / 100, bx + (40 * bw) / 100, by + (42 * bh) / 100);
                const float rightEye = ii.rectMean(
                    bx + (60 * bw) / 100, by + (22 * bh) / 100, bx + (84 * bw) / 100, by + (42 * bh) / 100);
                const float noseBridge = ii.rectMean(
                    bx + (41 * bw) / 100, by + (24 * bh) / 100, bx + (59 * bw) / 100, by + (56 * bh) / 100);
                if (noseBridge <= 0.5f * (leftEye + rightEye) + 1.5f) continue;

                const float forehead = ii.rectMean(
                    bx + (24 * bw) / 100, by + (6 * bh) / 100, bx + (76 * bw) / 100, by + (20 * bh) / 100);
                const float mouthBand = ii.rectMean(
                    bx + (26 * bw) / 100, by + (67 * bh) / 100, bx + (74 * bw) / 100, by + (83 * bh) / 100);

                // Outer surround contrast under active 850nm illumination.
                const int pad = std::max(4, bw / 5);
                const float leftSurr =
                    (bx >= 6) ? ii.rectMean(std::max(0, bx - pad), by, bx, by + bh) : coreMean;
                const float rightSurr = (bx + bw + 6 <= static_cast<int>(dw))
                                            ? ii.rectMean(bx + bw, by, std::min<int>(dw, bx + bw + pad), by + bh)
                                            : coreMean;
                const float surrMean = 0.5f * (leftSurr + rightSurr);

                const float sym = sampleSubblockSymmetry(ii, bx, by, bw, bh);
                if (sym < 0.28f) continue;

                const float haarScore =
                    (cheekBand - eyeBand) * 1.3f +
                    (noseBridge - 0.5f * (leftEye + rightEye)) * 1.4f +
                    (forehead - eyeBand) * 0.8f +
                    (cheekBand - mouthBand) * 0.7f +
                    std::clamp(coreMean - surrMean, -15.0f, 45.0f) * 0.6f +
                    sym * 40.0f;

                if (haarScore > best.score) {
                    best = {bx, by, bw, bh, haarScore};
                }
            }
        }
    }

    if (best.score < 32.0f) {
        res.quality = FaceQuality::NOT_DETECTED;
        return res;
    }

    res.detected = true;
    res.score = best.score;
    res.box = {
        static_cast<float>(best.x * scale),
        static_cast<float>(best.y * scale),
        static_cast<float>(best.w * scale),
        static_cast<float>(best.h * scale),
    };

    const int fx = static_cast<int>(res.box.x);
    const int fy = static_cast<int>(res.box.y);
    const int fw = static_cast<int>(res.box.w);
    const int fh = static_cast<int>(res.box.h);

    res.leftEye = findWeightedMinPatch(frame.gray8.data(),
                                       frame.width,
                                       frame.height,
                                       fx + (fw * 18) / 100,
                                       fy + (fh * 22) / 100,
                                       fx + (fw * 42) / 100,
                                       fy + (fh * 44) / 100,
                                       res.box.x + 0.30f * res.box.w,
                                       res.box.y + 0.33f * res.box.h,
                                       0.09f * res.box.w);
    res.rightEye = findWeightedMinPatch(frame.gray8.data(),
                                        frame.width,
                                        frame.height,
                                        fx + (fw * 58) / 100,
                                        fy + (fh * 22) / 100,
                                        fx + (fw * 82) / 100,
                                        fy + (fh * 44) / 100,
                                        res.box.x + 0.70f * res.box.w,
                                        res.box.y + 0.33f * res.box.h,
                                        0.09f * res.box.w);
    res.noseTip = findWeightedMaxPatch(frame.gray8.data(),
                                       frame.width,
                                       frame.height,
                                       fx + (fw * 36) / 100,
                                       fy + (fh * 42) / 100,
                                       fx + (fw * 64) / 100,
                                       fy + (fh * 66) / 100,
                                       res.box.x + 0.50f * res.box.w,
                                       res.box.y + 0.53f * res.box.h,
                                       0.10f * res.box.w);
    res.mouthLeft = findWeightedMinPatch(frame.gray8.data(),
                                         frame.width,
                                         frame.height,
                                         fx + (fw * 24) / 100,
                                         fy + (fh * 68) / 100,
                                         fx + (fw * 46) / 100,
                                         fy + (fh * 84) / 100,
                                         res.box.x + 0.35f * res.box.w,
                                         res.box.y + 0.75f * res.box.h,
                                         0.10f * res.box.w);
    res.mouthRight = findWeightedMinPatch(frame.gray8.data(),
                                          frame.width,
                                          frame.height,
                                          fx + (fw * 54) / 100,
                                          fy + (fh * 68) / 100,
                                          fx + (fw * 76) / 100,
                                          fy + (fh * 84) / 100,
                                          res.box.x + 0.65f * res.box.w,
                                          res.box.y + 0.75f * res.box.h,
                                          0.10f * res.box.w);

    const float eyeMidX = 0.5f * (res.leftEye.x + res.rightEye.x);
    const float eyeMidY = 0.5f * (res.leftEye.y + res.rightEye.y);
    const float mouthMidY = 0.5f * (res.mouthLeft.y + res.mouthRight.y);
    const float interOcular = std::max(10.0f, res.rightEye.x - res.leftEye.x);
    const float eyeToMouth = std::max(10.0f, mouthMidY - eyeMidY);

    res.panDeg = ((res.noseTip.x - eyeMidX) / interOcular) * 55.0f;
    res.tiltDeg = (((res.noseTip.y - eyeMidY) / eyeToMouth) - 0.50f) * 90.0f;

    const float frameMinDim = static_cast<float>(std::min(frame.width, frame.height));
    res.distanceCm = (11500.0f * (frameMinDim / 480.0f)) / std::max(16.0f, res.box.w);

    const float cx = res.box.x + 0.5f * res.box.w;
    const float cy = res.box.y + 0.5f * res.box.h;
    if (res.box.w < 0.23f * frameMinDim) {
        res.quality = FaceQuality::TOO_FAR;
    } else if (res.box.w > 0.85f * frameMinDim) {
        res.quality = FaceQuality::TOO_CLOSE;
    } else if (cx < 0.16f * frame.width) {
        res.quality = FaceQuality::FACE_TOO_LEFT;
    } else if (cx > 0.84f * frame.width) {
        res.quality = FaceQuality::FACE_TOO_RIGHT;
    } else if (cy < 0.14f * frame.height) {
        res.quality = FaceQuality::FACE_TOO_HIGH;
    } else if (cy > 0.86f * frame.height) {
        res.quality = FaceQuality::FACE_TOO_LOW;
    } else if (std::abs(res.panDeg) > 30.0f) {
        res.quality = FaceQuality::PAN_TOO_EXTREME;
    } else if (std::abs(res.tiltDeg) > 25.0f) {
        res.quality = FaceQuality::TILT_TOO_EXTREME;
    } else {
        res.quality = FaceQuality::GOOD;
    }
    return res;
}

Embedding extractEmbedding(const IrFrameStats& frame,
                           const FaceDetection& det,
                           const OptionalModelWeights* model) {
    Embedding emb{};
    if (!det.detected || frame.gray8.empty()) return emb;

    const float scaleRatio = static_cast<float>(kAlignedSize) / 112.0f;
    const std::array<Point2f, 5> dstPts{{
        {38.2946f * scaleRatio, 51.6963f * scaleRatio},
        {73.5318f * scaleRatio, 51.5014f * scaleRatio},
        {56.0252f * scaleRatio, 71.7366f * scaleRatio},
        {41.5493f * scaleRatio, 92.3655f * scaleRatio},
        {70.7299f * scaleRatio, 92.2041f * scaleRatio},
    }};
    const std::array<Point2f, 5> srcPts{{
        det.leftEye, det.rightEye, det.noseTip, det.mouthLeft, det.mouthRight,
    }};

    // Least-squares 4-DOF similarity transform from canonical (112x112) -> source frame.
    Point2f srcMean{}, dstMean{};
    for (int i = 0; i < 5; ++i) {
        srcMean.x += srcPts[i].x;
        srcMean.y += srcPts[i].y;
        dstMean.x += dstPts[i].x;
        dstMean.y += dstPts[i].y;
    }
    srcMean.x *= 0.2f;
    srcMean.y *= 0.2f;
    dstMean.x *= 0.2f;
    dstMean.y *= 0.2f;

    double numA = 0.0, numB = 0.0, den = 0.0;
    for (int i = 0; i < 5; ++i) {
        const double ux = dstPts[i].x - dstMean.x;
        const double uy = dstPts[i].y - dstMean.y;
        const double vx = srcPts[i].x - srcMean.x;
        const double vy = srcPts[i].y - srcMean.y;
        den += ux * ux + uy * uy;
        numA += ux * vx + uy * vy;
        numB += ux * vy - uy * vx;
    }
    const double a = (den > 1e-6) ? (numA / den) : (det.box.w / kAlignedSize);
    const double b = (den > 1e-6) ? (numB / den) : 0.0;
    const double tx = srcMean.x - (a * dstMean.x - b * dstMean.y);
    const double ty = srcMean.y - (b * dstMean.x + a * dstMean.y);

    std::array<float, kAlignedSize * kAlignedSize> patch{};
    for (int y = 0; y < kAlignedSize; ++y) {
        for (int x = 0; x < kAlignedSize; ++x) {
            const float sx = static_cast<float>(a * x - b * y + tx);
            const float sy = static_cast<float>(b * x + a * y + ty);
            patch[static_cast<size_t>(y) * kAlignedSize + x] =
                sampleBilinear(frame.gray8.data(), frame.width, frame.height, sx, sy);
        }
    }

    // Compute mid-band illumination-normalized structure (sigma 1.2 vs 7.0)
    // alongside fine-band Tan & Triggs DoG (sigma 1.0 vs 2.0) so both geometric
    // facial proportions and fine skin/edge orientation contribute equally.
    std::array<float, kAlignedSize * kAlignedSize> gammaPatch{};
    for (size_t i = 0; i < patch.size(); ++i) {
        gammaPatch[i] = std::pow(std::max(0.0f, patch[i]) / 255.0f, 0.25f);
    }
    std::array<float, kAlignedSize * kAlignedSize> bFine{}, bCoarse{}, midBand{};
    gaussianBlur112(gammaPatch, bFine, 1.2f);
    gaussianBlur112(gammaPatch, bCoarse, 7.0f);
    double midSq = 0.0;
    for (size_t i = 0; i < patch.size(); ++i) {
        midBand[i] = bFine[i] - bCoarse[i];
        midSq += static_cast<double>(midBand[i]) * midBand[i];
    }
    const float midRms = static_cast<float>(std::sqrt(midSq / patch.size()) + 1e-6);
    for (float& v : midBand) {
        v = std::tanh(v / (2.5f * midRms));
    }

    tanTriggsNormalize(patch);

    size_t dimIdx = 0;
    auto normalizeSlice = [&](size_t start, size_t count) {
        double sq = 0.0;
        for (size_t i = start; i < start + count; ++i) sq += static_cast<double>(emb[i]) * emb[i];
        const float inv = (sq > 1e-12) ? static_cast<float>(1.0 / std::sqrt(sq)) : 0.0f;
        for (size_t i = start; i < start + count; ++i) emb[i] *= inv;
    };

    // 1a. 64 dims: 8x8 zero-centered spatial appearance grid on midBand [8..104].
    {
        const size_t s0 = dimIdx;
        float mean64 = 0.0f;
        for (int gy = 0; gy < 8; ++gy) {
            const int y0 = 8 + gy * 12;
            const int y1 = y0 + 12;
            for (int gx = 0; gx < 8; ++gx) {
                const int x0 = 8 + gx * 12;
                const int x1 = x0 + 12;
                float sum = 0.0f;
                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) sum += midBand[y * kAlignedSize + x];
                }
                const float v = sum / 144.0f;
                emb[dimIdx++] = v;
                mean64 += v;
            }
        }
        mean64 /= 64.0f;
        for (size_t i = s0; i < s0 + 64; ++i) emb[i] -= mean64;
        normalizeSlice(s0, 64);
    }

    // 1b. 64 dims: 4x4 spatial grid x 4 zero-centered steerable gradient energies on patch.
    {
        const size_t s0 = dimIdx;
        for (int gy = 0; gy < 4; ++gy) {
            const int y0 = 8 + gy * 24;
            const int y1 = y0 + 24;
            for (int gx = 0; gx < 4; ++gx) {
                const int x0 = 8 + gx * 24;
                const int x1 = x0 + 24;
                std::array<float, 4> ori{};
                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        const float dx = patch[y * kAlignedSize + (x + 1)] - patch[y * kAlignedSize + (x - 1)];
                        const float dy = patch[(y + 1) * kAlignedSize + x] - patch[(y - 1) * kAlignedSize + x];
                        const float d1 = patch[(y + 1) * kAlignedSize + (x + 1)] - patch[(y - 1) * kAlignedSize + (x - 1)];
                        const float d2 = patch[(y + 1) * kAlignedSize + (x - 1)] - patch[(y - 1) * kAlignedSize + (x + 1)];
                        ori[0] += std::abs(dx);
                        ori[1] += std::abs(dy);
                        ori[2] += std::abs(d1) * 0.7071f;
                        ori[3] += std::abs(d2) * 0.7071f;
                    }
                }
                const float m = 0.25f * (ori[0] + ori[1] + ori[2] + ori[3]);
                for (int k = 0; k < 4; ++k) emb[dimIdx++] = ori[k] - m;
            }
        }
        normalizeSlice(s0, 64);
    }

    // 2. 96 dims: 12 facial landmark patches x 8 zero-centered signed HOG bins.
    {
        const size_t s0 = dimIdx;
        static constexpr std::array<std::pair<int, int>, 12> kHogCenters{{
            {38, 52}, {74, 52}, {56, 48}, {36, 70},
            {56, 62}, {76, 70}, {56, 72}, {46, 74},
            {66, 74}, {42, 92}, {56, 92}, {70, 92},
        }};
        for (const auto& [cx, cy] : kHogCenters) {
            std::array<float, 8> hog{};
            for (int dy = -12; dy <= 12; ++dy) {
                const int y = std::clamp(cy + dy, 1, kAlignedSize - 2);
                for (int dx = -12; dx <= 12; ++dx) {
                    const int x = std::clamp(cx + dx, 1, kAlignedSize - 2);
                    const float gx = patch[y * kAlignedSize + (x + 1)] - patch[y * kAlignedSize + (x - 1)];
                    const float gy = patch[(y + 1) * kAlignedSize + x] - patch[(y - 1) * kAlignedSize + x];
                    const float mag = std::sqrt(gx * gx + gy * gy);
                    float ang = std::atan2(gy, gx);
                    if (ang < 0.0f) ang += 2.0f * kPi;
                    const int bin = static_cast<int>((ang * 8.0f) / (2.0f * kPi)) & 7;
                    hog[bin] += mag;
                }
            }
            float m = 0.0f;
            for (float h : hog) m += h;
            m *= 0.125f;
            float norm = 0.0f;
            for (float& h : hog) {
                h -= m;
                norm += h * h;
            }
            norm = std::sqrt(norm) + 1e-5f;
            for (int k = 0; k < 8; ++k) emb[dimIdx++] = hog[k] / norm;
        }
        normalizeSlice(s0, 96);
    }

    // 3. 32 dims: low-frequency 6x6 2D DCT holistic AC coefficients on midBand.
    {
        const size_t s0 = dimIdx;
        std::array<float, 32 * 32> coarse{};
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                const int sx = 8 + (x * 96) / 32;
                const int sy = 8 + (y * 96) / 32;
                float acc = 0.0f;
                for (int dy = 0; dy < 3; ++dy) {
                    for (int dx = 0; dx < 3; ++dx) {
                        acc += midBand[(sy + dy) * kAlignedSize + (sx + dx)];
                    }
                }
                coarse[y * 32 + x] = acc / 9.0f;
            }
        }
        size_t dctCount = 0;
        for (int v = 0; v < 6 && dctCount < 32; ++v) {
            for (int u = 0; u < 6 && dctCount < 32; ++u) {
                if ((u + v) <= 1 || (u == 1 && v == 1)) continue;
                float sum = 0.0f;
                for (int y = 0; y < 32; ++y) {
                    const float cy = std::cos((kPi * (2 * y + 1) * v) / 64.0f);
                    for (int x = 0; x < 32; ++x) {
                        const float cx = std::cos((kPi * (2 * x + 1) * u) / 64.0f);
                        sum += coarse[y * 32 + x] * cx * cy;
                    }
                }
                emb[dimIdx++] = sum / 32.0f;
                ++dctCount;
            }
        }
        normalizeSlice(s0, 32);
    }

    if (model && model->loaded && model->mean.size() == kEmbeddingDim &&
        model->proj.size() == kEmbeddingDim * kEmbeddingDim) {
        Embedding projected{};
        for (size_t r = 0; r < kEmbeddingDim; ++r) {
            double acc = 0.0;
            const float* row = model->proj.data() + r * kEmbeddingDim;
            for (size_t c = 0; c < kEmbeddingDim; ++c) {
                acc += static_cast<double>(row[c]) * (emb[c] - model->mean[c]);
            }
            projected[r] = static_cast<float>(acc);
        }
        emb = projected;
    }

    l2Normalize(emb);
    return emb;
}

float cosineSimilarity(const Embedding& a, const Embedding& b) {
    double dot = 0.0;
    double na = 0.0;
    double nb = 0.0;
    for (size_t i = 0; i < kEmbeddingDim; ++i) {
        dot += static_cast<double>(a[i]) * b[i];
        na += static_cast<double>(a[i]) * a[i];
        nb += static_cast<double>(b[i]) * b[i];
    }
    const double denom = std::sqrt(na * nb);
    if (denom < 1e-12) return 0.0f;
    return static_cast<float>(dot / denom);
}

LivenessReport evaluateLiveness(const IrFrameStats& frame,
                                const FaceDetection& det,
                                const Embedding& emb,
                                FaceSessionAccumulator* acc,
                                bool enforceTemporalReplayCheck) {
    LivenessReport report;
    if (!det.detected || frame.upright10.empty()) {
        report.status = LivenessStatus::NEED_MORE_FRAMES;
        return report;
    }

    const unsigned x0 = static_cast<unsigned>(
        std::clamp<int>(static_cast<int>(det.box.x), 0, static_cast<int>(frame.width) - 1));
    const unsigned y0 = static_cast<unsigned>(
        std::clamp<int>(static_cast<int>(det.box.y), 0, static_cast<int>(frame.height) - 1));
    const unsigned x1 = static_cast<unsigned>(std::clamp<int>(
        static_cast<int>(det.box.x + det.box.w), static_cast<int>(x0) + 1, static_cast<int>(frame.width)));
    const unsigned y1 = static_cast<unsigned>(std::clamp<int>(
        static_cast<int>(det.box.y + det.box.h), static_cast<int>(y0) + 1, static_cast<int>(frame.height)));

    // Layer 1: 850nm emissive display rejection using raw 10-bit upright10 inside face ROI.
    std::array<uint32_t, 1024> hist{};
    uint64_t faceCount = 0;
    uint64_t faceSat = 0;
    for (unsigned y = y0; y < y1; ++y) {
        const uint16_t* row = frame.upright10.data() + static_cast<size_t>(y) * frame.width;
        for (unsigned x = x0; x < x1; ++x) {
            const uint16_t v = row[x];
            hist[v]++;
            if (v >= 1005u) ++faceSat;
            ++faceCount;
        }
    }
    auto qval = [&](double q) -> uint16_t {
        if (faceCount == 0) return 0;
        const uint64_t target =
            static_cast<uint64_t>(std::floor(q * static_cast<double>(faceCount - 1)));
        uint64_t c = 0;
        for (size_t b = 0; b < 1024; ++b) {
            c += hist[b];
            if (c > target) return static_cast<uint16_t>(b);
        }
        return 1023u;
    };
    report.faceP05 = qval(0.05);
    report.faceP95 = qval(0.95);
    const float satRatio =
        faceCount > 0 ? static_cast<float>(faceSat) / static_cast<float>(faceCount) : 0.0f;

    if (report.faceP95 < 42u || (report.faceP95 - report.faceP05) < 28u) {
        report.status = LivenessStatus::REJECT_SCREEN_EMISSION;
        return report;
    }
    if (satRatio > 0.30f) {
        report.status = LivenessStatus::REJECT_SPECULAR_GLARE;
        return report;
    }

    // Layer 2: 3D Lambertian convexity (inner nose/T-zone vs inner lateral cheeks inside the oval)
    // plus halftone/paper texture rejection. Avoiding box corners prevents dark background from
    // deflating rimMean on flat oval cutouts.
    const unsigned bw = x1 - x0;
    const unsigned bh = y1 - y0;
    const unsigned cx0 = x0 + (bw * 44u) / 100u;
    const unsigned cx1 = std::max(cx0 + 1u, x0 + (bw * 56u) / 100u);
    const unsigned cy0 = y0 + (bh * 40u) / 100u;
    const unsigned cy1 = std::max(cy0 + 1u, y0 + (bh * 56u) / 100u);

    const unsigned lc0 = x0 + (bw * 30u) / 100u;
    const unsigned lc1 = std::max(lc0 + 1u, x0 + (bw * 39u) / 100u);
    const unsigned rc0 = x0 + (bw * 61u) / 100u;
    const unsigned rc1 = std::max(rc0 + 1u, x0 + (bw * 70u) / 100u);
    const unsigned ky0 = y0 + (bh * 46u) / 100u;
    const unsigned ky1 = std::max(ky0 + 1u, y0 + (bh * 62u) / 100u);

    uint64_t centerSum = 0, centerCnt = 0;
    uint64_t rimSum = 0, rimCnt = 0;
    for (unsigned y = y0; y < y1; ++y) {
        const uint16_t* row = frame.upright10.data() + static_cast<size_t>(y) * frame.width;
        if (y >= cy0 && y < cy1) {
            for (unsigned x = cx0; x < cx1; ++x) {
                centerSum += row[x];
                ++centerCnt;
            }
        }
        if (y >= ky0 && y < ky1) {
            for (unsigned x = lc0; x < lc1; ++x) {
                rimSum += row[x];
                ++rimCnt;
            }
            for (unsigned x = rc0; x < rc1; ++x) {
                rimSum += row[x];
                ++rimCnt;
            }
        }
    }
    const float centerMean =
        centerCnt > 0 ? static_cast<float>(centerSum) / static_cast<float>(centerCnt) : 0.0f;
    const float rimMean =
        rimCnt > 0 ? static_cast<float>(rimSum) / static_cast<float>(rimCnt) : 1.0f;
    report.centerToRimRatio = centerMean / std::max(1.0f, rimMean);

    uint64_t gradEnergy = 0;
    uint64_t lapEnergy = 0;
    const unsigned tx0 = std::max(1u, x0 + ((x1 - x0) * 20u) / 100u);
    const unsigned tx1 = std::min(frame.width - 1u, x0 + ((x1 - x0) * 80u) / 100u);
    const unsigned ty0 = std::max(1u, y0 + ((y1 - y0) * 20u) / 100u);
    const unsigned ty1 = std::min(frame.height - 1u, y0 + ((y1 - y0) * 80u) / 100u);
    for (unsigned y = ty0; y < ty1; ++y) {
        const uint16_t* row = frame.upright10.data() + static_cast<size_t>(y) * frame.width;
        const uint16_t* up = row - frame.width;
        const uint16_t* dn = row + frame.width;
        for (unsigned x = tx0; x < tx1; ++x) {
            const int gx = static_cast<int>(row[x + 1]) - static_cast<int>(row[x - 1]);
            const int gy = static_cast<int>(dn[x]) - static_cast<int>(up[x]);
            const int lap =
                4 * static_cast<int>(row[x]) - row[x - 1] - row[x + 1] - up[x] - dn[x];
            gradEnergy += static_cast<uint64_t>(std::abs(gx) + std::abs(gy));
            lapEnergy += static_cast<uint64_t>(std::abs(lap));
        }
    }
    report.halftoneRatio =
        static_cast<float>(lapEnergy) / static_cast<float>(gradEnergy + 1000ull);

    if (report.centerToRimRatio < 1.08f) {
        report.status = LivenessStatus::REJECT_FLAT_OR_PRINT;
        return report;
    }
    if (report.halftoneRatio > 1.25f) {
        report.status = LivenessStatus::REJECT_HALFTONE_PATTERN;
        return report;
    }

    // Layer 3: Temporal continuity, frozen-frame replay, and motion-blur rejection.
    if (acc == nullptr) {
        report.live = true;
        report.status = LivenessStatus::PASS;
        return report;
    }

    std::array<uint8_t, 32 * 32> curPatch{};
    buildNormalized32x32FacePatch(frame, det.box, curPatch);

    if (acc->hasPrevPatch) {
        uint64_t madSum = 0;
        for (size_t i = 0; i < curPatch.size(); ++i) {
            madSum += static_cast<uint64_t>(
                std::abs(static_cast<int>(curPatch[i]) - static_cast<int>(acc->prevPatch[i])));
        }
        report.temporalMad = static_cast<float>(madSum) / static_cast<float>(curPatch.size());
        if (report.temporalMad > 28.0f) {
            acc->consecutiveLiveFrames = 0;
            acc->prevPatch = curPatch;
            acc->hasPrevEmbedding = false;
            report.status = LivenessStatus::REJECT_MOTION_BLUR;
            return report;
        }
        if (enforceTemporalReplayCheck && report.temporalMad < 0.03f) {
            acc->consecutiveLiveFrames = 0;
            report.status = LivenessStatus::REJECT_FROZEN_REPLAY;
            return report;
        }
    }

    if (acc->hasPrevEmbedding) {
        const float cons = cosineSimilarity(acc->prevEmbedding, emb);
        if (cons < kDefaultLiveConsistencyThreshold) {
            acc->consecutiveLiveFrames = 1;
            acc->prevPatch = curPatch;
            acc->hasPrevPatch = true;
            acc->prevEmbedding = emb;
            acc->frameCount++;
            report.status = LivenessStatus::REJECT_UNSTABLE_IDENTITY;
            return report;
        }
    }

    acc->prevPatch = curPatch;
    acc->hasPrevPatch = true;
    acc->prevEmbedding = emb;
    acc->hasPrevEmbedding = true;
    acc->frameCount++;
    acc->consecutiveLiveFrames++;

    if (acc->consecutiveLiveFrames >= acc->minConsecutiveFrames) {
        report.live = true;
        report.status = LivenessStatus::PASS;
    } else {
        report.live = false;
        report.status = LivenessStatus::NEED_MORE_FRAMES;
    }
    return report;
}

SpxLockoutTracker::SpxLockoutTracker(ClockFn clockMs)
    : clockMs_(clockMs ? std::move(clockMs) : defaultBootTimeMs) {}

void SpxLockoutTracker::setFailedAttempts(uint32_t count) {
    failedAttempts_ = count;
    if (failedAttempts_ < 5u) {
        timedUntilMs_ = 0;
    }
}

LockoutState SpxLockoutTracker::checkState() const {
    LockoutState st;
    st.failedAttempts = failedAttempts_;
    if (failedAttempts_ >= 20u) {
        st.mode = LockoutMode::PERMANENT;
        return st;
    }
    if (timedUntilMs_ > 0) {
        const int64_t now = clockMs_();
        if (now < timedUntilMs_) {
            st.mode = LockoutMode::TIMED;
            st.remainingMs = timedUntilMs_ - now;
            return st;
        }
    }
    st.mode = LockoutMode::NONE;
    return st;
}

LockoutState SpxLockoutTracker::addFailedAttempt() {
    ++failedAttempts_;
    if (failedAttempts_ >= 20u) {
        timedUntilMs_ = 0;
        return {LockoutMode::PERMANENT, failedAttempts_, 0};
    }
    if ((failedAttempts_ % 5u) == 0u) {
        constexpr int64_t kTimedLockoutDurationMs = 30000LL;
        timedUntilMs_ = clockMs_() + kTimedLockoutDurationMs;
        return {LockoutMode::TIMED, failedAttempts_, kTimedLockoutDurationMs};
    }
    return checkState();
}

void SpxLockoutTracker::reset() {
    failedAttempts_ = 0;
    timedUntilMs_ = 0;
}

SpxFaceStore::SpxFaceStore(int32_t userId, std::string baseDirOverride) : userId_(userId) {
    if (!baseDirOverride.empty()) {
        dirPath_ = std::move(baseDirOverride);
    } else {
        dirPath_ = "/data/vendor_de/" + std::to_string(userId) + "/facedata";
    }
    dataPath_ = dirPath_ + "/spx_face_v1.bin";
    keyPath_ = dirPath_ + "/spx_face_key_v1.bin";
}

SpxFaceStore::~SpxFaceStore() {
    secureZero(masterKey_.data(), masterKey_.size());
    for (auto& rec : enrollments_) {
        secureZero(&rec, sizeof(rec));
    }
}

bool SpxFaceStore::ensureKeyLoadedOrCreated(bool createIfMissing) {
    if (keyLoaded_) return true;
    std::vector<uint8_t> raw;
    if (readExactFile(keyPath_, raw, 64)) {
        if (raw.size() != 32) {
            secureZero(raw.data(), raw.size());
            return false;
        }
        std::memcpy(masterKey_.data(), raw.data(), 32);
        secureZero(raw.data(), raw.size());
        keyLoaded_ = true;
        return true;
    }
    if (!createIfMissing) return false;
    if (!mkdirRecursive(dirPath_, 0700)) return false;
    if (!csprngBytes(masterKey_.data(), masterKey_.size())) return false;
    if (!writeFileAtomic(keyPath_, masterKey_.data(), masterKey_.size(), 0600)) {
        secureZero(masterKey_.data(), masterKey_.size());
        return false;
    }
    keyLoaded_ = true;
    return true;
}

bool SpxFaceStore::load() {
    enrollments_.clear();
    authenticatorId_ = 0;
    featureMask_ = 0x03u;
    failedAttempts_ = 0;
    nextEnrollmentId_ = 1;

    std::vector<uint8_t> blob;
    if (access(dataPath_.c_str(), F_OK) != 0 && errno == ENOENT) {
        return true;
    }
    if (!readExactFile(dataPath_, blob, 1024u * 1024u)) {
        return false;
    }
    if (!ensureKeyLoadedOrCreated(false)) return false;

    // Header: magic(8) + version(4) + userId(4) + authId(8) + featureMask(4) +
    //         failedAttempts(4) + count(4) + nextEnrollmentId(4) + nonce(12) = 52B, + mac(32B)
    constexpr size_t kHeaderLen = 52;
    constexpr size_t kMacLen = 32;
    if (blob.size() < kHeaderLen + kMacLen) return false;
    if (std::memcmp(blob.data(), kStoreMagic, sizeof(kStoreMagic)) != 0) return false;

    const size_t bodyEnd = blob.size() - kMacLen;
    const auto macKey = hkdfSha256(nullptr, 0, masterKey_.data(), masterKey_.size(),
                                   reinterpret_cast<const uint8_t*>("spx_face_mac_v1"), 15);
    const auto encKey = hkdfSha256(nullptr, 0, masterKey_.data(), masterKey_.size(),
                                   reinterpret_cast<const uint8_t*>("spx_face_enc_v1"), 15);
    const auto expectedMac = hmacSha256(macKey.data(), macKey.size(), blob.data(), bodyEnd);
    const bool macOk = constantTimeEqual(expectedMac.data(), blob.data() + bodyEnd, kMacLen);
    if (!macOk) return false;

    size_t off = sizeof(kStoreMagic);
    uint32_t version = 0, count = 0;
    int32_t storedUser = 0;
    int32_t storedNextId = 1;
    std::array<uint8_t, 12> nonce{};
    if (!readPod(blob.data(), bodyEnd, off, version) ||
        !readPod(blob.data(), bodyEnd, off, storedUser) ||
        !readPod(blob.data(), bodyEnd, off, authenticatorId_) ||
        !readPod(blob.data(), bodyEnd, off, featureMask_) ||
        !readPod(blob.data(), bodyEnd, off, failedAttempts_) ||
        !readPod(blob.data(), bodyEnd, off, count) ||
        !readPod(blob.data(), bodyEnd, off, storedNextId)) {
        return false;
    }
    if (version != kStoreVersion || storedUser != userId_ || count > kMaxEnrollmentsPerUser) {
        return false;
    }
    std::memcpy(nonce.data(), blob.data() + off, nonce.size());
    off += nonce.size();

    const size_t cipherLen = bodyEnd - off;
    const size_t expectedPlain = static_cast<size_t>(count) * sizeof(EnrollmentRecord);
    if (cipherLen != expectedPlain) return false;

    std::vector<uint8_t> plain(cipherLen);
    if (cipherLen > 0) {
        chacha20Xor(encKey, nonce, 1u, blob.data() + off, plain.data(), cipherLen);
    }
    enrollments_.resize(count);
    for (size_t i = 0; i < count; ++i) {
        std::memcpy(&enrollments_[i], plain.data() + i * sizeof(EnrollmentRecord),
                    sizeof(EnrollmentRecord));
        if (enrollments_[i].sampleCount == 0 ||
            enrollments_[i].sampleCount > kSamplesPerEnrollment) {
            secureZero(plain.data(), plain.size());
            enrollments_.clear();
            return false;
        }
    }
    nextEnrollmentId_ = std::max(1, storedNextId);
    secureZero(plain.data(), plain.size());
    return true;
}

bool SpxFaceStore::save() {
    if (!mkdirRecursive(dirPath_, 0700)) return false;
    if (!ensureKeyLoadedOrCreated(true)) return false;

    std::array<uint8_t, 12> nonce{};
    if (!csprngBytes(nonce.data(), nonce.size())) return false;

    const uint32_t count = static_cast<uint32_t>(enrollments_.size());
    std::vector<uint8_t> plain(static_cast<size_t>(count) * sizeof(EnrollmentRecord));
    for (size_t i = 0; i < count; ++i) {
        std::memcpy(plain.data() + i * sizeof(EnrollmentRecord), &enrollments_[i],
                    sizeof(EnrollmentRecord));
    }

    const auto macKey = hkdfSha256(nullptr, 0, masterKey_.data(), masterKey_.size(),
                                   reinterpret_cast<const uint8_t*>("spx_face_mac_v1"), 15);
    const auto encKey = hkdfSha256(nullptr, 0, masterKey_.data(), masterKey_.size(),
                                   reinterpret_cast<const uint8_t*>("spx_face_enc_v1"), 15);

    std::vector<uint8_t> out;
    out.reserve(52 + plain.size() + 32);
    out.insert(out.end(), kStoreMagic, kStoreMagic + sizeof(kStoreMagic));
    appendPod(out, kStoreVersion);
    appendPod(out, userId_);
    appendPod(out, authenticatorId_);
    appendPod(out, featureMask_);
    appendPod(out, failedAttempts_);
    appendPod(out, count);
    appendPod(out, nextEnrollmentId_);
    out.insert(out.end(), nonce.begin(), nonce.end());

    if (!plain.empty()) {
        std::vector<uint8_t> cipher(plain.size());
        chacha20Xor(encKey, nonce, 1u, plain.data(), cipher.data(), plain.size());
        out.insert(out.end(), cipher.begin(), cipher.end());
    }
    secureZero(plain.data(), plain.size());

    const auto mac = hmacSha256(macKey.data(), macKey.size(), out.data(), out.size());
    out.insert(out.end(), mac.begin(), mac.end());

    return writeFileAtomic(dataPath_, out.data(), out.size(), 0600);
}

int64_t SpxFaceStore::invalidateAuthenticatorId() {
    if (enrollments_.empty()) {
        authenticatorId_ = 0;
    } else {
        authenticatorId_ = csprngInt64NonZero();
    }
    save();
    return authenticatorId_;
}

void SpxFaceStore::setFeatureBit(uint32_t bit, bool enabled) {
    if (enabled) {
        featureMask_ |= bit;
    } else {
        featureMask_ &= ~bit;
    }
    save();
}

bool SpxFaceStore::hasFeatureBit(uint32_t bit) const {
    return (featureMask_ & bit) != 0;
}

void SpxFaceStore::setFailedAttempts(uint32_t count) {
    if (failedAttempts_ != count) {
        failedAttempts_ = count;
        save();
    }
}

std::vector<int32_t> SpxFaceStore::listEnrollmentIds() const {
    std::vector<int32_t> ids;
    ids.reserve(enrollments_.size());
    for (const auto& e : enrollments_) ids.push_back(e.enrollmentId);
    return ids;
}

int32_t SpxFaceStore::addEnrollment(const std::vector<Embedding>& samples) {
    if (samples.empty() || enrollments_.size() >= kMaxEnrollmentsPerUser) return -1;
    EnrollmentRecord rec;
    rec.enrollmentId = nextEnrollmentId_++;
    while (rec.enrollmentId <= 0) rec.enrollmentId = nextEnrollmentId_++;
    rec.sampleCount = static_cast<uint32_t>(std::min(samples.size(), kSamplesPerEnrollment));
    for (uint32_t i = 0; i < rec.sampleCount; ++i) {
        rec.samples[i] = samples[i];
    }
    enrollments_.push_back(rec);
    if (authenticatorId_ == 0) {
        authenticatorId_ = csprngInt64NonZero();
    }
    if (!save()) {
        enrollments_.pop_back();
        return -1;
    }
    return rec.enrollmentId;
}

std::vector<int32_t> SpxFaceStore::removeEnrollments(const std::vector<int32_t>& ids) {
    std::vector<int32_t> removed;
    for (auto it = enrollments_.begin(); it != enrollments_.end();) {
        if (std::find(ids.begin(), ids.end(), it->enrollmentId) != ids.end()) {
            removed.push_back(it->enrollmentId);
            secureZero(&(*it), sizeof(*it));
            it = enrollments_.erase(it);
        } else {
            ++it;
        }
    }
    if (enrollments_.empty()) {
        authenticatorId_ = 0;
    }
    save();
    return removed;
}

void SpxFaceStore::wipeAll() {
    for (auto& rec : enrollments_) secureZero(&rec, sizeof(rec));
    enrollments_.clear();
    authenticatorId_ = 0;
    failedAttempts_ = 0;
    secureZero(masterKey_.data(), masterKey_.size());
    keyLoaded_ = false;
    unlink(dataPath_.c_str());
    unlink(keyPath_.c_str());
}

MatchResult SpxFaceStore::match(const Embedding& probe, float threshold) const {
    MatchResult best;
    for (const auto& rec : enrollments_) {
        for (uint32_t i = 0; i < rec.sampleCount; ++i) {
            const float sim = cosineSimilarity(probe, rec.samples[i]);
            if (sim > best.bestSimilarity) {
                best.bestSimilarity = sim;
                if (sim >= threshold) {
                    best.matched = true;
                    best.enrollmentId = rec.enrollmentId;
                }
            }
        }
    }
    return best;
}

}  // namespace spx::face
