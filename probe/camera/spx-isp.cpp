// SPDX-License-Identifier: GPL-2.0-only
#include "spx-isp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace spx {
namespace {

constexpr float kQ5Scale = 32.f;
constexpr float kQ5ToGamma = 4095.f / (1023.f * kQ5Scale);

// Observed dark floor with the current sensor mode tables: 16 (OV5693) / 64 (OV13858) DN.
// Indexed by 4x raw10 sum ([0, 4092]) so center, 2-tap, and 4-tap Bayer averages are exact.
const std::array<uint16_t, 4093>& blackLevelLut(bool front) {
    static const auto makeLut = [](float black) {
        std::array<uint16_t, 4093> table{};
        const float scale = (1023.f / (1023.f - black)) * kQ5Scale;
        for (unsigned u4 = 0; u4 < table.size(); ++u4) {
            float v = std::max(0.f, float(u4) * 0.25f - black) * scale;
            table[u4] = uint16_t(std::clamp<long>(std::lround(v), 0L, 32736L));
        }
        return table;
    };
    static const auto frontLut = makeLut(16.f);
    static const auto rearLut = makeLut(64.f);
    return front ? frontLut : rearLut;
}

const std::array<uint8_t, 4096>& srgbGammaLut() {
    static const auto table = [] {
        std::array<uint8_t, 4096> lut{};
        for (unsigned i = 0; i < lut.size(); ++i) {
            float v = float(i) / 4095.f;
            float s = v <= .0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.f / 2.4f) - .055f;
            lut[i] = uint8_t(std::clamp<long>(std::lround(255.f * s), 0L, 255L));
        }
        return lut;
    }();
    return table;
}

inline void unpackRaw10RowPadded(const uint8_t* src, uint16_t* dst, unsigned width) {
    // Write pixels [0..width-1] into dst[1..width] and mirror edges at dst[0] and dst[width+1]
    // to preserve Bayer color parity without per-pixel bounds checks.
    for (unsigned x = 0; x < width; x += 4, src += 5) {
        const uint32_t lsb = src[4];
        dst[x + 1] = uint16_t((uint32_t(src[0]) << 2) | (lsb & 3u));
        dst[x + 2] = uint16_t((uint32_t(src[1]) << 2) | ((lsb >> 2) & 3u));
        dst[x + 3] = uint16_t((uint32_t(src[2]) << 2) | ((lsb >> 4) & 3u));
        dst[x + 4] = uint16_t((uint32_t(src[3]) << 2) | ((lsb >> 6) & 3u));
    }
    dst[0] = dst[2];
    dst[width + 1] = dst[width - 1];
}

inline void demosaicFullRow(const uint16_t* r0, const uint16_t* r1, const uint16_t* r2,
                            unsigned width, bool redRow,
                            const std::array<uint16_t, 4093>& lut,
                            uint16_t* out, uint64_t sums[3]) {
    // r0/r1/r2 are padded rows: pixel x lives at index x+1, with mirrored edges at 0 and width+1.
    uint64_t sr = 0, sg = 0, sb = 0;
    if (redRow) {
        // Even x (idx x+1) = Gr (R horizontal, B vertical); odd x+1 (idx x+2) = R (G cross, B diagonal).
        for (unsigned x = 0; x < width; x += 2, out += 6) {
            const uint16_t r_even = lut[(uint32_t(r1[x]) + r1[x + 2]) << 1];
            const uint16_t g_even = lut[uint32_t(r1[x + 1]) << 2];
            const uint16_t b_even = lut[(uint32_t(r0[x + 1]) + r2[x + 1]) << 1];
            const uint16_t r_odd = lut[uint32_t(r1[x + 2]) << 2];
            const uint16_t g_odd = lut[uint32_t(r1[x + 1]) + r1[x + 3] + r0[x + 2] + r2[x + 2]];
            const uint16_t b_odd = lut[uint32_t(r0[x + 1]) + r0[x + 3] + r2[x + 1] + r2[x + 3]];
            out[0] = r_even; out[1] = g_even; out[2] = b_even;
            out[3] = r_odd;  out[4] = g_odd;  out[5] = b_odd;
            sr += uint32_t(r_even) + r_odd;
            sg += uint32_t(g_even) + g_odd;
            sb += uint32_t(b_even) + b_odd;
        }
    } else {
        // Even x (idx x+1) = B (R diagonal, G cross); odd x+1 (idx x+2) = Gb (R vertical, B horizontal).
        for (unsigned x = 0; x < width; x += 2, out += 6) {
            const uint16_t r_even = lut[uint32_t(r0[x]) + r0[x + 2] + r2[x] + r2[x + 2]];
            const uint16_t g_even = lut[uint32_t(r1[x]) + r1[x + 2] + r0[x + 1] + r2[x + 1]];
            const uint16_t b_even = lut[uint32_t(r1[x + 1]) << 2];
            const uint16_t r_odd = lut[(uint32_t(r0[x + 2]) + r2[x + 2]) << 1];
            const uint16_t g_odd = lut[uint32_t(r1[x + 2]) << 2];
            const uint16_t b_odd = lut[(uint32_t(r1[x + 1]) + r1[x + 3]) << 1];
            out[0] = r_even; out[1] = g_even; out[2] = b_even;
            out[3] = r_odd;  out[4] = g_odd;  out[5] = b_odd;
            sr += uint32_t(r_even) + r_odd;
            sg += uint32_t(g_even) + g_odd;
            sb += uint32_t(b_even) + b_odd;
        }
    }
    sums[0] += sr;
    sums[1] += sg;
    sums[2] += sb;
}

