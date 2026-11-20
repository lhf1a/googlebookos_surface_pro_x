// SPDX-License-Identifier: GPL-2.0-only
#include "../probe/camera/spx-isp.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {

spx::Frame makeSolidRaw10(unsigned w, unsigned h, unsigned stride, bool front,
                          uint16_t rVal, uint16_t gVal, uint16_t bVal) {
    spx::Frame f{w, h, stride, 0, 0, front, std::vector<uint8_t>(size_t(stride) * h, 0xaa)};
    for (unsigned y = 0; y < h; ++y) {
        for (unsigned x = 0; x < w; x += 4) {
            auto* p = f.raw10.data() + size_t(y) * stride + (x / 4) * 5;
            uint8_t lsb = 0;
            for (unsigned k = 0; k < 4; ++k) {
                bool red = front ? (y % 2 && (x + k) % 2) : (!(y % 2) && (x + k) % 2);
                bool blue = front ? (!(y % 2) && !((x + k) % 2)) : (y % 2 && !((x + k) % 2));
                uint16_t v = std::min<uint16_t>(1023, red ? rVal : blue ? bVal : gVal);
                p[k] = uint8_t(v >> 2);
                lsb |= uint8_t((v & 3u) << (k * 2));
            }
            p[4] = lsb;
        }
    }
    return f;
}

spx::Frame makeGradientRaw10(unsigned w, unsigned h, bool front) {
    unsigned stride = (w / 4) * 5;
    spx::Frame f{w, h, stride, 0, 0, front, std::vector<uint8_t>(size_t(stride) * h)};
    for (unsigned y = 0; y < h; ++y) {
        for (unsigned x = 0; x < w; x += 4) {
            auto* p = f.raw10.data() + size_t(y) * stride + (x / 4) * 5;
            uint8_t lsb = 0;
            for (unsigned k = 0; k < 4; ++k) {
                unsigned px = x + k;
                // Smooth luminance ramp well above black level (128..896).
                uint16_t v = uint16_t(128 + (768 * (px + y)) / (w + h));
                p[k] = uint8_t(v >> 2);
                lsb |= uint8_t((v & 3u) << (k * 2));
            }
            p[4] = lsb;
        }
    }
    return f;
}

void testLegacyBaseline() {
    for (bool front : {false, true}) {
        auto f = makeSolidRaw10(8, 4, 16, front, 512, 256, 128);
        spx::Isp isp;
        auto im = isp.process(f, 4, 2);
        assert(im.rgb.size() == 24 && im.rgb[0] > im.rgb[1] && im.rgb[1] > im.rgb[2]);
        assert(std::abs(im.linearMean - (288.f - (front ? 16.f : 64.f)) / (1023.f - (front ? 16.f : 64.f))) < .001f);
        for (size_t i = 3; i < im.rgb.size(); ++i) assert(im.rgb[i] == im.rgb[i % 3]);
        isp.prepare(f, true);
        auto full = isp.render(8, 4);
        assert(full.width == 8 && full.height == 4 && full.rgb.size() == 96);
        for (size_t i = 3; i < full.rgb.size(); ++i) assert(full.rgb[i] == full.rgb[i % 3]);
        assert(isp.render(8, 4).rgb == full.rgb); // Rendering must not advance AWB again.
        assert(isp.render(4, 2).linearMean == full.linearMean);
        // A green photosite must remain distinct at native resolution.
        unsigned gx = front ? 1 : 0;
        f.raw10[gx] = 160;
        isp.prepare(f, true);
        auto detail = isp.render(8, 4);
        assert(detail.rgb[gx * 3 + 1] > detail.rgb[(gx + 2) * 3 + 1]);
        f.raw10.resize(10);
        bool caught = false;
        try { isp.process(f, 4, 2); } catch (const std::runtime_error&) { caught = true; }
        assert(caught);
    }
    // Unprepared render or zero dimensions must throw.
    spx::Isp emptyIsp;
    bool threw = false;
    try { (void)emptyIsp.render(4, 2); } catch (const std::runtime_error&) { threw = true; }
    assert(threw);
}

