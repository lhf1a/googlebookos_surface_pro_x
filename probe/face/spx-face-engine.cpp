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
constexpr uint32_t kStoreVersion = 3u;
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
                             float sigmaScale);

Point2f findEyeWell(const uint8_t* gray,
                    unsigned width,
                    unsigned height,
                    int x0,
                    int y0,
                    int x1,
                    int y1,
                    float priorX,
                    float priorY,
                    float boxW) {
    if (width < 4 || height < 4) return {priorX, priorY};
    x0 = std::clamp(x0, 1, static_cast<int>(width) - 2);
    y0 = std::clamp(y0, 1, static_cast<int>(height) - 2);
    x1 = std::clamp(x1, x0, static_cast<int>(width) - 2);
    y1 = std::clamp(y1, y0, static_cast<int>(height) - 2);

    const float sigma = std::max(4.5f, 0.115f * boxW);
    const int rInX = std::max(1, static_cast<int>(std::lround(0.055f * boxW)));
    const int rInY = std::max(1, static_cast<int>(std::lround(0.041f * boxW)));
    const int rOutX = std::max(rInX + 2, static_cast<int>(std::lround(0.145f * boxW)));
    const int rOutY = std::max(rInY + 2, static_cast<int>(std::lround(0.123f * boxW)));

    const int rx0 = std::clamp(x0 - rOutX, 0, static_cast<int>(width) - 1);
    const int ry0 = std::clamp(y0 - rInY, 0, static_cast<int>(height) - 1);
    const int rx1 = std::clamp(x1 + rOutX, rx0, static_cast<int>(width) - 1);
    const int ry1 = std::clamp(y1 + rOutY, ry0, static_cast<int>(height) - 1);
    const unsigned roiW = static_cast<unsigned>(rx1 - rx0 + 1);
    const unsigned roiH = static_cast<unsigned>(ry1 - ry0 + 1);
    std::vector<uint8_t> roiGray(static_cast<size_t>(roiW) * roiH);
    for (unsigned ry = 0; ry < roiH; ++ry) {
        std::copy_n(gray + static_cast<size_t>(ry0 + static_cast<int>(ry)) * width + rx0,
                    roiW,
                    roiGray.data() + static_cast<size_t>(ry) * roiW);
    }
    IntegralImage roiIi;
    roiIi.build(roiGray.data(), roiW, roiH);

    double sumX = 0.0;
    double sumY = 0.0;
    double weightSum = 0.0;

    for (int y = y0; y <= y1; ++y) {
        const float dy = (static_cast<float>(y) - priorY) / sigma;
        const int ly = y - ry0;
        for (int x = x0; x <= x1; ++x) {
            const float dx = (static_cast<float>(x) - priorX) / sigma;
            const float spatialPrior = std::exp(-0.5f * (dx * dx + dy * dy));
            if (spatialPrior < 0.02f) continue;
            const int lx = x - rx0;

            const float innerMean =
                roiIi.rectMean(lx - rInX, ly - rInY, lx + rInX + 1, ly + rInY + 1);
            const float leftMean =
                roiIi.rectMean(lx - rOutX, ly - rInY, lx - rInX, ly + rInY + 1);
            const float rightMean =
                roiIi.rectMean(lx + rInX + 1, ly - rInY, lx + rOutX + 1, ly + rInY + 1);
            const float belowMean =
                roiIi.rectMean(lx - rInX, ly + rInY + 1, lx + rInX + 1, ly + rOutY + 1);

            // Bangs-safe & inner-canthus-safe eye-well contrast (sides + under-eye cheek).
            const float horizSurr = 0.5f * (leftMean + rightMean);
            const float minSideSurr = std::min(leftMean, rightMean);
            const float skinSurr =
                0.68f * minSideSurr + 0.10f * horizSurr + 0.22f * belowMean;
            const float wellContrast = std::max(0.0f, skinSurr - innerMean);
            if (wellContrast <= 0.5f) continue;

            const float w =
                spatialPrior * wellContrast * wellContrast *
                (1.0f + std::max(0.0f, 160.0f - innerMean) / 160.0f);
            sumX += static_cast<double>(x) * w;
            sumY += static_cast<double>(y) * w;
            weightSum += w;
        }
    }

    if (weightSum > 1e-3) {
        return {
            static_cast<float>(sumX / weightSum),
            static_cast<float>(sumY / weightSum),
        };
    }
    return findWeightedMinPatch(
        gray, width, height, x0, y0, x1, y1, priorX, priorY, 0.09f * boxW);
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

void removeLinearPlaneInner(std::array<float, kAlignedSize * kAlignedSize>& img) {
    static_assert(kAlignedSize == 112, "inner-face detrend assumes 112x112 aligned patch");
    double s1 = 0.0, su = 0.0, sv = 0.0;
    double su2 = 0.0, sv2 = 0.0, suv = 0.0;
    double y1 = 0.0, yu = 0.0, yv = 0.0;
    for (int y = 24; y < 100; ++y) {
        const double v = (y - 62.0) / 38.0;
        for (int x = 20; x < 92; ++x) {
            const double u = (x - 56.0) / 36.0;
            const double val = img[static_cast<size_t>(y) * kAlignedSize + x];
            s1 += 1.0;
            su += u;
            sv += v;
            su2 += u * u;
            sv2 += v * v;
            suv += u * v;
            y1 += val;
            yu += val * u;
            yv += val * v;
        }
    }
    double a[3][3] = {
        {s1, su, sv},
        {su, su2, suv},
        {sv, suv, sv2},
    };
    double rhs[3] = {y1, yu, yv};
    for (int i = 0; i < 3; ++i) {
        const double piv = a[i][i];
        if (std::abs(piv) < 1e-12) return;
        for (int j = i; j < 3; ++j) a[i][j] /= piv;
        rhs[i] /= piv;
        for (int k = 0; k < 3; ++k) {
            if (k == i) continue;
            const double factor = a[k][i];
            for (int j = i; j < 3; ++j) a[k][j] -= factor * a[i][j];
            rhs[k] -= factor * rhs[i];
        }
    }
    for (int y = 0; y < kAlignedSize; ++y) {
        const double v = (y - 62.0) / 38.0;
        for (int x = 0; x < kAlignedSize; ++x) {
            const double u = (x - 56.0) / 36.0;
            img[static_cast<size_t>(y) * kAlignedSize + x] -=
                static_cast<float>(rhs[0] + rhs[1] * u + rhs[2] * v);
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

std::vector<FaceDetection> detectFaceCandidatesNir(const IrFrameStats& frame,
                                                   size_t maxCandidates,
                                                   FaceQuality* outEmptyQuality) {
    auto setEmpty = [&](FaceQuality q) -> std::vector<FaceDetection> {
        if (outEmptyQuality) *outEmptyQuality = q;
        return {};
    };
    if (maxCandidates == 0 || frame.width < 32 || frame.height < 32 ||
        frame.gray8.size() < static_cast<size_t>(frame.width) * frame.height) {
        return setEmpty(FaceQuality::NOT_DETECTED);
    }

    if (frame.p99 < 24 && frame.darkRatio > 0.90f) {
        return setEmpty(FaceQuality::TOO_DARK);
    }
    if (frame.saturatedRatio > 0.70f) {
        return setEmpty(FaceQuality::TOO_BRIGHT);
    }

    // Downsample 2x when frame >= 320px to keep multi-scale sliding-window scan < 4ms.
    // Map raw 10-bit upright10 through a gamma-0.5 LUT so distant/dim faces retain midtone
    // contrast even when background/glare pushes p99 near 1023.
    const unsigned scale = (frame.width >= 320 && frame.height >= 320) ? 2u : 1u;
    const unsigned dw = frame.width / scale;
    const unsigned dh = frame.height / scale;
    std::vector<uint8_t> small(static_cast<size_t>(dw) * dh);
    const size_t fullPixels = static_cast<size_t>(frame.width) * frame.height;
    if (frame.upright10.size() >= fullPixels) {
        std::array<uint8_t, 1024> sqrtLut{};
        const uint16_t lo10 = frame.p1;
        const uint32_t rawSpan10 =
            (frame.p99 > lo10) ? static_cast<uint32_t>(frame.p99 - lo10) : 0u;
        const float invSpan10 =
            1.0f / static_cast<float>(std::max<uint32_t>(64u, rawSpan10));
        for (unsigned v = 0; v < 1024u; ++v) {
            const float t = (v <= lo10)
                                ? 0.0f
                                : std::min(1.0f, static_cast<float>(v - lo10) * invSpan10);
            sqrtLut[v] = static_cast<uint8_t>(std::lround(std::sqrt(t) * 255.0f));
        }
        if (scale == 2u) {
            for (unsigned y = 0; y < dh; ++y) {
                const uint16_t* r0 =
                    frame.upright10.data() + static_cast<size_t>(y * 2u) * frame.width;
                const uint16_t* r1 = r0 + frame.width;
                uint8_t* d = small.data() + static_cast<size_t>(y) * dw;
                for (unsigned x = 0; x < dw; ++x) {
                    const unsigned avg =
                        (r0[x * 2u] + r0[x * 2u + 1u] + r1[x * 2u] + r1[x * 2u + 1u]) >> 2;
                    d[x] = sqrtLut[std::min(avg, 1023u)];
                }
            }
        } else {
            for (size_t i = 0; i < small.size(); ++i) {
                small[i] = sqrtLut[std::min<unsigned>(frame.upright10[i], 1023u)];
            }
        }
    } else if (scale == 2u) {
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
    const int minSize = std::max(20, static_cast<int>(std::lround(minDim * 0.13f)));
    const int maxSize = std::min<int>(minDim - 4, static_cast<int>(std::lround(minDim * 0.88f)));

    struct Candidate {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        float rawScore = -1e9f;
        float rankScore = -1e9f;
    };

    auto evalWindow = [&](int bx, int by, int bw, int bh, float& outRaw, float& outRank) -> bool {
        if (bx < 1 || by < 1 || bx + bw >= static_cast<int>(dw) ||
            by + bh >= static_cast<int>(dh)) {
            return false;
        }
        const float coreStd =
            ii.rectStdDev(bx + bw / 6, by + bh / 6, bx + (5 * bw) / 6, by + (5 * bh) / 6);
        if (coreStd < 7.0f) return false;

        const float coreMean =
            ii.rectMean(bx + bw / 5, by + bh / 5, bx + (4 * bw) / 5, by + (4 * bh) / 5);
        const float eyeBand = ii.rectMean(
            bx + (14 * bw) / 100, by + (22 * bh) / 100, bx + (86 * bw) / 100, by + (42 * bh) / 100);
        const float cheekBand = ii.rectMean(
            bx + (16 * bw) / 100, by + (44 * bh) / 100, bx + (84 * bw) / 100, by + (65 * bh) / 100);
        if (cheekBand <= eyeBand + 1.5f) return false;

        const float leftEye = ii.rectMean(
            bx + (16 * bw) / 100, by + (22 * bh) / 100, bx + (40 * bw) / 100, by + (42 * bh) / 100);
        const float rightEye = ii.rectMean(
            bx + (60 * bw) / 100, by + (22 * bh) / 100, bx + (84 * bw) / 100, by + (42 * bh) / 100);
        // Per-side cheek > eye checks below enforce socket darkness locally; avoid cross-side
        // cheekBand vs single-eye rejection so strong side/bottom-left IR lighting (ROTATION_90)
        // does not drop real faces whose lit-side eye exceeds the cross-face average cheek band.
        if (std::abs(leftEye - rightEye) >= 72.0f) return false;

        const float leftCheek = ii.rectMean(
            bx + (14 * bw) / 100, by + (44 * bh) / 100, bx + (42 * bw) / 100, by + (65 * bh) / 100);
        const float rightCheek = ii.rectMean(
            bx + (58 * bw) / 100, by + (44 * bh) / 100, bx + (86 * bw) / 100, by + (65 * bh) / 100);
        if (leftCheek <= leftEye + 2.0f || rightCheek <= rightEye + 2.0f) return false;
        if (std::abs(leftCheek - rightCheek) > 74.0f) return false;

        const float leftBrow = ii.rectMean(
            bx + (16 * bw) / 100, by + (8 * bh) / 100, bx + (40 * bw) / 100, by + (22 * bh) / 100);
        const float rightBrow = ii.rectMean(
            bx + (60 * bw) / 100, by + (8 * bh) / 100, bx + (84 * bw) / 100, by + (22 * bh) / 100);
        const float forehead = ii.rectMean(
            bx + (24 * bw) / 100, by + (8 * bh) / 100, bx + (76 * bw) / 100, by + (22 * bh) / 100);
        const float upperNose = ii.rectMean(
            bx + (41 * bw) / 100, by + (22 * bh) / 100, bx + (59 * bw) / 100, by + (42 * bh) / 100);
        const float noseBridge = ii.rectMean(
            bx + (41 * bw) / 100, by + (24 * bh) / 100, bx + (59 * bw) / 100, by + (56 * bh) / 100);
        const float mouthBand = ii.rectMean(
            bx + (26 * bw) / 100, by + (67 * bh) / 100, bx + (74 * bw) / 100, by + (83 * bh) / 100);
        const float chinBand = ii.rectMean(
            bx + (30 * bw) / 100, by + (84 * bh) / 100, bx + (70 * bw) / 100, by + (96 * bh) / 100);
        if (chinBand < 0.25f * cheekBand) return false;

        const float eyeMean = 0.5f * (leftEye + rightEye);

        // Local-minimum eye sockets: brow above eye sockets and nasal bridge between eyes
        // must exceed the eye sockets (rejects oval crowns, vignettes, and 90/180 rotations).
        // Under bottom-left IR lighting (ROTATION_90 power-at-top), one upper brow can dim slightly
        // relative to its lit-side socket, so require the stronger brow > +1.5 and the weaker >= -3.5.
        const float leftBrowDelta = leftBrow - leftEye;
        const float rightBrowDelta = rightBrow - rightEye;
        if (std::max(leftBrowDelta, rightBrowDelta) <= 1.5f ||
            std::min(leftBrowDelta, rightBrowDelta) <= -3.5f) {
            return false;
        }
        if (upperNose <= leftEye + 1.5f || upperNose <= rightEye + 1.5f) return false;
        if (noseBridge <= leftEye + 2.0f || noseBridge <= rightEye + 2.0f) return false;

        // Upright vs 180-deg inverted head guard (tolerates bangs & light 850nm lips).
        const int vPad = std::max(4, bh / 5);
        const float aboveBox = (by >= 6)
                                   ? ii.rectMean(bx + (24 * bw) / 100,
                                                 std::max(0, by - vPad),
                                                 bx + (76 * bw) / 100,
                                                 by)
                                   : forehead;
        const float belowBox = (by + bh + 6 <= static_cast<int>(dh))
                                   ? ii.rectMean(bx + (24 * bw) / 100,
                                                 by + bh,
                                                 bx + (76 * bw) / 100,
                                                 std::min<int>(dh, by + bh + vPad))
                                   : chinBand;
        if (aboveBox > cheekBand + 8.0f) return false;
        if (aboveBox > belowBox + 70.0f && aboveBox > cheekBand - 45.0f) return false;

        // Outer surround contrast under active 850nm illumination.
        const int pad = std::max(4, bw / 5);
        const float leftSurr =
            (bx >= 6) ? ii.rectMean(std::max(0, bx - pad), by, bx, by + bh) : coreMean;
        const float rightSurr = (bx + bw + 6 <= static_cast<int>(dw))
                                    ? ii.rectMean(bx + bw, by, std::min<int>(dw, bx + bw + pad), by + bh)
                                    : coreMean;
        const float surrMean = 0.5f * (leftSurr + rightSurr);
        if (coreMean < std::min(leftSurr, rightSurr) - 10.0f) return false;

        const float sym = sampleSubblockSymmetry(ii, bx, by, bw, bh);
        if (sym < 0.28f) return false;

        const float localGain = std::clamp(32.0f / std::max(coreStd, 10.0f), 0.75f, 1.85f);
        const float structureScore =
            ((cheekBand - eyeBand) * 1.3f +
             (noseBridge - eyeMean) * 1.4f +
             std::max(0.0f, forehead - eyeBand) * 0.7f +
             std::max(0.0f, cheekBand - mouthBand) * 0.5f) *
            localGain;
        if (structureScore < 20.0f) return false;

        // Favor realistic face scales [0.28..0.58] of minDim; gently taper both tiny sub-boxes
        // (< 0.28) and giant head+background wrappers (> 0.58, e.g. 312..348px on 480x640).
        const float relW = static_cast<float>(bw) / static_cast<float>(minDim);
        float scaleWeight = 1.0f;
        if (relW < 0.28f) {
            scaleWeight = 0.80f + 0.20f * (relW / 0.28f);
        } else if (relW > 0.58f) {
            scaleWeight = std::max(0.72f, 1.0f - 0.85f * (relW - 0.58f));
        }
        outRaw = structureScore +
                 std::clamp((coreMean - surrMean) * localGain, -15.0f, 40.0f) * 0.55f +
                 sym * 30.0f;
        outRank = outRaw * scaleWeight;
        return true;
    };

    auto boxIoU = [](const Candidate& a, const Candidate& b) -> float {
        const int ix0 = std::max(a.x, b.x);
        const int iy0 = std::max(a.y, b.y);
        const int ix1 = std::min(a.x + a.w, b.x + b.w);
        const int iy1 = std::min(a.y + a.h, b.y + b.h);
        if (ix1 <= ix0 || iy1 <= iy0) return 0.0f;
        const float inter = static_cast<float>((ix1 - ix0) * (iy1 - iy0));
        const float areaA = static_cast<float>(a.w * a.h);
        const float areaB = static_cast<float>(b.w * b.h);
        const float uni = areaA + areaB - inter;
        return uni > 0.0f ? (inter / uni) : 0.0f;
    };

    auto scaleRatio = [](const Candidate& a, const Candidate& b) -> float {
        const float mn = static_cast<float>(std::max(1, std::min(a.w, b.w)));
        const float mx = static_cast<float>(std::max(a.w, b.w));
        return mx / mn;
    };

    std::vector<Candidate> pool;
    pool.reserve(128);
    for (int winW = minSize; winW <= maxSize;
         winW = std::max(winW + 3, static_cast<int>(winW * 1.12f))) {
        const int winH = std::min<int>(dh - 2, static_cast<int>(std::lround(winW * 1.15f)));
        const int step = std::max(3, winW / 12);
        for (int winY = 1; winY + winH < static_cast<int>(dh); winY += step) {
            for (int winX = 1; winX + winW < static_cast<int>(dw); winX += step) {
                float raw = 0.0f;
                float rank = 0.0f;
                if (evalWindow(winX, winY, winW, winH, raw, rank) && raw > 24.0f) {
                    pool.push_back({winX, winY, winW, winH, raw, rank});
                }
            }
        }
    }

    if (pool.empty()) {
        return setEmpty(FaceQuality::NOT_DETECTED);
    }

    std::sort(pool.begin(), pool.end(), [](const Candidate& a, const Candidate& b) {
        return a.rankScore > b.rankScore;
    });

    // Scale-aware NMS with small-box quota: suppress near-duplicates at similar scale while
    // preserving distinct scales (e.g. a real 180px face nested inside a 320px head+background box
    // or competing with multiple 62..76px background specks in Portrait under-lit scenes).
    std::vector<Candidate> seeds;
    seeds.reserve(maxCandidates);
    const float smallWidthCutoff = 0.22f * static_cast<float>(minDim);
    const size_t maxSmallPrimary = (maxCandidates >= 2u) ? (maxCandidates / 2u) : maxCandidates;
    size_t smallSelected = 0;
    auto isSuppressed = [&](const Candidate& c) -> bool {
        for (const auto& s : seeds) {
            const float iou = boxIoU(c, s);
            const float sr = scaleRatio(c, s);
            if (iou >= 0.72f || (iou >= 0.40f && sr < 1.32f)) {
                return true;
            }
        }
        return false;
    };
    for (const auto& c : pool) {
        const bool isSmall = static_cast<float>(c.w) < smallWidthCutoff;
        if (isSmall && smallSelected >= maxSmallPrimary) {
            continue;
        }
        if (!isSuppressed(c)) {
            seeds.push_back(c);
            if (isSmall) ++smallSelected;
            if (seeds.size() >= maxCandidates) break;
        }
    }
    if (seeds.size() < maxCandidates && maxCandidates >= 2u) {
        for (const auto& c : pool) {
            if (static_cast<float>(c.w) >= smallWidthCutoff) continue;
            if (!isSuppressed(c)) {
                seeds.push_back(c);
                if (seeds.size() >= maxCandidates) break;
            }
        }
    }

    std::vector<Candidate> refined;
    refined.reserve(seeds.size());
    for (const auto& seed : seeds) {
        Candidate best = seed;
        const int refinePad = std::max(3, seed.w / 12);
        for (int dwDelta : {-4, -2, 0, 2, 4}) {
            const int rw = std::clamp(seed.w + dwDelta, minSize, maxSize);
            const int rh = std::min<int>(dh - 2, static_cast<int>(std::lround(rw * 1.15f)));
            for (int ry = seed.y - refinePad; ry <= seed.y + refinePad; ++ry) {
                for (int rx = seed.x - refinePad; rx <= seed.x + refinePad; ++rx) {
                    float raw = 0.0f;
                    float rank = 0.0f;
                    if (evalWindow(rx, ry, rw, rh, raw, rank) && rank > best.rankScore) {
                        best = {rx, ry, rw, rh, raw, rank};
                    }
                }
            }
        }
        if (best.rawScore >= 38.0f) {
            refined.push_back(best);
        }
    }

    if (refined.empty()) {
        return setEmpty(FaceQuality::NOT_DETECTED);
    }

    std::sort(refined.begin(), refined.end(), [](const Candidate& a, const Candidate& b) {
        return a.rankScore > b.rankScore;
    });

    std::vector<Candidate> uniqueRefined;
    uniqueRefined.reserve(refined.size());
    for (const auto& c : refined) {
        bool dup = false;
        for (const auto& u : uniqueRefined) {
            if (boxIoU(c, u) >= 0.55f && scaleRatio(c, u) < 1.25f) {
                dup = true;
                break;
            }
        }
        if (!dup) uniqueRefined.push_back(c);
    }

    const float frameMinDim = static_cast<float>(std::min(frame.width, frame.height));
    std::vector<FaceDetection> out;
    out.reserve(uniqueRefined.size());
    for (const auto& best : uniqueRefined) {
        FaceDetection res;
        res.detected = true;
        res.score = best.rawScore;
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

        res.leftEye = findEyeWell(frame.gray8.data(),
                                  frame.width,
                                  frame.height,
                                  fx + (fw * 14) / 100,
                                  fy + (fh * 20) / 100,
                                  fx + (fw * 38) / 100,
                                  fy + (fh * 47) / 100,
                                  res.box.x + 0.30f * res.box.w,
                                  res.box.y + 0.34f * res.box.h,
                                  res.box.w);
        res.rightEye = findEyeWell(frame.gray8.data(),
                                   frame.width,
                                   frame.height,
                                   fx + (fw * 62) / 100,
                                   fy + (fh * 20) / 100,
                                   fx + (fw * 86) / 100,
                                   fy + (fh * 47) / 100,
                                   res.box.x + 0.70f * res.box.w,
                                   res.box.y + 0.34f * res.box.h,
                                   res.box.w);
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
        res.distanceCm = (11500.0f * (frameMinDim / 480.0f)) / std::max(16.0f, res.box.w);

        const float cx = res.box.x + 0.5f * res.box.w;
        const float cy = res.box.y + 0.5f * res.box.h;
        if (res.box.w < 0.125f * frameMinDim) {
            res.quality = FaceQuality::TOO_FAR;
        } else if (res.box.w > 0.88f * frameMinDim) {
            res.quality = FaceQuality::TOO_CLOSE;
        } else if (cx < 0.12f * frame.width) {
            res.quality = FaceQuality::FACE_TOO_LEFT;
        } else if (cx > 0.88f * frame.width) {
            res.quality = FaceQuality::FACE_TOO_RIGHT;
        } else if (cy < 0.10f * frame.height) {
            res.quality = FaceQuality::FACE_TOO_HIGH;
        } else if (cy > 0.90f * frame.height) {
            res.quality = FaceQuality::FACE_TOO_LOW;
        } else if (std::abs(res.panDeg) > 35.0f) {
            res.quality = FaceQuality::PAN_TOO_EXTREME;
        } else if (std::abs(res.tiltDeg) > 30.0f) {
            res.quality = FaceQuality::TILT_TOO_EXTREME;
        } else {
            res.quality = FaceQuality::GOOD;
        }
        out.push_back(res);
    }
    if (outEmptyQuality) *outEmptyQuality = FaceQuality::GOOD;
    return out;
}

FaceDetection detectFaceNir(const IrFrameStats& frame) {
    FaceQuality emptyQuality = FaceQuality::NOT_DETECTED;
    auto cands = detectFaceCandidatesNir(frame, 1u, &emptyQuality);
    if (!cands.empty()) {
        return cands.front();
    }
    FaceDetection res;
    res.quality = emptyQuality;
    return res;
}

MultiOrientationResult detectFaceNirMultiOrientation(const IrFrameStats& baseStats,
                                                     Rotation baseRotation,
                                                     bool allowFallback,
                                                     bool forceCompareAll) {
    MultiOrientationResult res;
    res.rotation = baseRotation;
    res.detection = detectFaceNir(baseStats);
    res.usedRotatedStats = false;

    if (!allowFallback ||
        res.detection.quality == FaceQuality::TOO_DARK ||
        res.detection.quality == FaceQuality::TOO_BRIGHT ||
        (res.detection.detected && !forceCompareAll)) {
        return res;
    }

    static constexpr std::array<Rotation, 3> kDeltas{
        Rotation::Deg90CW,
        Rotation::Deg270CW,
        Rotation::Deg180,
    };

    bool foundCand = false;
    FaceDetection bestCandDet{};
    Rotation bestCandRot = baseRotation;
    IrFrameStats bestCandStats{};

    for (Rotation delta : kDeltas) {
        IrFrameStats candStats = rotateIrFrameStats(baseStats, delta);
        const FaceDetection candDet = detectFaceNir(candStats);
        if (candDet.detected && (!foundCand || candDet.score > bestCandDet.score)) {
            foundCand = true;
            bestCandDet = candDet;
            bestCandRot = composeRotation(baseRotation, delta);
            bestCandStats = std::move(candStats);
        }
    }

    if (foundCand && (!res.detection.detected || bestCandDet.score > res.detection.score + 10.0f)) {
        res.detection = bestCandDet;
        res.rotation = bestCandRot;
        res.usedRotatedStats = true;
        res.rotatedStats = std::move(bestCandStats);
    }
    return res;
}

namespace {

Embedding extractEmbeddingWithOffset(const IrFrameStats& frame,
                                     const FaceDetection& det,
                                     float dstShiftX,
                                     float dstShiftY,
                                     float scaleMul,
                                     const OptionalModelWeights* model,
                                     float scaleYMul = 1.0f) {
    Embedding emb{};
    if (!det.detected || frame.gray8.empty()) return emb;

    const float scaleRatio = static_cast<float>(kAlignedSize) / 112.0f;
    const float dx = det.rightEye.x - det.leftEye.x;
    const float dy = det.rightEye.y - det.leftEye.y;
    const float interOcular = std::max(12.0f, std::hypot(dx, dy));
    const float ang = std::clamp(std::atan2(dy, std::max(8.0f, dx)), -0.35f, 0.35f);

    const double dstMidX = (55.9132 + static_cast<double>(dstShiftX)) * scaleRatio;
    const double dstMidY = (51.6963 + static_cast<double>(dstShiftY)) * scaleRatio;
    const double dstInterOcular = 35.2372 * scaleRatio;
    const double sxScale =
        (static_cast<double>(interOcular) / dstInterOcular) * static_cast<double>(scaleMul);
    const double syScale = sxScale * static_cast<double>(scaleYMul);
    const double cosAng = std::cos(static_cast<double>(ang));
    const double sinAng = std::sin(static_cast<double>(ang));
    const double ax = cosAng * sxScale;
    const double bx = sinAng * sxScale;
    const double ay = cosAng * syScale;
    const double by = sinAng * syScale;
    const double midSrcX =
        0.5 * (static_cast<double>(det.leftEye.x) + static_cast<double>(det.rightEye.x));
    const double midSrcY =
        0.5 * (static_cast<double>(det.leftEye.y) + static_cast<double>(det.rightEye.y));
    const double tx = midSrcX - (ax * dstMidX - by * dstMidY);
    const double ty = midSrcY - (bx * dstMidX + ay * dstMidY);

    std::array<float, kAlignedSize * kAlignedSize> patch{};
    for (int y = 0; y < kAlignedSize; ++y) {
        for (int x = 0; x < kAlignedSize; ++x) {
            const float sx = static_cast<float>(ax * x - by * y + tx);
            const float sy = static_cast<float>(bx * x + ay * y + ty);
            patch[static_cast<size_t>(y) * kAlignedSize + x] =
                sampleBilinear(frame.gray8.data(), frame.width, frame.height, sx, sy);
        }
    }

    // Inner-face plane-detrended tight DoG (sigma 1.2 vs 4.5) suppresses
    // outer head-silhouette parallax and directional 850nm LED shading gradients.
    std::array<float, kAlignedSize * kAlignedSize> gammaPatch{};
    for (size_t i = 0; i < patch.size(); ++i) {
        gammaPatch[i] = std::pow(std::max(0.0f, patch[i]) / 255.0f, 0.25f);
    }
    removeLinearPlaneInner(gammaPatch);
    std::array<float, kAlignedSize * kAlignedSize> bFine{}, bCoarse{}, midBand{};
    gaussianBlur112(gammaPatch, bFine, 1.2f);
    gaussianBlur112(gammaPatch, bCoarse, 4.5f);
    double midSq = 0.0;
    int midCnt = 0;
    for (int y = 24; y < 100; ++y) {
        for (int x = 20; x < 92; ++x) {
            const float v = bFine[static_cast<size_t>(y) * kAlignedSize + x] -
                            bCoarse[static_cast<size_t>(y) * kAlignedSize + x];
            midSq += static_cast<double>(v) * v;
            ++midCnt;
        }
    }
    const float midRms =
        static_cast<float>(std::sqrt(midSq / std::max(1, midCnt)) + 1e-6);
    for (size_t i = 0; i < patch.size(); ++i) {
        midBand[i] = std::tanh((bFine[i] - bCoarse[i]) / (2.5f * midRms));
    }

    tanTriggsNormalize(patch);

    size_t dimIdx = 0;
    auto normalizeSlice = [&](size_t start, size_t count) {
        double sq = 0.0;
        for (size_t i = start; i < start + count; ++i) sq += static_cast<double>(emb[i]) * emb[i];
        const float inv = (sq > 1e-12) ? static_cast<float>(1.0 / std::sqrt(sq)) : 0.0f;
        for (size_t i = start; i < start + count; ++i) emb[i] *= inv;
    };

    // 1a. 64 dims: 8x8 overlapping (13x13 window, stride 9) grid on inner face [20..92] x [26..98].
    {
        const size_t s0 = dimIdx;
        float mean64 = 0.0f;
        for (int gy = 0; gy < 8; ++gy) {
            const int cy = 30 + gy * 9;
            for (int gx = 0; gx < 8; ++gx) {
                const int cx = 24 + gx * 9;
                float sum = 0.0f;
                for (int y = cy - 6; y <= cy + 6; ++y) {
                    for (int x = cx - 6; x <= cx + 6; ++x) {
                        sum += midBand[static_cast<size_t>(y) * kAlignedSize + x];
                    }
                }
                const float v = sum / 169.0f;
                emb[dimIdx++] = v;
                mean64 += v;
            }
        }
        mean64 /= 64.0f;
        for (size_t i = s0; i < s0 + 64; ++i) emb[i] -= mean64;
        normalizeSlice(s0, 64);
    }

    // 1b. 64 dims: 4x4 inner-face spatial grid x 4 zero-centered steerable gradient energies.
    {
        const size_t s0 = dimIdx;
        for (int gy = 0; gy < 4; ++gy) {
            const int y0 = 26 + gy * 18;
            const int y1 = y0 + 18;
            for (int gx = 0; gx < 4; ++gx) {
                const int x0 = 20 + gx * 18;
                const int x1 = x0 + 18;
                std::array<float, 4> ori{};
                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        const float ddx = patch[y * kAlignedSize + (x + 1)] - patch[y * kAlignedSize + (x - 1)];
                        const float ddy = patch[(y + 1) * kAlignedSize + x] - patch[(y - 1) * kAlignedSize + x];
                        const float d1 = patch[(y + 1) * kAlignedSize + (x + 1)] - patch[(y - 1) * kAlignedSize + (x - 1)];
                        const float d2 = patch[(y + 1) * kAlignedSize + (x - 1)] - patch[(y - 1) * kAlignedSize + (x + 1)];
                        ori[0] += std::abs(ddx);
                        ori[1] += std::abs(ddy);
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

    // 2. 96 dims: 12 inner facial landmark patches x 8 soft-binned zero-centered signed HOG bins.
    {
        const size_t s0 = dimIdx;
        static constexpr std::array<std::pair<int, int>, 12> kHogCenters{{
            {38, 52}, {74, 52}, {56, 48}, {36, 70},
            {56, 62}, {76, 70}, {56, 72}, {46, 74},
            {66, 74}, {42, 92}, {56, 92}, {70, 92},
        }};
        for (const auto& [cx, cy] : kHogCenters) {
            std::array<float, 8> hog{};
            for (int ody = -13; ody <= 13; ++ody) {
                const int y = std::clamp(cy + ody, 1, kAlignedSize - 2);
                for (int odx = -13; odx <= 13; ++odx) {
                    const int x = std::clamp(cx + odx, 1, kAlignedSize - 2);
                    const float gx = patch[y * kAlignedSize + (x + 1)] - patch[y * kAlignedSize + (x - 1)];
                    const float gy = patch[(y + 1) * kAlignedSize + x] - patch[(y - 1) * kAlignedSize + x];
                    const float mag = std::sqrt(gx * gx + gy * gy);
                    float gAng = std::atan2(gy, gx);
                    if (gAng < 0.0f) gAng += 2.0f * kPi;
                    const float fb = (gAng * 8.0f) / (2.0f * kPi);
                    const int b0 = static_cast<int>(fb) & 7;
                    const int b1 = (b0 + 1) & 7;
                    const float frac = fb - std::floor(fb);
                    hog[b0] += mag * (1.0f - frac);
                    hog[b1] += mag * frac;
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

    // 3. 32 dims: low-frequency 6x6 2D DCT AC on inner face [20..92] x [26..98] (24x24 coarse grid).
    {
        const size_t s0 = dimIdx;
        std::array<float, 24 * 24> coarse{};
        for (int y = 0; y < 24; ++y) {
            for (int x = 0; x < 24; ++x) {
                const int sx = 20 + x * 3;
                const int sy = 26 + y * 3;
                float acc = 0.0f;
                for (int cdy = 0; cdy < 3; ++cdy) {
                    for (int cdx = 0; cdx < 3; ++cdx) {
                        acc += midBand[(sy + cdy) * kAlignedSize + (sx + cdx)];
                    }
                }
                coarse[y * 24 + x] = acc / 9.0f;
            }
        }
        size_t dctCount = 0;
        for (int v = 0; v < 6 && dctCount < 32; ++v) {
            for (int u = 0; u < 6 && dctCount < 32; ++u) {
                if ((u + v) <= 1 || (u == 1 && v == 1)) continue;
                float sum = 0.0f;
                for (int y = 0; y < 24; ++y) {
                    const float cy = std::cos((kPi * (2 * y + 1) * v) / 48.0f);
                    for (int x = 0; x < 24; ++x) {
                        const float cx = std::cos((kPi * (2 * x + 1) * u) / 48.0f);
                        sum += coarse[y * 24 + x] * cx * cy;
                    }
                }
                emb[dimIdx++] = sum / 24.0f;
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

}  // namespace

Embedding extractEmbedding(const IrFrameStats& frame,
                           const FaceDetection& det,
                           const OptionalModelWeights* model) {
    return extractEmbeddingWithOffset(frame, det, 0.0f, 0.0f, 1.0f, model);
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
    if (satRatio > 0.45f) {
        report.status = LivenessStatus::REJECT_SPECULAR_GLARE;
        return report;
    }

    // Layer 2: 3D Lambertian convexity + halftone/paper texture rejection.
    // Measure two symmetric, linear-gradient-invariant ratios strictly inside the face oval:
    //   1) nose-bridge T-zone [42..58%]x[40..58%] vs mid-cheeks [16..28%],[72..84%]x[46..64%]
    //   2) inner malar cheeks [24..40%],[60..76%]x[44..60%] vs outer receding cheeks
    //      [2..14%],[86..98%]x[44..60%]
    // A flat print (even tilted 30-45deg under side lighting) yields ~1.00 on both, while a real
    // 3D face under side-bezel Portrait 850nm illumination or glasses nose-pad shadows retains
    // strong lateral ellipsoid falloff on ratio (2) even when ratio (1) drops below 1.04.
    const unsigned bw = x1 - x0;
    const unsigned bh = y1 - y0;
    const unsigned cx0 = x0 + (bw * 42u) / 100u;
    const unsigned cx1 = std::min(x1, std::max(cx0 + 1u, x0 + (bw * 58u) / 100u));
    const unsigned cy0 = y0 + (bh * 40u) / 100u;
    const unsigned cy1 = std::min(y1, std::max(cy0 + 1u, y0 + (bh * 58u) / 100u));

    const unsigned lc0 = x0 + (bw * 16u) / 100u;
    const unsigned lc1 = std::min(x1, std::max(lc0 + 1u, x0 + (bw * 28u) / 100u));
    const unsigned rc0 = x0 + (bw * 72u) / 100u;
    const unsigned rc1 = std::min(x1, std::max(rc0 + 1u, x0 + (bw * 84u) / 100u));
    const unsigned ky0 = y0 + (bh * 46u) / 100u;
    const unsigned ky1 = std::min(y1, std::max(ky0 + 1u, y0 + (bh * 64u) / 100u));

    const unsigned coreL0 = x0 + (bw * 24u) / 100u;
    const unsigned coreL1 = std::min(x1, std::max(coreL0 + 1u, x0 + (bw * 40u) / 100u));
    const unsigned coreR0 = x0 + (bw * 60u) / 100u;
    const unsigned coreR1 = std::min(x1, std::max(coreR0 + 1u, x0 + (bw * 76u) / 100u));
    const unsigned ol0 = x0 + (bw * 2u) / 100u;
    const unsigned ol1 = std::min(x1, std::max(ol0 + 1u, x0 + (bw * 14u) / 100u));
    const unsigned or0 = x0 + (bw * 86u) / 100u;
    const unsigned or1 = std::min(x1, std::max(or0 + 1u, x0 + (bw * 98u) / 100u));
    const unsigned oy0 = y0 + (bh * 44u) / 100u;
    const unsigned oy1 = std::min(y1, std::max(oy0 + 1u, y0 + (bh * 60u) / 100u));

    uint64_t centerSum = 0, centerCnt = 0;
    uint64_t rimSum = 0, rimCnt = 0;
    uint64_t coreSum = 0, coreCnt = 0;
    uint64_t outerRimSum = 0, outerRimCnt = 0;
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
        if (y >= oy0 && y < oy1) {
            for (unsigned x = coreL0; x < coreL1; ++x) {
                coreSum += row[x];
                ++coreCnt;
            }
            for (unsigned x = coreR0; x < coreR1; ++x) {
                coreSum += row[x];
                ++coreCnt;
            }
            for (unsigned x = ol0; x < ol1; ++x) {
                outerRimSum += row[x];
                ++outerRimCnt;
            }
            for (unsigned x = or0; x < or1; ++x) {
                outerRimSum += row[x];
                ++outerRimCnt;
            }
        }
    }
    const float centerMean =
        centerCnt > 0 ? static_cast<float>(centerSum) / static_cast<float>(centerCnt) : 0.0f;
    const float rimMean =
        rimCnt > 0 ? static_cast<float>(rimSum) / static_cast<float>(rimCnt) : 1.0f;
    const float noseRatio = centerMean / std::max(1.0f, rimMean);

    const float coreMean =
        coreCnt > 0 ? static_cast<float>(coreSum) / static_cast<float>(coreCnt) : 0.0f;
    const float outerRimMean =
        outerRimCnt > 0 ? static_cast<float>(outerRimSum) / static_cast<float>(outerRimCnt) : 1.0f;
    const float coreOuterRatio = coreMean / std::max(1.0f, outerRimMean);
    report.centerToRimRatio = std::max(noseRatio, coreOuterRatio);

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

    if (report.centerToRimRatio < 1.04f) {
        report.status = LivenessStatus::REJECT_FLAT_OR_PRINT;
        return report;
    }
    if (report.halftoneRatio > 1.85f) {
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
    featureMask_ = 0x01u;
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

namespace {

template <typename MatchFn>
RefinedMatchResult matchWithAlignmentRefinementImpl(
    const IrFrameStats& frame,
    const FaceDetection& det,
    const Embedding& baseEmb,
    MatchFn&& matchFn,
    const OptionalModelWeights* model,
    float threshold) {
    const MatchResult baseRes = matchFn(baseEmb, threshold);
    if (baseRes.matched || !det.detected || baseRes.bestSimilarity < 0.36f) {
        return {baseRes, false};
    }

    // Stage 1: pure horizontal yaw-parallax translation (no scale distortion).
    // Side-bezel Portrait viewing (ROTATION_90 power-at-top & ROTATION_270 volume-at-top,
    // +-12..20 deg yaw) shifts nose/mouth +-4..6px relative to the eye midpoint, pulling raw
    // cosine down to ~0.38..0.62 on the genuine owner while impostors remain cleanly below
    // the strict 0.64 identity gate.
    MatchResult bestRes = baseRes;
    float bestDx = 0.0f;
    float bestDy = 0.0f;
    for (float dx : {-6.0f, -4.0f, 4.0f, 6.0f}) {
        const Embedding candEmb =
            extractEmbeddingWithOffset(frame, det, dx, 0.0f, 1.0f, model);
        const MatchResult candRes = matchFn(candEmb, threshold);
        if (candRes.bestSimilarity > bestRes.bestSimilarity) {
            bestRes = candRes;
            bestDx = dx;
        }
    }
    if (bestRes.bestSimilarity < 0.64f) {
        return {bestRes, bestRes.bestSimilarity > baseRes.bestSimilarity};
    }

    // Stage 2: vertical, isotropic scale, and yaw-foreshortening aspect refinement
    // strictly gated behind the 0.64 identity threshold so impostors cannot enter.
    for (float dy : {-2.5f, 2.5f}) {
        const Embedding candEmb =
            extractEmbeddingWithOffset(frame, det, bestDx, dy, 1.0f, model);
        const MatchResult candRes = matchFn(candEmb, threshold);
        if (candRes.bestSimilarity > bestRes.bestSimilarity) {
            bestRes = candRes;
            bestDy = dy;
        }
    }
    if (!bestRes.matched) {
        for (float sc : {0.94f, 1.06f}) {
            const Embedding candEmb =
                extractEmbeddingWithOffset(frame, det, bestDx, bestDy, sc, model);
            const MatchResult candRes = matchFn(candEmb, threshold);
            if (candRes.bestSimilarity > bestRes.bestSimilarity) {
                bestRes = candRes;
            }
        }
    }
    if (!bestRes.matched && bestDx != 0.0f) {
        // Under 16..20 deg side-bezel yaw, horizontal facial geometry foreshortens relative to
        // vertical height; compensating horizontal scale (0.90..0.94) while preserving vertical
        // scale (scaleYMul=1.12..1.15) also slightly reduces the residual horizontal shift.
        struct YawWarp {
            float dx;
            float scX;
            float syMul;
        };
        const float pulledDx = bestDx - std::copysign(2.0f, bestDx);
        const std::array<YawWarp, 3> warps{{
            {bestDx, 0.94f, 1.12f},
            {bestDx, 0.90f, 1.15f},
            {pulledDx, 0.90f, 1.15f},
        }};
        for (const auto& w : warps) {
            const Embedding candEmb =
                extractEmbeddingWithOffset(frame, det, w.dx, bestDy, w.scX, model, w.syMul);
            const MatchResult candRes = matchFn(candEmb, threshold);
            if (candRes.bestSimilarity > bestRes.bestSimilarity) {
                bestRes = candRes;
            }
        }
    }
    return {bestRes, bestRes.bestSimilarity > baseRes.bestSimilarity};
}

}  // namespace

MatchResult matchEmbeddings(const std::vector<Embedding>& candidates,
                            const Embedding& probe,
                            float threshold) {
    MatchResult best;
    for (const auto& cand : candidates) {
        const float sim = cosineSimilarity(probe, cand);
        if (sim > best.bestSimilarity) {
            best.bestSimilarity = sim;
            if (sim >= threshold) {
                best.matched = true;
                best.enrollmentId = 1;
            }
        }
    }
    return best;
}

RefinedMatchResult matchWithAlignmentRefinement(
    const IrFrameStats& frame,
    const FaceDetection& det,
    const Embedding& baseEmb,
    const SpxFaceStore& store,
    const OptionalModelWeights* model,
    float threshold) {
    return matchWithAlignmentRefinementImpl(
        frame,
        det,
        baseEmb,
        [&](const Embedding& emb, float thresh) { return store.match(emb, thresh); },
        model,
        threshold);
}

RefinedMatchResult matchCandidatesWithAlignmentRefinement(
    const IrFrameStats& frame,
    const FaceDetection& det,
    const Embedding& baseEmb,
    const std::vector<Embedding>& candidates,
    const OptionalModelWeights* model,
    float threshold) {
    return matchWithAlignmentRefinementImpl(
        frame,
        det,
        baseEmb,
        [&](const Embedding& emb, float thresh) {
            return matchEmbeddings(candidates, emb, thresh);
        },
        model,
        threshold);
}

OrientationSwitchHint probeAlternateOrientationMatch(const IrFrameStats& baseStats,
                                                     Rotation baseRotation,
                                                     float baseSimilarity,
                                                     const SpxFaceStore& store,
                                                     const OptionalModelWeights* model,
                                                     float matchThreshold,
                                                     bool requireFullMatch) {
    OrientationSwitchHint hint;
    static constexpr std::array<Rotation, 3> kDeltas{
        Rotation::Deg90CW,
        Rotation::Deg270CW,
        Rotation::Deg180,
    };

    const float minSwitchSim = std::max(0.62f, matchThreshold - 0.06f);
    for (Rotation delta : kDeltas) {
        const IrFrameStats candStats = rotateIrFrameStats(baseStats, delta);
        const auto candDets = detectFaceCandidatesNir(candStats, 2u);
        for (const auto& candDet : candDets) {
            if (!candDet.detected || candDet.score < 42.0f) {
                continue;
            }
            const Embedding candEmb = extractEmbedding(candStats, candDet, model);
            const auto staticLive = evaluateLiveness(candStats, candDet, candEmb, nullptr, false);
            if (!staticLive.live) {
                continue;
            }
            const MatchResult candMatch = store.match(candEmb, matchThreshold);
            const bool qualified =
                requireFullMatch
                    ? (candMatch.matched && candMatch.bestSimilarity > baseSimilarity + 0.08f)
                    : ((candMatch.matched && candMatch.bestSimilarity > baseSimilarity + 0.05f) ||
                       (candMatch.bestSimilarity >= minSwitchSim &&
                        candMatch.bestSimilarity >= baseSimilarity + 0.12f));
            if (qualified && (!hint.shouldSwitch || candMatch.bestSimilarity > hint.bestSimilarity)) {
                hint.shouldSwitch = true;
                hint.rotation = composeRotation(baseRotation, delta);
                hint.bestSimilarity = candMatch.bestSimilarity;
                hint.detScore = candDet.score;
            }
        }
    }
    return hint;
}

}  // namespace spx::face