inline int toGammaIndex(float v) {
    // NaN-safe clamp to [0, 4095] (corresponds to clamping linear RGB to [0, 1023] before gamma).
    if (!(v > 0.f)) return 0;
    if (v >= 4095.f) return 4095;
    return int(v + 0.5f);
}

inline void shadePixel(uint32_t lr, uint32_t lg, uint32_t lb,
                       float rgScale, float gScale, float bgScale,
                       float ccmA, float ccmB,
                       const std::array<uint8_t, 4096>& gamma,
                       uint8_t* dst) {
    // Apply AWB gains, then the symmetric row-normalized 3x3 CCM in difference form:
    //   r' = r + a*(g - r) + b*(b - r)
    //   g' = g + a*((r - g) + (b - g))
    //   b' = b + a*(g - b) - b*(b - r)
    // This reduces the 3x3 matrix multiply to 4 multiplications and preserves bit-exact
    // neutral gray (r == g == b => r' == g' == b') across all color temperatures.
    const float r = float(lr) * rgScale;
    const float g = float(lg) * gScale;
    const float b = float(lb) * bgScale;
    const float tBr = ccmB * (b - r);
    const float rr = r + ccmA * (g - r) + tBr;
    const float gg = g + ccmA * ((r - g) + (b - g));
    const float bb = b + ccmA * (g - b) - tBr;
    dst[0] = gamma[toGammaIndex(rr)];
    dst[1] = gamma[toGammaIndex(gg)];
    dst[2] = gamma[toGammaIndex(bb)];
}

} // namespace

float Isp::estimateColorTemperature(float redGain, float blueGain) {
    if (!std::isfinite(redGain) || !std::isfinite(blueGain) ||
        redGain <= 0.f || blueGain <= 0.f)
        return 4200.f;
    // Wien's approximation maps log(blueGain / redGain) linearly to mireds (1e6 / K),
    // anchored at 4200 K when redGain == blueGain:
    //   low redGain / blueGain (warm scene, strong red) -> 2900 K
    //   high redGain / blueGain (cool scene, strong blue) -> 6500 K
    constexpr float kAnchorMired = 1e6f / 4200.f;
    constexpr float kSlopeMired = 1e6f / 7690.f;
    const float mired = kAnchorMired - kSlopeMired * std::log(redGain / blueGain);
    if (mired <= 1e6f / 6500.f) return 6500.f;
    if (mired >= 1e6f / 2900.f) return 2900.f;
    return std::clamp(1e6f / mired, 2900.f, 6500.f);
}