void testCcmPropertiesAndInterpolation() {
    auto near = [](float a, float b) { return std::abs(a - b) < 1e-5f; };
    const spx::Isp::Ccm k2900{1.42f, -0.24f, -0.18f, -0.24f, 1.48f, -0.24f, -0.18f, -0.24f, 1.42f};
    const spx::Isp::Ccm k4200{1.38f, -0.22f, -0.16f, -0.22f, 1.44f, -0.22f, -0.16f, -0.22f, 1.38f};
    const spx::Isp::Ccm k6500{1.34f, -0.20f, -0.14f, -0.20f, 1.40f, -0.20f, -0.14f, -0.20f, 1.34f};

    auto checkEq = [&](const spx::Isp::Ccm& a, const spx::Isp::Ccm& b) {
        for (size_t i = 0; i < 9; ++i) assert(near(a[i], b[i]));
    };
    checkEq(spx::Isp::ccmForColorTemperature(2900.f), k2900);
    checkEq(spx::Isp::ccmForColorTemperature(1500.f), k2900);
    checkEq(spx::Isp::ccmForColorTemperature(4200.f), k4200);
    checkEq(spx::Isp::ccmForColorTemperature(6500.f), k6500);
    checkEq(spx::Isp::ccmForColorTemperature(9500.f), k6500);
    checkEq(spx::Isp::ccmForColorTemperature(std::numeric_limits<float>::quiet_NaN()), k4200);

    // Every interpolated CCM must remain symmetric and row- & column-normalized to 1.0.
    for (float t : {2000.f, 2900.f, 3200.f, 3550.f, 4200.f, 5000.f, 5350.f, 6500.f, 9000.f}) {
        auto m = spx::Isp::ccmForColorTemperature(t);
        for (int r = 0; r < 3; ++r) {
            float rowSum = 0.f, colSum = 0.f;
            for (int c = 0; c < 3; ++c) {
                assert(near(m[r * 3 + c], m[c * 3 + r]));
                rowSum += m[r * 3 + c];
                colSum += m[c * 3 + r];
            }
            assert(near(rowSum, 1.f));
            assert(near(colSum, 1.f));
        }
    }

    // Color temperature estimation from AWB redGain/blueGain ratio must be monotonic & NaN-safe.
    float tWarm = spx::Isp::estimateColorTemperature(0.6f, 1.8f);
    float tMid = spx::Isp::estimateColorTemperature(1.0f, 1.0f);
    float tCool = spx::Isp::estimateColorTemperature(1.8f, 0.7f);
    assert(tWarm >= 2900.f && tWarm < tMid);
    assert(near(tMid, 4200.f));
    assert(tCool > tMid && tCool <= 6500.f);
    assert(near(spx::Isp::estimateColorTemperature(0.f, 1.f), 4200.f));
    assert(near(spx::Isp::estimateColorTemperature(-1.f, 1.f), 4200.f));
    assert(near(spx::Isp::estimateColorTemperature(std::numeric_limits<float>::quiet_NaN(), 1.f), 4200.f));
}