Isp::Ccm Isp::ccmForColorTemperature(float kelvin) {
    // Calibrated symmetric row- and column-normalized 3x3 CCMs (OV5693 / OV13858).
    // Every row and column sums identically to 1.0, eliminating magenta highlight clipping.
    constexpr float kT0 = 2900.f, kT1 = 4200.f, kT2 = 6500.f;
    constexpr float kA0 = -0.24f, kB0 = -0.18f;
    constexpr float kA1 = -0.22f, kB1 = -0.16f;
    constexpr float kA2 = -0.20f, kB2 = -0.14f;
    if (!std::isfinite(kelvin)) kelvin = kT1;
    float a, b;
    if (kelvin <= kT0) {
        a = kA0; b = kB0;
    } else if (kelvin <= kT1) {
        const float t = (kelvin - kT0) / (kT1 - kT0);
        a = kA0 + t * (kA1 - kA0);
        b = kB0 + t * (kB1 - kB0);
    } else if (kelvin < kT2) {
        const float t = (kelvin - kT1) / (kT2 - kT1);
        a = kA1 + t * (kA2 - kA1);
        b = kB1 + t * (kB2 - kB1);
    } else {
        a = kA2; b = kB2;
    }
    const float dRB = 1.f - a - b;
    const float dG = 1.f - 2.f * a;
    return {
        dRB, a,   b,
        a,   dG,  a,
        b,   a,   dRB,
    };
}

Image Isp::process(const Frame& f, unsigned w, unsigned h) {
    prepare(f, w > f.width / 2 || h > f.height / 2);
    return render(w, h);
}

void Isp::prepare(const Frame& f, bool fullResolution) {
    if (f.width < 4 || f.height < 2 || f.width % 4 ||
        f.stride < f.width / 4 * 5 || f.raw10.size() < size_t(f.stride) * f.height)
        throw std::runtime_error("invalid RAW10 image");

    bw = fullResolution ? f.width : f.width / 2;
    bh = fullResolution ? f.height : f.height / 2;
    cells.resize(size_t(bw) * bh * 3);
    const auto& lut = blackLevelLut(f.front);
    uint64_t sums[3]{};

    if (fullResolution) {
        const size_t rowSpan = size_t(f.width) + 2;
        scratchRows.resize(rowSpan * 3);
        uint16_t* ring[3] = {
            scratchRows.data(),
            scratchRows.data() + rowSpan,
            scratchRows.data() + rowSpan * 2,
        };
        const uint8_t* base = f.raw10.data();
        unpackRaw10RowPadded(base, ring[0], f.width);
        unpackRaw10RowPadded(base + f.stride, ring[1], f.width);

        // Row 0 mirrors y=-1 -> y=1.
        demosaicFullRow(ring[1], ring[0], ring[1],
                        f.width, !f.front, lut, cells.data(), sums);
        // Interior rows unpack each subsequent row exactly once into the 3-row ring buffer.
        for (unsigned y = 1; y + 1 < f.height; ++y) {
            unpackRaw10RowPadded(base + size_t(y + 1) * f.stride, ring[(y + 1) % 3], f.width);
            const bool redRow = f.front ? bool(y & 1u) : !(y & 1u);
            demosaicFullRow(ring[(y - 1) % 3], ring[y % 3], ring[(y + 1) % 3],
                            f.width, redRow, lut, cells.data() + size_t(y) * bw * 3, sums);
        }
        // Final row mirrors y=height -> y=height-2.
        const unsigned last = f.height - 1;
        const bool lastRedRow = f.front ? bool(last & 1u) : !(last & 1u);
        demosaicFullRow(ring[(last - 1) % 3], ring[last % 3], ring[(last - 1) % 3],
                        f.width, lastRedRow, lut, cells.data() + size_t(last) * bw * 3, sums);
    } else {
        const unsigned groups = f.width / 4;
        const uint8_t* base = f.raw10.data();
        if (f.front) {
            // SBGGR10 2x2 cell: [B=a, Gb=b; Gr=c, R=d]
            for (unsigned y = 0; y < bh; ++y) {
                const uint8_t* p0 = base + size_t(y * 2) * f.stride;
                const uint8_t* p1 = p0 + f.stride;
                uint16_t* out = cells.data() + size_t(y) * bw * 3;
                uint64_t sr = 0, sg = 0, sb = 0;
                for (unsigned g = 0; g < groups; ++g, p0 += 5, p1 += 5, out += 6) {
                    const uint32_t l0 = p0[4], l1 = p1[4];
                    const uint32_t a0 = (uint32_t(p0[0]) << 2) | (l0 & 3u);
                    const uint32_t b0 = (uint32_t(p0[1]) << 2) | ((l0 >> 2) & 3u);
                    const uint32_t a1 = (uint32_t(p0[2]) << 2) | ((l0 >> 4) & 3u);
                    const uint32_t b1 = (uint32_t(p0[3]) << 2) | ((l0 >> 6) & 3u);
                    const uint32_t c0 = (uint32_t(p1[0]) << 2) | (l1 & 3u);
                    const uint32_t d0 = (uint32_t(p1[1]) << 2) | ((l1 >> 2) & 3u);
                    const uint32_t c1 = (uint32_t(p1[2]) << 2) | ((l1 >> 4) & 3u);
                    const uint32_t d1 = (uint32_t(p1[3]) << 2) | ((l1 >> 6) & 3u);
                    const uint16_t r0v = lut[d0 << 2];
                    const uint16_t g0v = lut[(b0 + c0) << 1];
                    const uint16_t b0v = lut[a0 << 2];
                    const uint16_t r1v = lut[d1 << 2];
                    const uint16_t g1v = lut[(b1 + c1) << 1];
                    const uint16_t b1v = lut[a1 << 2];
                    out[0] = r0v; out[1] = g0v; out[2] = b0v;
                    out[3] = r1v; out[4] = g1v; out[5] = b1v;
                    sr += uint32_t(r0v) + r1v;
                    sg += uint32_t(g0v) + g1v;
                    sb += uint32_t(b0v) + b1v;
                }
                sums[0] += sr; sums[1] += sg; sums[2] += sb;
            }
        } else {
            // SGRBG10 2x2 cell: [Gr=a, R=b; B=c, Gb=d]
            for (unsigned y = 0; y < bh; ++y) {
                const uint8_t* p0 = base + size_t(y * 2) * f.stride;
                const uint8_t* p1 = p0 + f.stride;
                uint16_t* out = cells.data() + size_t(y) * bw * 3;
                uint64_t sr = 0, sg = 0, sb = 0;
                for (unsigned g = 0; g < groups; ++g, p0 += 5, p1 += 5, out += 6) {
                    const uint32_t l0 = p0[4], l1 = p1[4];
                    const uint32_t a0 = (uint32_t(p0[0]) << 2) | (l0 & 3u);
                    const uint32_t b0 = (uint32_t(p0[1]) << 2) | ((l0 >> 2) & 3u);
                    const uint32_t a1 = (uint32_t(p0[2]) << 2) | ((l0 >> 4) & 3u);
                    const uint32_t b1 = (uint32_t(p0[3]) << 2) | ((l0 >> 6) & 3u);
                    const uint32_t c0 = (uint32_t(p1[0]) << 2) | (l1 & 3u);
                    const uint32_t d0 = (uint32_t(p1[1]) << 2) | ((l1 >> 2) & 3u);
                    const uint32_t c1 = (uint32_t(p1[2]) << 2) | ((l1 >> 4) & 3u);
                    const uint32_t d1 = (uint32_t(p1[3]) << 2) | ((l1 >> 6) & 3u);
                    const uint16_t r0v = lut[b0 << 2];
                    const uint16_t g0v = lut[(a0 + d0) << 1];
                    const uint16_t b0v = lut[c0 << 2];
                    const uint16_t r1v = lut[b1 << 2];
                    const uint16_t g1v = lut[(a1 + d1) << 1];
                    const uint16_t b1v = lut[c1 << 2];
                    out[0] = r0v; out[1] = g0v; out[2] = b0v;
                    out[3] = r1v; out[4] = g1v; out[5] = b1v;
                    sr += uint32_t(r0v) + r1v;
                    sg += uint32_t(g0v) + g1v;
                    sb += uint32_t(b0v) + b1v;
                }
                sums[0] += sr; sums[1] += sg; sums[2] += sb;
            }
        }
    }

    const double sumR = double(sums[0]) / double(kQ5Scale);
    const double sumG = double(sums[1]) / double(kQ5Scale);
    const double sumB = double(sums[2]) / double(kQ5Scale);
    const float rg = std::clamp(float(sumG / std::max(sumR, 1.0)), .5f, 3.f);
    const float bg = std::clamp(float(sumG / std::max(sumB, 1.0)), .5f, 3.f);
    redGain = .9f * redGain + .1f * rg;
    blueGain = .9f * blueGain + .1f * bg;
    colorTempK = estimateColorTemperature(redGain, blueGain);
    mean = float((sumR + 2.0 * sumG + sumB) / (4.0 * double(bw) * double(bh) * 1023.0));
}