void testNeutralGrayAndZeroMagentaHighlightClipping() {
    for (bool front : {false, true}) {
        // Unbiased neutral gray across the full 10-bit range including highlight saturation (1023).
        for (uint16_t level : {uint16_t(0), uint16_t(16), uint16_t(64), uint16_t(128),
                               uint16_t(256), uint16_t(512), uint16_t(768),
                               uint16_t(960), uint16_t(1020), uint16_t(1023)}) {
            auto f = makeSolidRaw10(16, 8, 20, front, level, level, level);
            spx::Isp isp;
            for (bool full : {false, true}) {
                isp.prepare(f, full);
                auto im = isp.render(full ? 16 : 8, full ? 8 : 4);
                for (size_t i = 0; i < im.rgb.size(); i += 3) {
                    assert(im.rgb[i] == im.rgb[i + 1]);
                    assert(im.rgb[i + 1] == im.rgb[i + 2]);
                }
                if (level == 1023) {
                    assert(im.rgb[0] == 255 && im.rgb[1] == 255 && im.rgb[2] == 255);
                }
            }
        }

        // Adapt AWB to warm (2900K) and cool (6500K) illuminants, then verify WB-balanced
        // gray and bright highlights remain strictly neutral with zero magenta clipping.
        for (bool warm : {true, false}) {
            spx::Isp isp;
            const uint16_t black = front ? 16 : 64;
            auto illum = warm ? makeSolidRaw10(16, 8, 20, front, 700, 350, 180)
                              : makeSolidRaw10(16, 8, 20, front, 180, 350, 700);
            for (int i = 0; i < 40; ++i) isp.prepare(illum, false);
            if (warm) assert(isp.colorTemperature() < 3400.f);
            else assert(isp.colorTemperature() > 5200.f);

            const float rg = isp.currentRedGain();
            const float bg = isp.currentBlueGain();
            // Check mid-gray and near-white highlights matched to current AWB gains.
            for (float targetLinear : {250.f, 500.f, 950.f}) {
                const float invScale = (1023.f - black) / 1023.f;
                float rLin = std::min(1023.f, targetLinear / rg);
                float gLin = std::min(1023.f, targetLinear);
                float bLin = std::min(1023.f, targetLinear / bg);
                // Only test levels where none of the raw sensor channels clip before WB.
                if (rLin * invScale + black > 1022.f || bLin * invScale + black > 1022.f) continue;
                uint16_t rRaw = uint16_t(std::lround(rLin * invScale + black));
                uint16_t gRaw = uint16_t(std::lround(gLin * invScale + black));
                uint16_t bRaw = uint16_t(std::lround(bLin * invScale + black));
                // Use a fresh ISP copy so prepare() preserves the adapted gains for a single frame check.
                spx::Isp probe = isp;
                auto card = makeSolidRaw10(16, 8, 20, front, rRaw, gRaw, bRaw);
                probe.prepare(card, true);
                auto im = probe.render(16, 8);
                int r = im.rgb[0], g = im.rgb[1], b = im.rgb[2];
                // Neutral card under adapted WB must not drift or turn magenta (G must not lag R/B).
                assert(std::abs(r - g) <= 4 && std::abs(b - g) <= 4);
                assert(!(r > 240 && b > 240 && g < 230));
            }
        }
    }
}

void testSaturationAndClampingBounds() {
    for (bool front : {false, true}) {
        // Saturated primaries: CCM has >1.0 diagonal and negative off-diagonals, so clamping
        // to [0, 1023] before gamma must prevent wraparound/underflow/overflow.
        {
            spx::Isp isp;
            auto f = makeSolidRaw10(8, 4, 10, front, 1023, 0, 0);
            auto im = isp.process(f, 8, 4);
            assert(im.rgb[0] == 255 && im.rgb[1] == 0 && im.rgb[2] == 0);
        }
        {
            spx::Isp isp;
            auto f = makeSolidRaw10(8, 4, 10, front, 0, 1023, 0);
            auto im = isp.process(f, 8, 4);
            assert(im.rgb[0] == 0 && im.rgb[1] == 255 && im.rgb[2] == 0);
        }
        {
            spx::Isp isp;
            auto f = makeSolidRaw10(8, 4, 10, front, 0, 0, 1023);
            auto im = isp.process(f, 8, 4);
            assert(im.rgb[0] == 0 && im.rgb[1] == 0 && im.rgb[2] == 255);
        }
        {
            // Below black floor must clamp cleanly to pure black (0, 0, 0).
            spx::Isp isp;
            uint16_t black = front ? 16 : 64;
            auto f = makeSolidRaw10(8, 4, 10, front, black, black, black);
            auto im = isp.process(f, 4, 2);
            assert(im.rgb[0] == 0 && im.rgb[1] == 0 && im.rgb[2] == 0);
            assert(im.linearMean == 0.f);
        }
        {
            // Verify CCM visibly boosts saturation over identity on a moderately tinted patch.
            spx::Isp isp;
            auto f = makeSolidRaw10(16, 8, 20, front, 520, 320, 320);
            auto im = isp.process(f, 8, 4);
            // Compute what sRGB(r) - sRGB(g) would be WITHOUT CCM for the same linear values + AWB:
            const float black = front ? 16.f : 64.f;
            float rLin = (520.f - black) * 1023.f / (1023.f - black) * isp.currentRedGain();
            float gLin = (320.f - black) * 1023.f / (1023.f - black);
            auto srgb8 = [](float x) {
                float v = std::clamp(x / 1023.f, 0.f, 1.f);
                float s = v <= .0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.f / 2.4f) - .055f;
                return int(std::lround(255.f * s));
            };
            int spreadNoCcm = srgb8(rLin) - srgb8(gLin);
            int spreadWithCcm = int(im.rgb[0]) - int(im.rgb[1]);
            assert(spreadWithCcm > spreadNoCcm + 10);
        }
    }
}

void testFullHalfParityAndGeometryEdges() {
    for (bool front : {false, true}) {
        // Minimum height (h=2) and odd height (h=3) edge mirroring under ASan/UBSan.
        for (unsigned h : {2u, 3u, 4u, 48u}) {
            auto f = makeSolidRaw10(64, h, 80, front, 400, 300, 220);
            spx::Isp ispHalf, ispFull;
            ispHalf.prepare(f, false);
            ispFull.prepare(f, true);
            auto imHalf = ispHalf.render(32, std::max(1u, h / 2));
            auto imFull = ispFull.render(32, std::max(1u, h / 2));
            assert(std::abs(imHalf.linearMean - imFull.linearMean) < 1e-4f);
            assert(imHalf.rgb == imFull.rgb);
        }

        // Smooth spatial gradient parity between half-res and full-res downscaled to same size.
        auto grad = makeGradientRaw10(64, 48, front);
        spx::Isp ispHalf, ispFull;
        ispHalf.prepare(grad, false);
        ispFull.prepare(grad, true);
        auto imHalf = ispHalf.render(32, 24);
        auto imFull = ispFull.render(32, 24);
        assert(std::abs(imHalf.linearMean - imFull.linearMean) < 0.01f);
        int maxDiff = 0;
        for (size_t i = 0; i < imHalf.rgb.size(); ++i)
            maxDiff = std::max(maxDiff, std::abs(int(imHalf.rgb[i]) - int(imFull.rgb[i])));
        assert(maxDiff <= 6);

        // Aspect-ratio center crops (both wider and taller target ratios).
        auto wide = ispFull.render(48, 27);
        auto tall = ispFull.render(24, 32);
        assert(wide.rgb.size() == 48u * 27u * 3u);
        assert(tall.rgb.size() == 24u * 32u * 3u);
    }
}

void testThroughputSanity() {
    auto f = makeGradientRaw10(1296, 972, true);
    spx::Isp isp;
    // Warm up LUTs and scratch buffers.
    auto im = isp.process(f, 640, 480);
    assert(im.rgb.size() == 640u * 480u * 3u);

    auto t0 = std::chrono::steady_clock::now();
    auto imHalf = isp.process(f, 640, 480);
    auto t1 = std::chrono::steady_clock::now();
    auto imFull = isp.process(f, 1280, 720);
    auto t2 = std::chrono::steady_clock::now();

    double halfMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double fullMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    assert(imHalf.rgb.size() == 640u * 480u * 3u);
    assert(imFull.rgb.size() == 1280u * 720u * 3u);
    // Generous ceiling that still catches regressions even under ASan/UBSan instrumentation.
    assert(halfMs < 500.0 && fullMs < 1500.0);
    std::printf("spx_camera_isp_test: OK (1296x972 half->640x480: %.2f ms, full->1280x720: %.2f ms)\n",
                halfMs, fullMs);
}

} // namespace

int main() {
    testLegacyBaseline();
    testCcmPropertiesAndInterpolation();
    testNeutralGrayAndZeroMagentaHighlightClipping();
    testSaturationAndClampingBounds();
    testFullHalfParityAndGeometryEdges();
    testThroughputSanity();
    return 0;
}