Image Isp::render(unsigned w, unsigned h) const {
    if (!w || !h || cells.empty() || !bw || !bh)
        throw std::runtime_error("invalid ISP output");
    Image out{w, h, std::vector<uint8_t>(size_t(w) * h * 3), mean};
    const auto& gamma = srgbGammaLut();
    const Ccm ccm = ccmForColorTemperature(colorTempK);
    const float ccmA = ccm[1];
    const float ccmB = ccm[2];
    const float rgScale = redGain * kQ5ToGamma;
    const float gScale = kQ5ToGamma;
    const float bgScale = blueGain * kQ5ToGamma;

    if (w == bw && h == bh) {
        const size_t n = size_t(w) * h;
        const uint16_t* src = cells.data();
        uint8_t* dst = out.rgb.data();
        for (size_t i = 0; i < n; ++i, src += 3, dst += 3) {
            shadePixel(src[0], src[1], src[2], rgScale, gScale, bgScale,
                       ccmA, ccmB, gamma, dst);
        }
        return out;
    }

    // Center crop to the requested aspect ratio, then bilinear resize with precomputed X taps.
    float cw = float(bw), ch = float(bh);
    if (cw * float(h) > ch * float(w)) cw = ch * float(w) / float(h);
    else ch = cw * float(h) / float(w);

    struct AxisTap {
        size_t i0;
        size_t i1;
        uint16_t w0;
        uint16_t w1;
    };
    std::vector<AxisTap> xTaps(w);
    const float xBase = (float(bw) - cw) * 0.5f - 0.5f;
    const float xStep = cw / float(w);
    const float maxX = float(bw - 1);
    for (unsigned x = 0; x < w; ++x) {
        const float sx = std::clamp(xBase + (float(x) + 0.5f) * xStep, 0.f, maxX);
        const unsigned ix = unsigned(sx);
        const unsigned jx = std::min(ix + 1, bw - 1);
        const uint16_t w1 = uint16_t(std::lround((sx - float(ix)) * 256.f));
        xTaps[x] = AxisTap{size_t(ix) * 3, size_t(jx) * 3, uint16_t(256u - w1), w1};
    }

    const float yBase = (float(bh) - ch) * 0.5f - 0.5f;
    const float yStep = ch / float(h);
    const float maxY = float(bh - 1);
    const size_t rowStride = size_t(bw) * 3;
    for (unsigned y = 0; y < h; ++y) {
        const float sy = std::clamp(yBase + (float(y) + 0.5f) * yStep, 0.f, maxY);
        const unsigned iy = unsigned(sy);
        const unsigned jy = std::min(iy + 1, bh - 1);
        const uint32_t wy1 = uint32_t(std::lround((sy - float(iy)) * 256.f));
        const uint32_t wy0 = 256u - wy1;
        const uint16_t* row0 = cells.data() + size_t(iy) * rowStride;
        const uint16_t* row1 = cells.data() + size_t(jy) * rowStride;
        uint8_t* dst = out.rgb.data() + size_t(y) * w * 3;
        for (unsigned x = 0; x < w; ++x, dst += 3) {
            const auto& t = xTaps[x];
            const uint16_t* p00 = row0 + t.i0;
            const uint16_t* p01 = row0 + t.i1;
            const uint16_t* p10 = row1 + t.i0;
            const uint16_t* p11 = row1 + t.i1;
            const uint32_t w00 = uint32_t(t.w0) * wy0;
            const uint32_t w01 = uint32_t(t.w1) * wy0;
            const uint32_t w10 = uint32_t(t.w0) * wy1;
            const uint32_t w11 = uint32_t(t.w1) * wy1;
            const uint32_t lr = (p00[0] * w00 + p01[0] * w01 + p10[0] * w10 + p11[0] * w11 + 32768u) >> 16;
            const uint32_t lg = (p00[1] * w00 + p01[1] * w01 + p10[1] * w10 + p11[1] * w11 + 32768u) >> 16;
            const uint32_t lb = (p00[2] * w00 + p01[2] * w01 + p10[2] * w10 + p11[2] * w11 + 32768u) >> 16;
            shadePixel(lr, lg, lb, rgScale, gScale, bgScale, ccmA, ccmB, gamma, dst);
        }
    }
    return out;
}

} // namespace spx
