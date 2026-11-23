// SPDX-License-Identifier: GPL-2.0-only
/* Host ASan/UBSan test suite for SPX Hall Lid + OV7251 850nm IR Face pipeline. */
#include "spx-face-crypto.h"
#include "spx-face-engine.h"
#include "spx-ir-preprocess.h"
#include "spx-lid-monitor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

#define SPX_ASSERT(cond)                                                              \
    do {                                                                              \
        if (!(cond)) {                                                                \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            std::abort();                                                             \
        }                                                                             \
    } while (false)

void removeTree(const std::string& path) {
    struct stat st{};
    if (lstat(path.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(path.c_str());
        if (dir) {
            struct dirent* ent = nullptr;
            while ((ent = readdir(dir)) != nullptr) {
                if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) {
                    continue;
                }
                removeTree(path + "/" + ent->d_name);
            }
            closedir(dir);
        }
        rmdir(path.c_str());
    } else {
        unlink(path.c_str());
    }
}

std::string makeTempDir(const char* prefix) {
    std::string tmpl = std::string("/tmp/") + prefix + "XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    char* res = mkdtemp(buf.data());
    SPX_ASSERT(res != nullptr);
    return std::string(res);
}

std::vector<uint8_t> packY10(const std::vector<uint16_t>& px, unsigned w, unsigned h, unsigned stride) {
    std::vector<uint8_t> raw(static_cast<size_t>(h) * stride, 0);
    for (unsigned y = 0; y < h; ++y) {
        for (unsigned x = 0; x < w; ++x) {
            const uint16_t v = px[static_cast<size_t>(y) * w + x] & 0x03FFu;
            const size_t off = static_cast<size_t>(y) * stride + x * 2u;
            raw[off] = static_cast<uint8_t>(v & 0xFFu);
            raw[off + 1] = static_cast<uint8_t>((v >> 8) & 0x03u);
        }
    }
    return raw;
}

std::vector<uint8_t> packY10P(const std::vector<uint16_t>& px, unsigned w, unsigned h, unsigned stride) {
    SPX_ASSERT((w % 4u) == 0);
    std::vector<uint8_t> raw(static_cast<size_t>(h) * stride, 0);
    for (unsigned y = 0; y < h; ++y) {
        for (unsigned x = 0; x < w; x += 4u) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            const uint16_t p0 = px[idx + 0] & 0x03FFu;
            const uint16_t p1 = px[idx + 1] & 0x03FFu;
            const uint16_t p2 = px[idx + 2] & 0x03FFu;
            const uint16_t p3 = px[idx + 3] & 0x03FFu;
            const size_t off = static_cast<size_t>(y) * stride + (x / 4u) * 5u;
            raw[off + 0] = static_cast<uint8_t>(p0 >> 2);
            raw[off + 1] = static_cast<uint8_t>(p1 >> 2);
            raw[off + 2] = static_cast<uint8_t>(p2 >> 2);
            raw[off + 3] = static_cast<uint8_t>(p3 >> 2);
            raw[off + 4] = static_cast<uint8_t>((p0 & 3u) | ((p1 & 3u) << 2) |
                                                ((p2 & 3u) << 4) | ((p3 & 3u) << 6));
        }
    }
    return raw;
}

enum class SyntheticFaceVariant {
    Normal3D,
    ScreenSpoof,
    FlatPrint,
    DistinctPerson,
    SmallSoft850Nir65Cm,
};

std::vector<uint8_t> makeSyntheticFaceY10(unsigned w,
                                          unsigned h,
                                          SyntheticFaceVariant variant,
                                          float dxShiftPx = 0.0f,
                                          int noisePhase = 0) {
    std::vector<uint16_t> px(static_cast<size_t>(w) * h, 0);
    const float cx = w * 0.50f + dxShiftPx;
    const float cy = h * 0.48f;
    float rx = (variant == SyntheticFaceVariant::DistinctPerson) ? w * 0.27f : w * 0.24f;
    float ry = (variant == SyntheticFaceVariant::DistinctPerson) ? h * 0.25f : h * 0.28f;
    if (variant == SyntheticFaceVariant::SmallSoft850Nir65Cm) {
        rx = w * 0.11f;
        ry = h * 0.13f;
    }

    for (unsigned y = 0; y < h; ++y) {
        for (unsigned x = 0; x < w; ++x) {
            const float dx = (static_cast<float>(x) - cx) / rx;
            const float dy = (static_cast<float>(y) - cy) / ry;
            const float r2 = dx * dx + dy * dy;
            float val = 28.0f;
            if (r2 <= 1.0f) {
                if (variant == SyntheticFaceVariant::ScreenSpoof) {
                    val = 14.0f + 4.0f * (1.0f - r2);
                } else if (variant == SyntheticFaceVariant::FlatPrint) {
                    val = 340.0f;
                } else {
                    const float z = std::sqrt(std::max(0.0f, 1.0f - r2));
                    val = (variant == SyntheticFaceVariant::DistinctPerson)
                              ? (260.0f + 360.0f * z)
                              : (220.0f + 420.0f * z);
                }

                if (variant != SyntheticFaceVariant::ScreenSpoof) {
                    const float eyeSep = (variant == SyntheticFaceVariant::DistinctPerson) ? 0.44f : 0.38f;
                    const float le = (dx + eyeSep) * (dx + eyeSep) / 0.04f +
                                     (dy + 0.26f) * (dy + 0.26f) / 0.028f;
                    const float re = (dx - eyeSep) * (dx - eyeSep) / 0.04f +
                                     (dy + 0.26f) * (dy + 0.26f) / 0.028f;
                    if (le < 1.0f || re < 1.0f) {
                        val *= (variant == SyntheticFaceVariant::DistinctPerson) ? 0.25f : 0.42f;
                    }
                    const float nose = (dx * dx) / 0.028f + (dy - 0.04f) * (dy - 0.04f) / 0.09f;
                    if (nose < 1.0f && variant != SyntheticFaceVariant::FlatPrint &&
                        variant != SyntheticFaceVariant::SmallSoft850Nir65Cm) {
                        val += (variant == SyntheticFaceVariant::DistinctPerson)
                                   ? 200.0f * (1.0f - nose)
                                   : 140.0f * (1.0f - nose);
                    }
                    const float mouth = (dx * dx) / 0.14f + (dy - 0.48f) * (dy - 0.48f) / 0.015f;
                    if (mouth < 1.0f) {
                        val *= (variant == SyntheticFaceVariant::SmallSoft850Nir65Cm) ? 0.92f : 0.48f;
                    }
                    if (variant == SyntheticFaceVariant::DistinctPerson) {
                        // Prominent glasses/brow frame + high cheekbones + beard/chin gradient
                        const float brow = (dy + 0.39f) * (dy + 0.39f) / 0.008f + (dx * dx) / 0.48f;
                        if (brow < 1.0f) val *= 0.45f;
                        const float beard = ((dy - 0.56f) / 0.38f);
                        if (beard > 0.0f) val *= 0.58f;
                        val += 95.0f * std::cos(dx * 22.0f + dy * 9.0f) + 55.0f * std::sin(dy * 26.0f);
                    }
                }
            }
            const int tex = (variant == SyntheticFaceVariant::DistinctPerson)
                                ? static_cast<int>(((x * 19u + y * 3u) & 15u))
                                : static_cast<int>(((x * 7u + y * 13u) & 7u));
            const int temporal = (noisePhase != 0 && r2 <= 1.0f)
                                     ? (static_cast<int>(((x + y + noisePhase * 3u) % 9u)) * 3 - 12)
                                     : 0;
            const long v10 = std::clamp(std::lround(val + tex + temporal), 0L, 1023L);
            px[static_cast<size_t>(y) * w + x] = static_cast<uint16_t>(v10);
        }
    }
    return packY10(px, w, h, w * 2u);
}

void testIrPreprocess() {
    using namespace spx::face;
    SPX_ASSERT(parseRotationDegrees("0") == Rotation::Deg0);
    SPX_ASSERT(parseRotationDegrees("90") == Rotation::Deg90CW);
    SPX_ASSERT(parseRotationDegrees("180") == Rotation::Deg180);
    SPX_ASSERT(parseRotationDegrees("270") == Rotation::Deg270CW);
    SPX_ASSERT(parseRotationDegrees("invalid", Rotation::Deg180) == Rotation::Deg180);

    // Y10 vs Y10P unpacking equivalence & rotation orientation checks on 8x4 ramp.
    constexpr unsigned kW = 8;
    constexpr unsigned kH = 4;
    std::vector<uint16_t> px(kW * kH);
    for (unsigned y = 0; y < kH; ++y) {
        for (unsigned x = 0; x < kW; ++x) {
            px[y * kW + x] = static_cast<uint16_t>(30u + y * 120u + x * 31u);
        }
    }
    const auto y10 = packY10(px, kW, kH, kW * 2u + 4u);     // with row stride padding
    const auto y10p = packY10P(px, kW, kH, (kW / 4u) * 5u + 2u);

    const auto s0Y10 = preprocessIrFrame(y10.data(), y10.size(), kW, kH, kW * 2u + 4u, kPixFmtY10, Rotation::Deg0);
    const auto s0Y10P = preprocessIrFrame(y10p.data(), y10p.size(), kW, kH, (kW / 4u) * 5u + 2u, kPixFmtY10P, Rotation::Deg0);
    SPX_ASSERT(s0Y10.width == kW && s0Y10.height == kH);
    SPX_ASSERT(s0Y10.upright10 == px);
    SPX_ASSERT(s0Y10P.upright10 == px);
    SPX_ASSERT(s0Y10.p1 <= s0Y10.p50 && s0Y10.p50 <= s0Y10.p99);
    SPX_ASSERT(s0Y10.roiLinearMean > 0.0f && s0Y10.roiLinearMean <= 1.0f);

    const auto s90 = preprocessIrFrame(y10.data(), y10.size(), kW, kH, kW * 2u + 4u, kPixFmtY10, Rotation::Deg90CW);
    SPX_ASSERT(s90.width == kH && s90.height == kW);
    // Top-right in 90CW comes from (x=0, y=0)
    SPX_ASSERT(s90.upright10[0 * kH + (kH - 1)] == px[0]);
    // Bottom-right in 90CW comes from (x=kW-1, y=0)
    SPX_ASSERT(s90.upright10[(kW - 1) * kH + (kH - 1)] == px[kW - 1]);

    const auto s180 = preprocessIrFrame(y10.data(), y10.size(), kW, kH, kW * 2u + 4u, kPixFmtY10, Rotation::Deg180);
    SPX_ASSERT(s180.width == kW && s180.height == kH);
    SPX_ASSERT(s180.upright10[0] == px[kW * kH - 1]);

    const auto s270 = preprocessIrFrame(y10.data(), y10.size(), kW, kH, kW * 2u + 4u, kPixFmtY10, Rotation::Deg270CW);
    SPX_ASSERT(s270.width == kH && s270.height == kW);
    // Bottom-left in 270CW comes from (x=0, y=0)
    SPX_ASSERT(s270.upright10[(kW - 1) * kH + 0] == px[0]);

    // Dark & saturated frame statistics + gray8ToRgba conversion
    std::vector<uint16_t> darkPx(64 * 64, 5u);
    auto rawDark = packY10(darkPx, 64, 64, 128);
    auto stDark = preprocessIrFrame(rawDark.data(), rawDark.size(), 64, 64, 128, kPixFmtY10, Rotation::Deg0);
    SPX_ASSERT(stDark.darkRatio > 0.9f);
    SPX_ASSERT(stDark.saturatedRatio < 0.01f);

    std::vector<uint16_t> brightPx(64 * 64, 1020u);
    auto rawBright = packY10(brightPx, 64, 64, 128);
    auto stBright = preprocessIrFrame(rawBright.data(), rawBright.size(), 64, 64, 128, kPixFmtY10, Rotation::Deg0);
    SPX_ASSERT(stBright.saturatedRatio > 0.9f);

    std::array<uint8_t, 4> gray{10, 80, 160, 250};
    std::vector<uint8_t> rgba(2 * 4 * 4, 0);  // stride 4 pixels
    gray8ToRgba(gray.data(), 2, 2, rgba.data(), 4);
    SPX_ASSERT(rgba[0] == 10 && rgba[1] == 10 && rgba[2] == 10 && rgba[3] == 0xFF);
    SPX_ASSERT(rgba[4] == 80 && rgba[7] == 0xFF);
    SPX_ASSERT(rgba[4 * 4 + 0] == 160 && rgba[4 * 4 + 3] == 0xFF);
    SPX_ASSERT(rgba[4 * 4 + 4] == 250 && rgba[4 * 4 + 7] == 0xFF);

    // Portrait 480x640 IR preview rendering from 640x480 landscape source crop, mirrored.
    std::vector<uint8_t> src640x480(640u * 480u, 15u);
    src640x480[0u * 640u + 0u] = 99u;      // Outside center crop
    src640x480[0u * 640u + 80u] = 200u;    // Top-left of center 480x480 crop
    src640x480[0u * 640u + 559u] = 40u;    // Top-right of center 480x480 crop
    src640x480[479u * 640u + 80u] = 120u;  // Bottom-left of center 480x480 crop

    std::vector<uint8_t> prevRgba(640u * 512u * 4u, 77u);
    renderIrPreviewRgba(src640x480.data(), 640u, 480u, prevRgba.data(), 480u, 640u, 512u, true);
    SPX_ASSERT(prevRgba[0] == 40u && prevRgba[1] == 40u && prevRgba[2] == 40u && prevRgba[3] == 255u);
    const size_t topRightOff = 479u * 4u;
    SPX_ASSERT(prevRgba[topRightOff + 0] == 200u && prevRgba[topRightOff + 3] == 255u);
    const size_t botRightOff = (479u * 512u + 479u) * 4u;
    SPX_ASSERT(prevRgba[botRightOff + 0] == 120u && prevRgba[botRightOff + 3] == 255u);
    const size_t padRowOff = (500u * 512u + 10u) * 4u;
    SPX_ASSERT(prevRgba[padRowOff + 0] == 0u && prevRgba[padRowOff + 3] == 255u);

    std::vector<uint16_t> prev565(640u * 512u, 0xFFFFu);
    renderIrPreviewRgb565(src640x480.data(), 640u, 480u, prev565.data(), 480u, 640u, 512u, true);
    SPX_ASSERT(prev565[0] == ((5u << 11) | (10u << 5) | 5u));
    SPX_ASSERT(prev565[479] == ((25u << 11) | (50u << 5) | 25u));
    SPX_ASSERT(prev565[500u * 512u + 10u] == 0u);

    // Zoomed 1.35x center crop (cropSide=356, srcX0=142, srcY0=62) in 480x640 RGBA preview.
    src640x480[62u * 640u + 497u] = 177u;  // top-left after mirrorX
    src640x480[62u * 640u + 142u] = 211u;  // top-right after mirrorX
    src640x480[417u * 640u + 142u] = 155u; // bottom-right after mirrorX
    std::vector<uint8_t> prevZoomRgba(640u * 512u * 4u, 77u);
    renderIrPreviewRgba(
        src640x480.data(), 640u, 480u, prevZoomRgba.data(), 480u, 640u, 512u, true, 1.35f);
    SPX_ASSERT(prevZoomRgba[0] == 177u && prevZoomRgba[3] == 255u);
    SPX_ASSERT(prevZoomRgba[topRightOff + 0] == 211u && prevZoomRgba[topRightOff + 3] == 255u);
    SPX_ASSERT(prevZoomRgba[botRightOff + 0] == 155u && prevZoomRgba[botRightOff + 3] == 255u);
    SPX_ASSERT(prevZoomRgba[padRowOff + 0] == 0u && prevZoomRgba[padRowOff + 3] == 255u);

    // Enrollment progress bar color & overlay rendering in RGBA8888 and RGB565.
    const auto cProg0 = enrollProgressFillColor(0.0f);
    const auto cProgHalf = enrollProgressFillColor(0.5f);
    const auto cProgDone = enrollProgressFillColor(1.0f);
    const auto cProgClamp = enrollProgressFillColor(1.5f);
    SPX_ASSERT(cProg0.b > cProg0.r && cProg0.b > cProg0.g);
    SPX_ASSERT(cProgHalf.b > cProgHalf.r && cProgHalf.b > cProgHalf.g);
    SPX_ASSERT(cProgDone.g > cProgDone.r && cProgDone.g > cProgDone.b);
    SPX_ASSERT(cProgDone.r == cProgClamp.r && cProgDone.g == cProgClamp.g &&
               cProgDone.b == cProgClamp.b);

    std::vector<uint8_t> grayMid(640u * 480u, 128u);
    std::vector<uint8_t> barLegacy(640u * 512u * 4u, 0u);
    renderIrPreviewRgba(
        grayMid.data(), 640u, 480u, barLegacy.data(), 480u, 640u, 512u, true, 1.0f, -1.0f);
    const size_t scrimSampleOff = (447u * 512u + 20u) * 4u;
    const size_t barLeftOff = (447u * 512u + 120u) * 4u;
    const size_t barRightOff = (447u * 512u + 340u) * 4u;
    const size_t centerOff = (240u * 512u + 240u) * 4u;
    SPX_ASSERT(barLegacy[scrimSampleOff + 0] == 128u && barLegacy[scrimSampleOff + 1] == 128u &&
               barLegacy[scrimSampleOff + 2] == 128u);
    SPX_ASSERT(barLegacy[barLeftOff + 0] == 128u && barLegacy[barRightOff + 0] == 128u);

    std::vector<uint8_t> barRgba0(640u * 512u * 4u, 0u);
    renderIrPreviewRgba(
        grayMid.data(), 640u, 480u, barRgba0.data(), 480u, 640u, 512u, true, 1.0f, 0.0f);
    SPX_ASSERT(barRgba0[centerOff + 0] == 128u && barRgba0[centerOff + 1] == 128u &&
               barRgba0[centerOff + 2] == 128u);
    SPX_ASSERT(barRgba0[scrimSampleOff + 0] < 128u);
    SPX_ASSERT(barRgba0[barLeftOff + 0] == barRgba0[barLeftOff + 1] &&
               barRgba0[barLeftOff + 1] == barRgba0[barLeftOff + 2]);
    SPX_ASSERT(barRgba0[barLeftOff + 0] > barRgba0[scrimSampleOff + 0]);

    std::vector<uint8_t> barRgbaHalf(640u * 512u * 4u, 0u);
    renderIrPreviewRgba(
        grayMid.data(), 640u, 480u, barRgbaHalf.data(), 480u, 640u, 512u, true, 1.0f, 0.5f);
    SPX_ASSERT(barRgbaHalf[barLeftOff + 2] > barRgbaHalf[barLeftOff + 0] + 80u);
    SPX_ASSERT(barRgbaHalf[barRightOff + 0] == barRgbaHalf[barRightOff + 1] &&
               barRgbaHalf[barRightOff + 1] == barRgbaHalf[barRightOff + 2]);
    SPX_ASSERT(barRgbaHalf[barRightOff + 0] > barRgbaHalf[scrimSampleOff + 0]);
    SPX_ASSERT(barRgbaHalf[padRowOff + 0] == 0u && barRgbaHalf[padRowOff + 1] == 0u &&
               barRgbaHalf[padRowOff + 2] == 0u && barRgbaHalf[padRowOff + 3] == 255u);

    std::vector<uint8_t> barRgbaDone(640u * 512u * 4u, 0u);
    renderIrPreviewRgba(
        grayMid.data(), 640u, 480u, barRgbaDone.data(), 480u, 640u, 512u, true, 1.0f, 1.0f);
    SPX_ASSERT(barRgbaDone[barLeftOff + 1] > barRgbaDone[barLeftOff + 0] + 80u);
    SPX_ASSERT(barRgbaDone[barRightOff + 1] > barRgbaDone[barRightOff + 0] + 80u);

    auto to565 = [](Rgb8 c) -> uint16_t {
        return static_cast<uint16_t>(((static_cast<uint16_t>(c.r >> 3)) << 11) |
                                     ((static_cast<uint16_t>(c.g >> 2)) << 5) |
                                     static_cast<uint16_t>(c.b >> 3));
    };
    std::vector<uint16_t> bar565(640u * 512u, 0xFFFFu);
    renderIrPreviewRgb565(
        grayMid.data(), 640u, 480u, bar565.data(), 480u, 640u, 512u, true, 1.0f, 1.0f);
    SPX_ASSERT(bar565[447u * 512u + 120u] == to565(cProgDone));
    SPX_ASSERT(bar565[447u * 512u + 340u] == to565(cProgDone));
    SPX_ASSERT(bar565[500u * 512u + 10u] == 0u);

    // Same progress bar contract when source buffer is already 480x640 (e.g. rotated portrait/inverted).
    std::vector<uint8_t> grayMidPort(480u * 640u, 128u);
    std::vector<uint8_t> barPortRgba(640u * 512u * 4u, 0u);
    renderIrPreviewRgba(
        grayMidPort.data(), 480u, 640u, barPortRgba.data(), 480u, 640u, 512u, true, 1.0f, 0.5f);
    SPX_ASSERT(barPortRgba[centerOff + 0] == 128u && barPortRgba[centerOff + 3] == 255u);
    SPX_ASSERT(barPortRgba[scrimSampleOff + 0] < 128u);
    SPX_ASSERT(barPortRgba[barLeftOff + 2] > barPortRgba[barLeftOff + 0] + 80u);
    SPX_ASSERT(barPortRgba[padRowOff + 0] == 0u && barPortRgba[padRowOff + 3] == 255u);

    std::vector<uint16_t> barPort565(640u * 512u, 0xFFFFu);
    renderIrPreviewRgb565(
        grayMidPort.data(), 480u, 640u, barPort565.data(), 480u, 640u, 512u, true, 1.0f, 1.0f);
    SPX_ASSERT(barPort565[447u * 512u + 120u] == to565(cProgDone));
    SPX_ASSERT(barPort565[447u * 512u + 340u] == to565(cProgDone));
    SPX_ASSERT(barPort565[500u * 512u + 10u] == 0u);
}

void testCryptoAndHat() {
    using namespace spx::face;
    // SHA-256("abc")
    const auto d = sha256(reinterpret_cast<const uint8_t*>("abc"), 3);
    static constexpr std::array<uint8_t, 32> kShaAbc{
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
        0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
        0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    SPX_ASSERT(d == kShaAbc);

    // RFC 4231 Test Case 2
    const auto mac = hmacSha256(reinterpret_cast<const uint8_t*>("Jefe"), 4,
                                reinterpret_cast<const uint8_t*>("what do ya want for nothing?"), 28);
    static constexpr std::array<uint8_t, 32> kRfcMac{
        0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e, 0x6a, 0x04, 0x24,
        0x26, 0x08, 0x95, 0x75, 0xc7, 0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27,
        0x39, 0x83, 0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43,
    };
    SPX_ASSERT(mac == kRfcMac);

    const std::array<uint8_t, 32> ikm = kShaAbc;
    const auto k1 = hkdfSha256(nullptr, 0, ikm.data(), ikm.size(),
                               reinterpret_cast<const uint8_t*>("info-a"), 6);
    const auto k2 = hkdfSha256(nullptr, 0, ikm.data(), ikm.size(),
                               reinterpret_cast<const uint8_t*>("info-b"), 6);
    SPX_ASSERT(k1 != k2);
    SPX_ASSERT(k1 == hkdfSha256(nullptr, 0, ikm.data(), ikm.size(),
                                reinterpret_cast<const uint8_t*>("info-a"), 6));

    // ChaCha20 round-trip
    std::array<uint8_t, 12> nonce{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    std::vector<uint8_t> plain(97);
    for (size_t i = 0; i < plain.size(); ++i) plain[i] = static_cast<uint8_t>((i * 29u) & 0xFFu);
    std::vector<uint8_t> cipher(plain.size()), recovered(plain.size());
    chacha20Xor(k1, nonce, 1, plain.data(), cipher.data(), plain.size());
    SPX_ASSERT(cipher != plain);
    chacha20Xor(k1, nonce, 1, cipher.data(), recovered.data(), cipher.size());
    SPX_ASSERT(recovered == plain);
    SPX_ASSERT(constantTimeEqual(plain.data(), recovered.data(), plain.size()));

    // CSPRNG sanity
    std::array<uint8_t, 32> rnd1{}, rnd2{};
    SPX_ASSERT(csprngBytes(rnd1.data(), rnd1.size()));
    SPX_ASSERT(csprngBytes(rnd2.data(), rnd2.size()));
    SPX_ASSERT(rnd1 != rnd2);
    SPX_ASSERT(csprngInt64NonZero() != 0);

    // 37-byte hw_auth_token_t layout & MAC
    const int64_t ch = 0x1122334455667788LL;
    const int64_t uid = 0x0102030405060708LL;
    const int64_t authId = 0x090A0B0C0D0E0F10LL;
    const int64_t ts = 0x0000018E12345678LL;
    const auto wire = serializeAuthTokenPayload(ch, uid, authId, kAuthTypeBiometric, ts);
    SPX_ASSERT(wire.size() == 37);
    SPX_ASSERT(wire[0] == 0);
    // LE challenge starts at byte 1
    SPX_ASSERT(wire[1] == 0x88 && wire[8] == 0x11);
    // BE authenticatorType=2 at bytes 25..28
    SPX_ASSERT(wire[25] == 0 && wire[26] == 0 && wire[27] == 0 && wire[28] == 2);
    // BE timestamp starts at byte 29
    SPX_ASSERT(wire[36] == 0x78 && wire[35] == 0x56);

    const auto tokenMac = computeAuthTokenMac(k1, ch, uid, authId, kAuthTypeBiometric, ts);
    const auto expectedTokenMac = hmacSha256(k1.data(), k1.size(), wire.data(), wire.size());
    SPX_ASSERT(tokenMac == expectedTokenMac);

    std::set<int64_t> activeChallenges{ch};
    std::array<uint8_t, 32> zeroMac{};
    SPX_ASSERT(isPlausibleIncomingHat(tokenMac.data(), tokenMac.size(), ch, &activeChallenges));
    SPX_ASSERT(!isPlausibleIncomingHat(zeroMac.data(), zeroMac.size(), ch, &activeChallenges));
    SPX_ASSERT(!isPlausibleIncomingHat(tokenMac.data(), 16, ch, &activeChallenges));
    SPX_ASSERT(!isPlausibleIncomingHat(tokenMac.data(), tokenMac.size(), 999, &activeChallenges));
    SPX_ASSERT(!isPlausibleIncomingHat(tokenMac.data(), tokenMac.size(), 0, &activeChallenges));
}

void testEngineAndLiveness() {
    using namespace spx::face;

    const auto rawReal0 = makeSyntheticFaceY10(480, 640, SyntheticFaceVariant::Normal3D, 0.0f, 0);
    const auto stReal0 = preprocessIrFrame(
        rawReal0.data(), rawReal0.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto det0 = detectFaceNir(stReal0);
    SPX_ASSERT(det0.detected);
    SPX_ASSERT(det0.quality == FaceQuality::GOOD);
    const auto emb0 = extractEmbedding(stReal0, det0);

    // L2 normalized
    double sq = 0.0;
    for (float v : emb0) sq += static_cast<double>(v) * v;
    SPX_ASSERT(std::abs(sq - 1.0) < 1e-4);
    SPX_ASSERT(cosineSimilarity(emb0, emb0) > 0.999f);

    // Jittered frame of same identity with slight sensor noise
    const auto rawReal1 = makeSyntheticFaceY10(480, 640, SyntheticFaceVariant::Normal3D, 2.0f, 1);
    const auto stReal1 = preprocessIrFrame(
        rawReal1.data(), rawReal1.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto det1 = detectFaceNir(stReal1);
    SPX_ASSERT(det1.detected);
    const auto emb1 = extractEmbedding(stReal1, det1);
    SPX_ASSERT(cosineSimilarity(emb0, emb1) >= 0.88f);

    // Structurally distinct face must be rejected below threshold
    const auto rawOther = makeSyntheticFaceY10(480, 640, SyntheticFaceVariant::DistinctPerson, 0.0f, 0);
    const auto stOther = preprocessIrFrame(
        rawOther.data(), rawOther.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto detOther = detectFaceNir(stOther);
    SPX_ASSERT(detOther.detected);
    const auto embOther = extractEmbedding(stOther, detOther);
    SPX_ASSERT(cosineSimilarity(emb0, embOther) < kDefaultMatchThreshold);

    // Layer 1: dark LCD/OLED phone screen under 850nm NIR
    const auto rawScreen = makeSyntheticFaceY10(480, 640, SyntheticFaceVariant::ScreenSpoof);
    const auto stScreen = preprocessIrFrame(
        rawScreen.data(), rawScreen.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto detScreen = detectFaceNir(stScreen);
    if (detScreen.detected) {
        const auto embScreen = extractEmbedding(stScreen, detScreen);
        const auto liveScreen = evaluateLiveness(stScreen, detScreen, embScreen, nullptr, false);
        SPX_ASSERT(!liveScreen.live);
    }

    // Layer 2: flat print spoof lacking 3D nose-to-cheek relief
    const auto rawFlat = makeSyntheticFaceY10(480, 640, SyntheticFaceVariant::FlatPrint);
    const auto stFlat = preprocessIrFrame(
        rawFlat.data(), rawFlat.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto detFlat = detectFaceNir(stFlat);
    SPX_ASSERT(detFlat.detected);
    const auto embFlat = extractEmbedding(stFlat, detFlat);
    const auto liveFlat = evaluateLiveness(stFlat, detFlat, embFlat, nullptr, false);
    SPX_ASSERT(!liveFlat.live);
    SPX_ASSERT(liveFlat.status == LivenessStatus::REJECT_FLAT_OR_PRINT);

    // Layer 3: frozen replay rejected when identical frame repeats, live frames with noise pass
    FaceSessionAccumulator acc;
    acc.reset(2);
    const auto lFirst = evaluateLiveness(stReal0, det0, emb0, &acc, true);
    SPX_ASSERT(!lFirst.live);
    SPX_ASSERT(lFirst.status == LivenessStatus::NEED_MORE_FRAMES);

    const auto lFrozen = evaluateLiveness(stReal0, det0, emb0, &acc, true);
    SPX_ASSERT(!lFrozen.live);
    SPX_ASSERT(lFrozen.status == LivenessStatus::REJECT_FROZEN_REPLAY);

    acc.reset(2);
    SPX_ASSERT(evaluateLiveness(stReal0, det0, emb0, &acc, true).status == LivenessStatus::NEED_MORE_FRAMES);
    const auto lPass = evaluateLiveness(stReal1, det1, emb1, &acc, true);
    SPX_ASSERT(lPass.live);
    SPX_ASSERT(lPass.status == LivenessStatus::PASS);

    // OptionalModelWeights missing/corrupt handling
    OptionalModelWeights weights;
    SPX_ASSERT(!weights.loadFromFile("/tmp/nonexistent-spx-model-weights.bin"));
    SPX_ASSERT(!weights.loaded);

    // Symmetric non-face patterns and 90°/180° rotated faces must not trigger false detections.
    std::vector<uint16_t> stripesPx(480u * 640u, 0u);
    std::vector<uint16_t> vignettePx(480u * 640u, 0u);
    for (unsigned y = 0; y < 640u; ++y) {
        const float ny = (static_cast<float>(y) - 320.0f) / 320.0f;
        const uint16_t sv = static_cast<uint16_t>(180u + ((y / 24u) & 1u) * 320u);
        for (unsigned x = 0; x < 480u; ++x) {
            stripesPx[y * 480u + x] = sv;
            const float nx = (static_cast<float>(x) - 240.0f) / 240.0f;
            const float r2 = std::min(1.0f, nx * nx + ny * ny);
            vignettePx[y * 480u + x] = static_cast<uint16_t>(700.0f * (1.0f - 0.7f * r2));
        }
    }
    const auto rawStripes = packY10(stripesPx, 480u, 640u, 960u);
    const auto stStripes = preprocessIrFrame(
        rawStripes.data(), rawStripes.size(), 480u, 640u, 960u, kPixFmtY10, Rotation::Deg0);
    SPX_ASSERT(!detectFaceNir(stStripes).detected);

    const auto rawVig = packY10(vignettePx, 480u, 640u, 960u);
    const auto stVig = preprocessIrFrame(
        rawVig.data(), rawVig.size(), 480u, 640u, 960u, kPixFmtY10, Rotation::Deg0);
    SPX_ASSERT(!detectFaceNir(stVig).detected);

    const auto stRot90 = preprocessIrFrame(
        rawReal0.data(), rawReal0.size(), 480u, 640u, 960u, kPixFmtY10, Rotation::Deg90CW);
    SPX_ASSERT(!detectFaceNir(stRot90).detected);

    const auto stRot180 = preprocessIrFrame(
        rawReal0.data(), rawReal0.size(), 480u, 640u, 960u, kPixFmtY10, Rotation::Deg180);
    SPX_ASSERT(!detectFaceNir(stRot180).detected);

    // Realistic 850nm NIR face at ~65cm distance with soft lip contrast & flat nose bridge.
    const auto rawNir65 = makeSyntheticFaceY10(
        480, 640, SyntheticFaceVariant::SmallSoft850Nir65Cm, 0.0f, 1);
    const auto stNir65 = preprocessIrFrame(
        rawNir65.data(), rawNir65.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto detNir65 = detectFaceNir(stNir65);
    SPX_ASSERT(detNir65.detected);
    SPX_ASSERT(detNir65.quality == FaceQuality::GOOD);
    const auto embNir65 = extractEmbedding(stNir65, detNir65);
    const auto liveNir65 = evaluateLiveness(stNir65, detNir65, embNir65, nullptr, false);
    SPX_ASSERT(liveNir65.live);
    SPX_ASSERT(liveNir65.status == LivenessStatus::PASS);

    // 2-eye alignment & scale/exposure invariance across shift (+10px), gain (0.6x/1.4x),
    // while keeping a distinct synthetic identity well separated.
    auto scaleY10 = [](const std::vector<uint8_t>& src, float gain) {
        std::vector<uint8_t> out(src.size());
        for (size_t i = 0; i + 1 < src.size(); i += 2) {
            const uint16_t v = static_cast<uint16_t>(src[i]) |
                               (static_cast<uint16_t>(src[i + 1]) << 8);
            const uint16_t scaled = static_cast<uint16_t>(
                std::clamp(static_cast<int>(std::lround(v * gain)), 0, 1023));
            out[i] = static_cast<uint8_t>(scaled & 0xffu);
            out[i + 1] = static_cast<uint8_t>((scaled >> 8) & 0x03u);
        }
        return out;
    };

    const auto rawBase = makeSyntheticFaceY10(
        480, 640, SyntheticFaceVariant::Normal3D, 0.0f, 0);
    const auto stBase = preprocessIrFrame(
        rawBase.data(), rawBase.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto detBase = detectFaceNir(stBase);
    SPX_ASSERT(detBase.detected);
    const auto embBase = extractEmbedding(stBase, detBase);

    const auto rawShift = makeSyntheticFaceY10(
        480, 640, SyntheticFaceVariant::Normal3D, 10.0f, 1);
    const auto stShift = preprocessIrFrame(
        rawShift.data(), rawShift.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto detShift = detectFaceNir(stShift);
    SPX_ASSERT(detShift.detected);
    const auto embShift = extractEmbedding(stShift, detShift);
    SPX_ASSERT(cosineSimilarity(embBase, embShift) >= kDefaultMatchThreshold);

    const auto rawDark = scaleY10(rawBase, 0.6f);
    const auto stDark = preprocessIrFrame(
        rawDark.data(), rawDark.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto detDark = detectFaceNir(stDark);
    SPX_ASSERT(detDark.detected);
    const auto embDark = extractEmbedding(stDark, detDark);
    SPX_ASSERT(cosineSimilarity(embBase, embDark) >= kDefaultMatchThreshold);

    const auto rawBright = scaleY10(rawShift, 1.4f);
    const auto stBright = preprocessIrFrame(
        rawBright.data(), rawBright.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto detBright = detectFaceNir(stBright);
    SPX_ASSERT(detBright.detected);
    const auto embBright = extractEmbedding(stBright, detBright);
    SPX_ASSERT(cosineSimilarity(embBase, embBright) >= kDefaultMatchThreshold);

    const auto rawDistinct = makeSyntheticFaceY10(
        480, 640, SyntheticFaceVariant::DistinctPerson, 0.0f, 0);
    const auto stDistinct = preprocessIrFrame(
        rawDistinct.data(), rawDistinct.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto detDistinct = detectFaceNir(stDistinct);
    SPX_ASSERT(detDistinct.detected);
    const auto embDistinct = extractEmbedding(stDistinct, detDistinct);
    SPX_ASSERT(cosineSimilarity(embBase, embDistinct) < kDefaultMatchThreshold);

    // Distant ~75cm NIR face (boxW ~ 72px) with a bright background specular corner (p99 ~ 930)
    // crushing linear 8-bit face contrast.
    auto makeDistantGlareFaceY10 = [](float dxShiftPx, int noisePhase) {
        constexpr unsigned kW = 480u;
        constexpr unsigned kH = 640u;
        std::vector<uint16_t> px(static_cast<size_t>(kW) * kH, 24u);
        const float cx = kW * 0.50f + dxShiftPx;
        const float cy = kH * 0.48f;
        const float rx = kW * 0.075f;  // diameter ~ 72px (~15% of 480px minDim)
        const float ry = kH * 0.090f;
        for (unsigned y = 0; y < kH; ++y) {
            for (unsigned x = 0; x < kW; ++x) {
                if (x >= 72u && x < 145u && y >= 76u && y < 125u) {
                    px[static_cast<size_t>(y) * kW + x] = 930u;
                    continue;
                }
                const float dx = (static_cast<float>(x) - cx) / rx;
                const float dy = (static_cast<float>(y) - cy) / ry;
                const float r2 = dx * dx + dy * dy;
                float val = 24.0f;
                if (r2 <= 1.0f) {
                    const float z = std::sqrt(std::max(0.0f, 1.0f - r2));
                    val = 150.0f + 170.0f * z;
                    const float le = (dx + 0.38f) * (dx + 0.38f) / 0.04f +
                                     (dy + 0.26f) * (dy + 0.26f) / 0.028f;
                    const float re = (dx - 0.38f) * (dx - 0.38f) / 0.04f +
                                     (dy + 0.26f) * (dy + 0.26f) / 0.028f;
                    if (le < 1.0f || re < 1.0f) {
                        val *= 0.42f;
                    }
                    const float mouth = (dx * dx) / 0.14f + (dy - 0.48f) * (dy - 0.48f) / 0.015f;
                    if (mouth < 1.0f) {
                        val *= 0.90f;
                    }
                }
                const int tex = static_cast<int>(((x * 7u + y * 13u) & 7u));
                const int temporal = (noisePhase != 0 && r2 <= 1.0f)
                                         ? (static_cast<int>(((x + y + noisePhase * 3u) % 9u)) * 3 - 12)
                                         : 0;
                px[static_cast<size_t>(y) * kW + x] = static_cast<uint16_t>(
                    std::clamp(std::lround(val + tex + temporal), 0L, 1023L));
            }
        }
        return packY10(px, kW, kH, kW * 2u);
    };

    const auto rawDist0 = makeDistantGlareFaceY10(0.0f, 0);
    const auto stDist0 = preprocessIrFrame(
        rawDist0.data(), rawDist0.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    SPX_ASSERT(stDist0.p99 >= 900u && stDist0.p50 < 100u);
    const auto detDist0 = detectFaceNir(stDist0);
    SPX_ASSERT(detDist0.detected);
    SPX_ASSERT(detDist0.quality == FaceQuality::GOOD);
    const auto embDist0 = extractEmbedding(stDist0, detDist0);

    const auto rawDist1 = makeDistantGlareFaceY10(6.0f, 1);
    const auto stDist1 = preprocessIrFrame(
        rawDist1.data(), rawDist1.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto detDist1 = detectFaceNir(stDist1);
    SPX_ASSERT(detDist1.detected);
    SPX_ASSERT(detDist1.quality == FaceQuality::GOOD);
    const auto embDist1 = extractEmbedding(stDist1, detDist1);
    SPX_ASSERT(cosineSimilarity(embDist0, embDist1) >= kDefaultMatchThreshold);

    FaceSessionAccumulator accDist;
    accDist.reset(2);
    SPX_ASSERT(evaluateLiveness(stDist0, detDist0, embDist0, &accDist, true).status ==
               LivenessStatus::NEED_MORE_FRAMES);
    const auto liveDist1 = evaluateLiveness(stDist1, detDist1, embDist1, &accDist, true);
    SPX_ASSERT(liveDist1.live);
    SPX_ASSERT(liveDist1.status == LivenessStatus::PASS);

    // Dark bangs covering forehead + left-to-right off-axis IR gradient (0.85x..1.15x).
    std::vector<uint8_t> rawBangs = rawBase;
    for (unsigned y = 0; y < 640u; ++y) {
        const float fdy = (static_cast<float>(y) - 640.0f * 0.48f) / (640.0f * 0.28f);
        for (unsigned x = 0; x < 480u; ++x) {
            const size_t off = (static_cast<size_t>(y) * 480u + x) * 2u;
            uint16_t v = static_cast<uint16_t>(rawBangs[off]) |
                         (static_cast<uint16_t>(rawBangs[off + 1]) << 8);
            const float fdx = (static_cast<float>(x) - 480.0f * 0.50f) / (480.0f * 0.24f);
            if (fdx * fdx + fdy * fdy <= 1.0f && fdy < -0.42f) {
                v = 45u;
            }
            const float shade = 0.85f + 0.30f * (static_cast<float>(x) / 480.0f);
            const uint16_t sv = static_cast<uint16_t>(
                std::clamp(static_cast<int>(std::lround(v * shade)), 0, 1023));
            rawBangs[off] = static_cast<uint8_t>(sv & 0xffu);
            rawBangs[off + 1] = static_cast<uint8_t>((sv >> 8) & 0x03u);
        }
    }
    const auto stBangs = preprocessIrFrame(
        rawBangs.data(), rawBangs.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto detBangs = detectFaceNir(stBangs);
    SPX_ASSERT(detBangs.detected);
    SPX_ASSERT(detBangs.quality == FaceQuality::GOOD);
    const auto embBangs = extractEmbedding(stBangs, detBangs);
    const auto liveBangs = evaluateLiveness(stBangs, detBangs, embBangs, nullptr, false);
    SPX_ASSERT(liveBangs.live);
}

void testFaceStore() {
    using namespace spx::face;
    const std::string root = makeTempDir("spx-face-store-");
    const std::string user0Dir = root + "/u0";
    const std::string user10Dir = root + "/u10";

    const auto rawReal = makeSyntheticFaceY10(480, 640, SyntheticFaceVariant::Normal3D, 0.0f, 0);
    const auto stReal = preprocessIrFrame(
        rawReal.data(), rawReal.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto det = detectFaceNir(stReal);
    const auto embReal = extractEmbedding(stReal, det);

    const auto rawOther = makeSyntheticFaceY10(480, 640, SyntheticFaceVariant::DistinctPerson, 0.0f, 0);
    const auto stOther = preprocessIrFrame(
        rawOther.data(), rawOther.size(), 480, 640, 960, kPixFmtY10, Rotation::Deg0);
    const auto embOther = extractEmbedding(stOther, detectFaceNir(stOther));

    int64_t savedAuthId = 0;
    int32_t id1 = 0, id2 = 0;
    {
        SpxFaceStore store0(0, user0Dir);
        SPX_ASSERT(store0.load());
        SPX_ASSERT(store0.hasFeatureBit(1u));
        SPX_ASSERT(!store0.hasFeatureBit(2u));
        SPX_ASSERT(store0.enrollmentCount() == 0);
        id1 = store0.addEnrollment({embReal});
        SPX_ASSERT(id1 > 0);
        id2 = store0.addEnrollment({embReal});
        SPX_ASSERT(id2 > id1);
        // Max enrollments per user enforced (kMaxEnrollmentsPerUser == 2)
        SPX_ASSERT(store0.addEnrollment({embReal}) == -1);
        savedAuthId = store0.authenticatorId();
        SPX_ASSERT(savedAuthId != 0);
        SPX_ASSERT(store0.save());
    }

    // Permissions check: dir 0700, key & encrypted store 0600
    struct stat st{};
    SPX_ASSERT(stat(user0Dir.c_str(), &st) == 0);
    SPX_ASSERT((st.st_mode & 0777) == 0700);
    SPX_ASSERT(stat((user0Dir + "/spx_face_v1.bin").c_str(), &st) == 0);
    SPX_ASSERT((st.st_mode & 0777) == 0600);
    SPX_ASSERT(stat((user0Dir + "/spx_face_key_v1.bin").c_str(), &st) == 0);
    SPX_ASSERT((st.st_mode & 0777) == 0600);

    // Reload + match + per-user isolation + invalidation
    {
        SpxFaceStore store0(0, user0Dir);
        SPX_ASSERT(store0.load());
        SPX_ASSERT(store0.enrollmentCount() == 2);
        SPX_ASSERT(store0.authenticatorId() == savedAuthId);
        const auto mGood = store0.match(embReal);
        SPX_ASSERT(mGood.matched && mGood.enrollmentId > 0);
        SPX_ASSERT(!store0.match(embOther).matched);

        const auto prevAuthId = store0.authenticatorId();
        SPX_ASSERT(store0.invalidateAuthenticatorId() != prevAuthId);

        const auto removed = store0.removeEnrollments({id1});
        SPX_ASSERT(removed.size() == 1 && removed[0] == id1);
        SPX_ASSERT(store0.enrollmentCount() == 1);
        SPX_ASSERT(store0.save());

        SpxFaceStore store10(10, user10Dir);
        SPX_ASSERT(store10.load());
        SPX_ASSERT(store10.enrollmentCount() == 0);
    }

    // Tamper 1 byte of ciphertext -> HMAC check must reject load()
    {
        const std::string dataFile = user0Dir + "/spx_face_v1.bin";
        int fd = open(dataFile.c_str(), O_RDWR);
        SPX_ASSERT(fd >= 0);
        SPX_ASSERT(lseek(fd, 60, SEEK_SET) == 60);
        uint8_t byte = 0;
        SPX_ASSERT(read(fd, &byte, 1) == 1);
        byte ^= 0x55u;
        SPX_ASSERT(lseek(fd, 60, SEEK_SET) == 60);
        SPX_ASSERT(write(fd, &byte, 1) == 1);
        close(fd);

        SpxFaceStore tampered(0, user0Dir);
        SPX_ASSERT(!tampered.load());

        // Restore valid wipe path
        tampered.wipeAll();
        SpxFaceStore clean(0, user0Dir);
        SPX_ASSERT(clean.load());
        SPX_ASSERT(clean.enrollmentCount() == 0);

        // Construct a valid-MAC v1 blob -> load() must reject stale kStoreVersion=1 templates
        SPX_ASSERT(clean.addEnrollment({embReal}) > 0);
        auto readFileBytes = [](const std::string& p) {
            int rfd = open(p.c_str(), O_RDONLY);
            SPX_ASSERT(rfd >= 0);
            struct stat fst{};
            SPX_ASSERT(fstat(rfd, &fst) == 0 && fst.st_size > 0);
            std::vector<uint8_t> out(static_cast<size_t>(fst.st_size));
            size_t off = 0;
            while (off < out.size()) {
                ssize_t n = read(rfd, out.data() + off, out.size() - off);
                SPX_ASSERT(n > 0);
                off += static_cast<size_t>(n);
            }
            close(rfd);
            return out;
        };
        const auto masterKey = readFileBytes(user0Dir + "/spx_face_key_v1.bin");
        SPX_ASSERT(masterKey.size() == 32u);
        auto v3Blob = readFileBytes(dataFile);
        SPX_ASSERT(v3Blob.size() >= 84u);
        SPX_ASSERT(v3Blob[8] == 3u && v3Blob[9] == 0u && v3Blob[10] == 0u && v3Blob[11] == 0u);
        const size_t bodyEnd = v3Blob.size() - 32u;
        const auto macKey = hkdfSha256(nullptr, 0, masterKey.data(), masterKey.size(),
                                       reinterpret_cast<const uint8_t*>("spx_face_mac_v1"), 15);
        for (uint8_t staleVer : {uint8_t{2u}, uint8_t{1u}}) {
            v3Blob[8] = staleVer;  // downgrade version header while keeping valid MAC
            const auto newMac = hmacSha256(macKey.data(), macKey.size(), v3Blob.data(), bodyEnd);
            std::memcpy(v3Blob.data() + bodyEnd, newMac.data(), 32u);
            {
                int wfd = open(dataFile.c_str(), O_WRONLY | O_TRUNC);
                SPX_ASSERT(wfd >= 0);
                size_t off = 0;
                while (off < v3Blob.size()) {
                    ssize_t n = write(wfd, v3Blob.data() + off, v3Blob.size() - off);
                    SPX_ASSERT(n > 0);
                    off += static_cast<size_t>(n);
                }
                close(wfd);
            }
            SpxFaceStore staleStore(0, user0Dir);
            SPX_ASSERT(!staleStore.load());
        }
        SpxFaceStore wipeTarget(0, user0Dir);
        wipeTarget.wipeAll();

        SpxFaceStore rebuilt(0, user0Dir);
        SPX_ASSERT(rebuilt.load());
        SPX_ASSERT(rebuilt.addEnrollment({embReal}) > 0);
        SpxFaceStore reloaded(0, user0Dir);
        SPX_ASSERT(reloaded.load());
        SPX_ASSERT(reloaded.enrollmentCount() == 1);
    }

    removeTree(root);
}

void testLockoutTracker() {
    using namespace spx::face;
    int64_t nowMs = 10000;
    SpxLockoutTracker tracker([&]() { return nowMs; });

    for (int i = 1; i <= 4; ++i) {
        SPX_ASSERT(tracker.addFailedAttempt().mode == LockoutMode::NONE);
        SPX_ASSERT(tracker.failedAttempts() == static_cast<uint32_t>(i));
    }
    auto s5 = tracker.addFailedAttempt();
    SPX_ASSERT(s5.mode == LockoutMode::TIMED && s5.remainingMs == 30000);
    nowMs += 15000;
    SPX_ASSERT(tracker.checkState().mode == LockoutMode::TIMED);
    SPX_ASSERT(tracker.checkState().remainingMs == 15000);
    nowMs += 16000;
    SPX_ASSERT(tracker.checkState().mode == LockoutMode::NONE);

    // 10th and 15th attempts also trigger 30s TIMED lockout
    for (int attempt = 6; attempt <= 19; ++attempt) {
        auto st = tracker.addFailedAttempt();
        if (attempt == 10 || attempt == 15) {
            SPX_ASSERT(st.mode == LockoutMode::TIMED);
            nowMs += 31000;
            SPX_ASSERT(tracker.checkState().mode == LockoutMode::NONE);
        } else {
            SPX_ASSERT(st.mode == LockoutMode::NONE);
        }
    }

    auto s20 = tracker.addFailedAttempt();
    SPX_ASSERT(s20.mode == LockoutMode::PERMANENT);
    nowMs += 36000000LL;
    SPX_ASSERT(tracker.checkState().mode == LockoutMode::PERMANENT);

    tracker.reset();
    SPX_ASSERT(tracker.failedAttempts() == 0);
    SPX_ASSERT(tracker.checkState().mode == LockoutMode::NONE);
}

void testMultiOrientationSupport() {
    using namespace spx::face;

    // 1. rotationToDegrees, composeRotation, and bit-for-bit rotateIrFrameStats across all 16 pairs.
    static constexpr std::array<Rotation, 4> kRots{
        Rotation::Deg0,
        Rotation::Deg90CW,
        Rotation::Deg180,
        Rotation::Deg270CW,
    };
    constexpr unsigned kW = 8;
    constexpr unsigned kH = 4;
    std::vector<uint16_t> rampPx(kW * kH);
    for (unsigned y = 0; y < kH; ++y) {
        for (unsigned x = 0; x < kW; ++x) {
            rampPx[y * kW + x] = static_cast<uint16_t>(40u + y * 130u + x * 29u);
        }
    }
    const auto rampRaw = packY10(rampPx, kW, kH, kW * 2u);

    for (Rotation base : kRots) {
        const auto baseStats =
            preprocessIrFrame(rampRaw.data(), rampRaw.size(), kW, kH, kW * 2u, kPixFmtY10, base);
        for (Rotation delta : kRots) {
            const int expectedDeg = (rotationToDegrees(base) + rotationToDegrees(delta)) % 360;
            const Rotation composed = composeRotation(base, delta);
            SPX_ASSERT(rotationToDegrees(composed) == expectedDeg);

            const auto rotStats = rotateIrFrameStats(baseStats, delta);
            const auto direct = preprocessIrFrame(
                rampRaw.data(), rampRaw.size(), kW, kH, kW * 2u, kPixFmtY10, composed);
            SPX_ASSERT(rotStats.width == direct.width);
            SPX_ASSERT(rotStats.height == direct.height);
            SPX_ASSERT(rotStats.upright10 == direct.upright10);
            if (delta == Rotation::Deg0) {
                SPX_ASSERT(rotStats.gray8 == direct.gray8);
            }
            const auto roundTrip = rotateIrFrameStats(rotStats, [&] {
                switch (delta) {
                case Rotation::Deg0:
                    return Rotation::Deg0;
                case Rotation::Deg90CW:
                    return Rotation::Deg270CW;
                case Rotation::Deg180:
                    return Rotation::Deg180;
                case Rotation::Deg270CW:
                    return Rotation::Deg90CW;
                }
                return Rotation::Deg0;
            }());
            SPX_ASSERT(roundTrip.width == baseStats.width && roundTrip.height == baseStats.height);
            SPX_ASSERT(roundTrip.upright10 == baseStats.upright10);
            SPX_ASSERT(roundTrip.gray8 == baseStats.gray8);
            SPX_ASSERT(rotStats.p1 == baseStats.p1 && rotStats.p50 == baseStats.p50 &&
                       rotStats.p99 == baseStats.p99);
            SPX_ASSERT(std::abs(rotStats.roiLinearMean - baseStats.roiLinearMean) < 1e-6f);
            SPX_ASSERT(std::abs(rotStats.darkRatio - baseStats.darkRatio) < 1e-6f);
            SPX_ASSERT(std::abs(rotStats.saturatedRatio - baseStats.saturatedRatio) < 1e-6f);
        }
    }

    // 2. Helper to rotate packed Y10 frames so we can simulate holding the device in all 4 angles.
    auto rotateRawY10Cw = [](const std::vector<uint8_t>& srcRaw,
                             unsigned srcW,
                             unsigned srcH,
                             Rotation rot,
                             unsigned& outW,
                             unsigned& outH) -> std::vector<uint8_t> {
        const auto st = preprocessIrFrame(
            srcRaw.data(), srcRaw.size(), srcW, srcH, srcW * 2u, kPixFmtY10, rot);
        outW = st.width;
        outH = st.height;
        return packY10(st.upright10, outW, outH, outW * 2u);
    };
    auto inverseCw = [](Rotation rot) -> Rotation {
        switch (rot) {
        case Rotation::Deg0:
            return Rotation::Deg0;
        case Rotation::Deg90CW:
            return Rotation::Deg270CW;
        case Rotation::Deg180:
            return Rotation::Deg180;
        case Rotation::Deg270CW:
            return Rotation::Deg90CW;
        }
        return Rotation::Deg0;
    };

    const auto uprightFace =
        makeSyntheticFaceY10(480u, 640u, SyntheticFaceVariant::Normal3D, 0.0f, 0);

    // Enroll at Deg270CW (device normal landscape posture).
    const std::string storeRoot = makeTempDir("spx-face-4rot-");
    SpxFaceStore store(0, storeRoot + "/u0");
    SPX_ASSERT(store.load());
    {
        unsigned rw = 0, rh = 0;
        const auto raw270 = rotateRawY10Cw(uprightFace, 480u, 640u, Rotation::Deg90CW, rw, rh);
        SPX_ASSERT(rw == 640u && rh == 480u);
        const auto stEnr = preprocessIrFrame(
            raw270.data(), raw270.size(), rw, rh, rw * 2u, kPixFmtY10, Rotation::Deg270CW);
        const auto detEnr = detectFaceNir(stEnr);
        SPX_ASSERT(detEnr.detected && detEnr.quality == FaceQuality::GOOD);
        const auto embEnr = extractEmbedding(stEnr, detEnr);
        SPX_ASSERT(store.addEnrollment({embEnr}) > 0);
    }

    // Authenticate from Deg270CW starting posture across all 4 physical holding orientations.
    for (Rotation expectedRot : kRots) {
        unsigned rw = 0, rh = 0;
        const auto sensorRaw =
            rotateRawY10Cw(uprightFace, 480u, 640u, inverseCw(expectedRot), rw, rh);
        const auto baseStats = preprocessIrFrame(
            sensorRaw.data(), sensorRaw.size(), rw, rh, rw * 2u, kPixFmtY10, Rotation::Deg270CW);
        const auto mo = detectFaceNirMultiOrientation(
            baseStats, Rotation::Deg270CW, true, false);
        SPX_ASSERT(mo.detection.detected);
        SPX_ASSERT(mo.rotation == expectedRot);
        SPX_ASSERT(mo.usedRotatedStats == (expectedRot != Rotation::Deg270CW));

        const auto& useStats = mo.usedRotatedStats ? mo.rotatedStats : baseStats;
        const auto embProbe = extractEmbedding(useStats, mo.detection);
        const auto m = store.match(embProbe, kDefaultMatchThreshold);
        SPX_ASSERT(m.matched);
        SPX_ASSERT(m.bestSimilarity >= kDefaultMatchThreshold);

        if (expectedRot != Rotation::Deg270CW) {
            const auto disabledMo = detectFaceNirMultiOrientation(
                baseStats, Rotation::Deg270CW, false, false);
            SPX_ASSERT(!disabledMo.detection.detected);
            SPX_ASSERT(disabledMo.rotation == Rotation::Deg270CW);
            SPX_ASSERT(!disabledMo.usedRotatedStats);
        }
    }

    // 3. TOO_DARK / TOO_BRIGHT short-circuits without searching alternate orientations.
    {
        std::vector<uint16_t> darkPx(64u * 64u, 5u);
        const auto darkRaw = packY10(darkPx, 64u, 64u, 128u);
        const auto darkStats = preprocessIrFrame(
            darkRaw.data(), darkRaw.size(), 64u, 64u, 128u, kPixFmtY10, Rotation::Deg270CW);
        const auto moDark = detectFaceNirMultiOrientation(
            darkStats, Rotation::Deg270CW, true, true);
        SPX_ASSERT(!moDark.detection.detected);
        SPX_ASSERT(moDark.detection.quality == FaceQuality::TOO_DARK);
        SPX_ASSERT(moDark.rotation == Rotation::Deg270CW);
        SPX_ASSERT(!moDark.usedRotatedStats);

        std::vector<uint16_t> brightPx(64u * 64u, 1020u);
        const auto brightRaw = packY10(brightPx, 64u, 64u, 128u);
        const auto brightStats = preprocessIrFrame(
            brightRaw.data(), brightRaw.size(), 64u, 64u, 128u, kPixFmtY10, Rotation::Deg270CW);
        const auto moBright = detectFaceNirMultiOrientation(
            brightStats, Rotation::Deg270CW, true, true);
        SPX_ASSERT(!moBright.detection.detected);
        SPX_ASSERT(moBright.detection.quality == FaceQuality::TOO_BRIGHT);
        SPX_ASSERT(moBright.rotation == Rotation::Deg270CW);
        SPX_ASSERT(!moBright.usedRotatedStats);
    }

    // 4. probeAlternateOrientationMatch recovers true matching orientation after low baseSimilarity.
    {
        unsigned rw = 0, rh = 0;
        const auto raw90 =
            rotateRawY10Cw(uprightFace, 480u, 640u, Rotation::Deg270CW, rw, rh);
        const auto wrongBaseStats = preprocessIrFrame(
            raw90.data(), raw90.size(), rw, rh, rw * 2u, kPixFmtY10, Rotation::Deg270CW);
        const auto hint = probeAlternateOrientationMatch(
            wrongBaseStats, Rotation::Deg270CW, 0.25f, store, nullptr, kDefaultMatchThreshold);
        SPX_ASSERT(hint.shouldSwitch);
        SPX_ASSERT(hint.rotation == Rotation::Deg90CW);
        SPX_ASSERT(hint.bestSimilarity >= kDefaultMatchThreshold);
        SPX_ASSERT(hint.detScore >= 42.0f);

        // Distinct unrelated face already upright at Deg270CW OR rotated (even with baseSimilarity = 0.0f
        // due to transient liveness rejection) must NOT trigger a spurious orientation switch.
        const auto distinctFace =
            makeSyntheticFaceY10(480u, 640u, SyntheticFaceVariant::DistinctPerson, 0.0f, 0);
        const auto distinctRaw270 =
            rotateRawY10Cw(distinctFace, 480u, 640u, Rotation::Deg90CW, rw, rh);
        const auto distinctBase = preprocessIrFrame(
            distinctRaw270.data(), distinctRaw270.size(), rw, rh, rw * 2u, kPixFmtY10, Rotation::Deg270CW);
        const auto distinctDet = detectFaceNir(distinctBase);
        SPX_ASSERT(distinctDet.detected);
        const float distinctSim =
            store.match(extractEmbedding(distinctBase, distinctDet), kDefaultMatchThreshold).bestSimilarity;
        SPX_ASSERT(distinctSim < kDefaultMatchThreshold);
        const auto noHint = probeAlternateOrientationMatch(
            distinctBase, Rotation::Deg270CW, distinctSim, store, nullptr, kDefaultMatchThreshold);
        SPX_ASSERT(!noHint.shouldSwitch);

        const auto distinctRotatedStats = preprocessIrFrame(
            distinctRaw270.data(), distinctRaw270.size(), rw, rh, rw * 2u, kPixFmtY10, Rotation::Deg0);
        const auto noHintZeroBase = probeAlternateOrientationMatch(
            distinctRotatedStats, Rotation::Deg0, 0.0f, store, nullptr, kDefaultMatchThreshold);
        SPX_ASSERT(!noHintZeroBase.shouldSwitch);
    }

    // 5. classifyGravityOrientation across all 4 postures, flat desk, hysteresis deadband, and non-finite inputs.
    {
        Rotation out = Rotation::Deg180;
        // Live SPX landscape kickstand sample: (0.16, 8.53, 4.92) -> Deg0 -> 270 + 0 = 270.
        SPX_ASSERT(classifyGravityOrientation(0.16f, 8.53f, 4.92f, false, Rotation::Deg0, out));
        SPX_ASSERT(out == Rotation::Deg0);
        SPX_ASSERT(composeRotation(Rotation::Deg270CW, out) == Rotation::Deg270CW);

        // Portrait camera-left: ax > 0 dominant -> Deg90CW -> 270 + 90 = 0.
        SPX_ASSERT(classifyGravityOrientation(9.2f, 0.3f, 2.5f, true, Rotation::Deg0, out));
        SPX_ASSERT(out == Rotation::Deg90CW);
        SPX_ASSERT(composeRotation(Rotation::Deg270CW, out) == Rotation::Deg0);

        // Upside-down landscape: ay < 0 dominant -> Deg180 -> 270 + 180 = 90.
        SPX_ASSERT(classifyGravityOrientation(-0.2f, -9.5f, 1.5f, true, Rotation::Deg90CW, out));
        SPX_ASSERT(out == Rotation::Deg180);
        SPX_ASSERT(composeRotation(Rotation::Deg270CW, out) == Rotation::Deg90CW);

        // Portrait camera-right: ax < 0 dominant -> Deg270CW -> 270 + 270 = 180.
        SPX_ASSERT(classifyGravityOrientation(-9.1f, -0.4f, 2.0f, true, Rotation::Deg180, out));
        SPX_ASSERT(out == Rotation::Deg270CW);
        SPX_ASSERT(composeRotation(Rotation::Deg270CW, out) == Rotation::Deg180);

        // Flat on desk: hypot(ax, ay) < 2.8 returns false without prev, preserves prev when available.
        out = Rotation::Deg0;
        SPX_ASSERT(!classifyGravityOrientation(0.3f, 0.4f, 9.75f, false, Rotation::Deg90CW, out));
        SPX_ASSERT(classifyGravityOrientation(0.3f, 0.4f, 9.75f, true, Rotation::Deg90CW, out));
        SPX_ASSERT(out == Rotation::Deg90CW);

        // Diagonal hysteresis deadband (7.0 < 1.35 * 6.5) preserves Deg90CW, then (4.0, 8.8) switches to Deg0.
        SPX_ASSERT(classifyGravityOrientation(6.5f, 7.0f, 1.0f, true, Rotation::Deg90CW, out));
        SPX_ASSERT(out == Rotation::Deg90CW);
        SPX_ASSERT(classifyGravityOrientation(4.0f, 8.8f, 1.0f, true, Rotation::Deg90CW, out));
        SPX_ASSERT(out == Rotation::Deg0);

        // Non-finite inputs handled safely.
        const float qnan = std::numeric_limits<float>::quiet_NaN();
        const float pinf = std::numeric_limits<float>::infinity();
        SPX_ASSERT(!classifyGravityOrientation(qnan, 0.0f, 9.8f, false, Rotation::Deg0, out));
        SPX_ASSERT(classifyGravityOrientation(pinf, 8.0f, 0.0f, true, Rotation::Deg180, out));
        SPX_ASSERT(out == Rotation::Deg180);
    }

    removeTree(storeRoot);
}

void testLidMonitor() {
    using namespace spx::face;
    SPX_ASSERT(isSsamCoverFolded("folded-back"));
    SPX_ASSERT(isSsamCoverFolded("folded-canvas"));
    SPX_ASSERT(isSsamCoverFolded("book"));
    SPX_ASSERT(!isSsamCoverFolded("closed"));
    SPX_ASSERT(!isSsamCoverFolded("laptop"));
    SPX_ASSERT(!isSsamCoverFolded("disconnected"));
    SPX_ASSERT(!isSsamCoverFolded("unavailable"));

    SPX_ASSERT(!evaluateEffectiveLidClosed(true, "folded-back"));
    SPX_ASSERT(evaluateEffectiveLidClosed(true, "closed"));
    SPX_ASSERT(evaluateEffectiveLidClosed(true, "disconnected"));
    SPX_ASSERT(!evaluateEffectiveLidClosed(false, "closed"));

    const std::string tmpDir = makeTempDir("spx-lid-mon-");
    const std::string ssamDev = tmpDir + "/ssam/01:03:00:01:01";
    SPX_ASSERT(mkdir((tmpDir + "/ssam").c_str(), 0755) == 0);
    SPX_ASSERT(mkdir(ssamDev.c_str(), 0755) == 0);
    SPX_ASSERT(mkdir((tmpDir + "/input").c_str(), 0755) == 0);

    {
        FILE* f = std::fopen((ssamDev + "/state").c_str(), "w");
        SPX_ASSERT(f != nullptr);
        std::fputs("folded-back\n", f);
        std::fclose(f);
    }
    SPX_ASSERT(readSsamCoverState(tmpDir + "/ssam") == "folded-back");

    SpxLidMonitor mon(":test:", ":test:");
    int callbackCount = 0;
    mon.setOnLidClosedCallback([&]() { ++callbackCount; });

    mon.updateStateForTest(true, true, "folded-back");
    SPX_ASSERT(!mon.isLidClosed());
    SPX_ASSERT(callbackCount == 0);

    mon.updateStateForTest(true, false, "closed");
    SPX_ASSERT(mon.isLidClosed());
    SPX_ASSERT(callbackCount == 1);

    removeTree(tmpDir);
}

void testCrossOrientationRobustness() {
    using namespace spx::face;
    constexpr float kPi = 3.14159265358979323846f;

    struct Face3DParams {
        float eyeSep = 0.38f;
        float eyeY = -0.26f;
        float noseW = 0.028f;
        float noseBoost = 140.0f;
        float mouthY = 0.48f;
        float mouthW = 0.14f;
        float aspectX = 0.24f;
        float aspectY = 0.28f;
        bool glasses = false;
        bool beard = false;
        int texSeed = 0;
    };

    auto renderFace3D = [&](unsigned w,
                            unsigned h,
                            const Face3DParams& p,
                            float lx,
                            float ly,
                            float lz,
                            float yawDeg,
                            float pitchDeg,
                            float scaleMul,
                            bool flatPrint,
                            float flatTiltDeg = 0.0f) {
        std::vector<uint16_t> px(static_cast<size_t>(w) * h, 28u);
        const float cx = static_cast<float>(w) * 0.50f;
        const float cy = static_cast<float>(h) * 0.48f;
        const float minDim = static_cast<float>(std::min(w, h));
        const float rx = minDim * p.aspectX * scaleMul;
        const float ry = minDim * p.aspectY * scaleMul;
        const float ln = std::sqrt(lx * lx + ly * ly + lz * lz);
        lx /= ln;
        ly /= ln;
        lz /= ln;
        const float yaw = yawDeg * kPi / 180.0f;
        const float pitch = pitchDeg * kPi / 180.0f;
        const float flatTilt = flatTiltDeg * kPi / 180.0f;
        const float cyaw = std::cos(yaw), syaw = std::sin(yaw);
        const float cpitch = std::cos(pitch), spitch = std::sin(pitch);

        for (unsigned y = 0; y < h; ++y) {
            for (unsigned x = 0; x < w; ++x) {
                const float dx = (static_cast<float>(x) - cx) / rx;
                const float dy = (static_cast<float>(y) - cy) / ry;
                const float r2 = dx * dx + dy * dy;
                float val = 28.0f;
                if (r2 <= 1.0f) {
                    float fx = dx;
                    float fy = dy;
                    float ndotl = 0.9f;
                    float noseR2 = 2.0f;
                    if (flatPrint) {
                        const float zdist = 40.0f + dx * 9.0f * std::sin(flatTilt);
                        const float fnx = std::sin(flatTilt);
                        const float fnz = std::cos(flatTilt);
                        const float dot = std::max(0.1f, fnx * lx + fnz * lz);
                        ndotl = dot * (40.0f * 40.0f) / (zdist * zdist);
                    } else {
                        const float z = std::sqrt(std::max(0.0f, 1.0f - r2));
                        fx = dx * cyaw + z * syaw;
                        const float fz1 = -dx * syaw + z * cyaw;
                        fy = dy * cpitch - fz1 * spitch;
                        float nx = dx, ny = dy, nz = z;
                        noseR2 = (fx * fx) / p.noseW + (fy - 0.04f) * (fy - 0.04f) / 0.09f;
                        if (noseR2 < 1.0f) {
                            const float bump = 1.0f - noseR2;
                            nx += fx * 1.4f * bump;
                            nz += 0.6f * bump;
                        }
                        const float nl = std::sqrt(nx * nx + ny * ny + nz * nz) + 1e-6f;
                        nx /= nl;
                        ny /= nl;
                        nz /= nl;
                        ndotl = std::max(0.08f, nx * lx + ny * ly + nz * lz);
                    }

                    float alb = 0.72f;
                    const float le = (fx + p.eyeSep) * (fx + p.eyeSep) / 0.04f +
                                     (fy - p.eyeY) * (fy - p.eyeY) / 0.028f;
                    const float re = (fx - p.eyeSep) * (fx - p.eyeSep) / 0.04f +
                                     (fy - p.eyeY) * (fy - p.eyeY) / 0.028f;
                    if (le < 1.0f || re < 1.0f) alb *= 0.42f;
                    if (!flatPrint && noseR2 < 1.0f) {
                        alb += (p.noseBoost / 600.0f) * (1.0f - noseR2);
                    }
                    const float mouth =
                        (fx * fx) / p.mouthW + (fy - p.mouthY) * (fy - p.mouthY) / 0.015f;
                    if (mouth < 1.0f) alb *= 0.55f;
                    if (p.glasses) {
                        const float brow =
                            (fy + 0.39f) * (fy + 0.39f) / 0.008f + (fx * fx) / 0.48f;
                        if (brow < 1.0f) alb *= 0.45f;
                    }
                    if (p.beard && fy > 0.52f) alb *= 0.58f;
                    if (p.texSeed == 1) {
                        alb += 0.14f * std::cos(fx * 22.0f + fy * 9.0f) +
                               0.08f * std::sin(fy * 26.0f);
                    } else if (p.texSeed == 2) {
                        alb += 0.10f * std::sin(fx * 16.0f - fy * 14.0f);
                    } else if (p.texSeed == 3) {
                        alb += 0.09f * std::cos(fx * 12.0f) * std::cos(fy * 18.0f);
                    } else if (p.texSeed == 4) {
                        alb += 0.11f * std::sin(fx * 19.0f) * std::cos(fy * 15.0f);
                    }
                    float shMul = 1.0f;
                    if (!flatPrint) {
                        const float shX = fx - (-lx * 0.18f);
                        const float shY = fy - (-ly * 0.14f);
                        const float nSh =
                            (shX * shX) / 0.032f + (shY - 0.04f) * (shY - 0.04f) / 0.08f;
                        if (nSh < 1.0f && noseR2 >= 0.7f) {
                            shMul = 1.0f - 0.35f * (1.0f - nSh);
                        }
                    }
                    val = 80.0f + 680.0f * alb * ndotl * shMul;
                }
                const int tex = static_cast<int>(((x * 7u + y * 13u) & 7u));
                px[static_cast<size_t>(y) * w + x] =
                    static_cast<uint16_t>(std::clamp(std::lround(val + tex), 0L, 1023L));
            }
        }
        return packY10(px, w, h, w * 2u);
    };

    const Face3DParams owner{};
    const std::string storeRoot = makeTempDir("spx-face-xorient-");
    SpxFaceStore store(0, storeRoot + "/u0");
    SPX_ASSERT(store.load());

    // Enroll once in landscape posture (480x640 upright, horizontal LED offset lx=+0.25).
    {
        const auto rawLand = renderFace3D(480u, 640u, owner, 0.25f, 0.0f, 0.95f, 0.0f, 0.0f, 1.0f, false);
        const auto stLand = preprocessIrFrame(
            rawLand.data(), rawLand.size(), 480u, 640u, 960u, kPixFmtY10, Rotation::Deg0);
        const auto detLand = detectFaceNir(stLand);
        SPX_ASSERT(detLand.detected && detLand.quality == FaceQuality::GOOD);
        const auto embLand = extractEmbedding(stLand, detLand);
        SPX_ASSERT(store.addEnrollment({embLand}) > 0);
    }

    auto verifyCrossGenuine = [&](unsigned w,
                                  unsigned h,
                                  float lx,
                                  float ly,
                                  float yawDeg,
                                  float pitchDeg,
                                  float scaleMul) {
        const auto raw =
            renderFace3D(w, h, owner, lx, ly, 0.95f, yawDeg, pitchDeg, scaleMul, false);
        const auto st = preprocessIrFrame(
            raw.data(), raw.size(), w, h, w * 2u, kPixFmtY10, Rotation::Deg0);
        const auto det = detectFaceNir(st);
        SPX_ASSERT(det.detected);
        const auto baseEmb = extractEmbedding(st, det);
        const auto refined = matchWithAlignmentRefinement(
            st, det, baseEmb, store, nullptr, kDefaultMatchThreshold);
        SPX_ASSERT(refined.match.matched);
        SPX_ASSERT(refined.match.bestSimilarity >= kDefaultMatchThreshold);
    };

    // Portrait with vertical LED parallax + side-bezel camera yaw & distance shifts
    // (both ROTATION_90 power-at-top and ROTATION_270 volume-keys-at-top).
    verifyCrossGenuine(640u, 480u, 0.0f, -0.25f, 10.0f, 0.0f, 1.00f);
    verifyCrossGenuine(640u, 480u, 0.0f, -0.25f, 14.0f, 0.0f, 1.00f);
    verifyCrossGenuine(640u, 480u, 0.0f, -0.25f, 10.0f, 6.0f, 1.00f);
    verifyCrossGenuine(640u, 480u, -0.25f, 0.25f, -14.0f, -4.0f, 0.85f);
    verifyCrossGenuine(640u, 480u, -0.35f, 0.25f, -12.0f, 0.0f, 0.78f);
    verifyCrossGenuine(640u, 480u, -0.35f, 0.35f, -16.0f, 0.0f, 0.90f);
    verifyCrossGenuine(480u, 640u, -0.25f, 0.0f, 0.0f, 8.0f, 1.00f);
    verifyCrossGenuine(640u, 480u, 0.0f, -0.25f, 8.0f, 2.0f, 0.92f);
    // Power-at-TOP (ROTATION_90, rot=0): IR LED sits below OV7251 on the left bezel
    // (bottom-left under-lighting ly > 0 + positive side-bezel yaw +14..+20 deg).
    verifyCrossGenuine(640u, 480u, 0.0f, 0.30f, 14.0f, 0.0f, 1.00f);
    verifyCrossGenuine(640u, 480u, 0.0f, 0.35f, 16.0f, 0.0f, 1.00f);
    verifyCrossGenuine(640u, 480u, 0.35f, 0.30f, 14.0f, 0.0f, 0.95f);
    verifyCrossGenuine(640u, 480u, 0.0f, 0.25f, 20.0f, 0.0f, 1.00f);
    verifyCrossGenuine(640u, 480u, -0.75f, 0.35f, 16.0f, 0.0f, 0.95f);

    // 4 distinct synthetic impostors under the EXACT same landscape pose & lighting must be rejected.
    Face3DParams impB;
    impB.eyeSep = 0.44f;
    impB.aspectX = 0.27f;
    impB.aspectY = 0.25f;
    impB.glasses = true;
    impB.beard = true;
    impB.texSeed = 1;

    Face3DParams impC;
    impC.eyeSep = 0.34f;
    impC.eyeY = -0.22f;
    impC.mouthY = 0.44f;
    impC.aspectX = 0.22f;
    impC.aspectY = 0.30f;
    impC.texSeed = 2;

    Face3DParams impD;
    impD.eyeSep = 0.43f;
    impD.eyeY = -0.30f;
    impD.mouthY = 0.55f;
    impD.noseW = 0.055f;
    impD.mouthW = 0.22f;
    impD.aspectX = 0.26f;
    impD.aspectY = 0.25f;
    impD.texSeed = 3;

    Face3DParams impF;
    impF.eyeSep = 0.34f;
    impF.eyeY = -0.21f;
    impF.mouthY = 0.56f;
    impF.noseBoost = 30.0f;
    impF.mouthW = 0.22f;
    impF.aspectX = 0.22f;
    impF.aspectY = 0.31f;
    impF.beard = true;
    impF.texSeed = 4;

    struct ImpostorPose {
        float lx;
        float ly;
        float yaw;
    };
    const std::array<ImpostorPose, 3> impPoses{{
        {0.25f, 0.0f, 0.0f},
        {0.0f, 0.35f, 16.0f},
        {-0.35f, 0.35f, -16.0f},
    }};
    for (const auto& imp : {impB, impC, impD, impF}) {
        for (size_t pIdx = 0; pIdx < impPoses.size(); ++pIdx) {
            const auto& pose = impPoses[pIdx];
            const auto raw = renderFace3D(
                480u, 640u, imp, pose.lx, pose.ly, 0.95f, pose.yaw, 0.0f, 1.0f, false);
            const auto st = preprocessIrFrame(
                raw.data(), raw.size(), 480u, 640u, 960u, kPixFmtY10, Rotation::Deg0);
            const auto det = detectFaceNir(st);
            if (pIdx == 0) {
                SPX_ASSERT(det.detected);
            }
            if (!det.detected) continue;
            const auto baseEmb = extractEmbedding(st, det);
            const auto refined = matchWithAlignmentRefinement(
                st, det, baseEmb, store, nullptr, kDefaultMatchThreshold);
            SPX_ASSERT(!refined.match.matched);
            SPX_ASSERT(refined.match.bestSimilarity < kDefaultMatchThreshold - 0.08f);
        }
    }

    // Small noisy glare + inner-canthus + eyebrow stability regression test (prevents 02:16 io collapse).
    {
        auto renderSmallNoisyCanthusGlare = [&](float yawDeg,
                                                float pitchDeg,
                                                float scaleMul,
                                                int seed) {
            constexpr unsigned kW = 480u;
            constexpr unsigned kH = 640u;
            std::vector<uint16_t> px(static_cast<size_t>(kW) * kH, 42u);
            for (unsigned y = 20u; y < 140u; ++y) {
                for (unsigned x = 20u; x < 150u; ++x) {
                    px[static_cast<size_t>(y) * kW + x] = 760u;
                }
            }
            const float cx = static_cast<float>(kW) * 0.50f + ((seed % 3) - 1) * 0.6f;
            const float cy = static_cast<float>(kH) * 0.48f + (((seed / 3) % 3) - 1) * 0.6f;
            const float rx = static_cast<float>(kW) * owner.aspectX * scaleMul;
            const float ry = static_cast<float>(kW) * owner.aspectY * scaleMul;
            const float lx = 0.24f, ly = 0.02f, lz = 0.95f;
            const float ln = std::sqrt(lx * lx + ly * ly + lz * lz);
            const float nlx = lx / ln, nly = ly / ln, nlz = lz / ln;
            const float yaw = yawDeg * kPi / 180.0f;
            const float pitch = pitchDeg * kPi / 180.0f;
            const float cyaw = std::cos(yaw), syaw = std::sin(yaw);
            const float cpitch = std::cos(pitch), spitch = std::sin(pitch);

            for (unsigned y = 0; y < kH; ++y) {
                for (unsigned x = 0; x < kW; ++x) {
                    const float dx = (static_cast<float>(x) - cx) / rx;
                    const float dy = (static_cast<float>(y) - cy) / ry;
                    const float r2 = dx * dx + dy * dy;
                    float val = static_cast<float>(px[static_cast<size_t>(y) * kW + x]);
                    if (r2 <= 1.0f) {
                        const float z = std::sqrt(std::max(0.0f, 1.0f - r2));
                        const float fx = dx * cyaw + z * syaw;
                        const float fz1 = -dx * syaw + z * cyaw;
                        const float fy = dy * cpitch - fz1 * spitch;
                        float nx = dx, ny = dy, nz = z;
                        const float noseR2 = (fx * fx) / owner.noseW +
                                             (fy - 0.04f) * (fy - 0.04f) / 0.09f;
                        if (noseR2 < 1.0f) {
                            const float bump = 1.0f - noseR2;
                            nx += fx * 1.4f * bump;
                            nz += 0.6f * bump;
                        }
                        const float nl = std::sqrt(nx * nx + ny * ny + nz * nz) + 1e-6f;
                        const float ndotl =
                            std::max(0.10f, (nx / nl) * nlx + (ny / nl) * nly + (nz / nl) * nlz);
                        float alb = 0.72f;
                        const float le = (fx + owner.eyeSep) * (fx + owner.eyeSep) / 0.04f +
                                         (fy - owner.eyeY) * (fy - owner.eyeY) / 0.028f;
                        const float re = (fx - owner.eyeSep) * (fx - owner.eyeSep) / 0.04f +
                                         (fy - owner.eyeY) * (fy - owner.eyeY) / 0.028f;
                        if (le < 1.0f || re < 1.0f) alb *= 0.42f;
                        // Dark inner-canthus shadow beside nasal bridge
                        const float lic = (fx + 0.19f) * (fx + 0.19f) / 0.011f +
                                          (fy - (owner.eyeY + 0.02f)) * (fy - (owner.eyeY + 0.02f)) / 0.016f;
                        const float ric = (fx - 0.19f) * (fx - 0.19f) / 0.011f +
                                          (fy - (owner.eyeY + 0.02f)) * (fy - (owner.eyeY + 0.02f)) / 0.016f;
                        if (lic < 1.0f || ric < 1.0f) alb *= 0.34f;
                        // Dark eyebrows above eye sockets
                        const float lb = (fx + owner.eyeSep) * (fx + owner.eyeSep) / 0.055f +
                                         (fy - (owner.eyeY - 0.15f)) * (fy - (owner.eyeY - 0.15f)) / 0.008f;
                        const float rb = (fx - owner.eyeSep) * (fx - owner.eyeSep) / 0.055f +
                                         (fy - (owner.eyeY - 0.15f)) * (fy - (owner.eyeY - 0.15f)) / 0.008f;
                        if (lb < 1.0f || rb < 1.0f) alb *= 0.50f;
                        if (noseR2 < 1.0f) {
                            alb += (owner.noseBoost / 600.0f) * (1.0f - noseR2);
                        }
                        const float mouth = (fx * fx) / owner.mouthW +
                                            (fy - owner.mouthY) * (fy - owner.mouthY) / 0.015f;
                        if (mouth < 1.0f) alb *= 0.55f;
                        val = 55.0f + 360.0f * alb * ndotl;
                    }
                    const uint32_t hsh = (x * 73856093u) ^ (y * 19349663u) ^
                                         (static_cast<uint32_t>(seed) * 83492791u);
                    const float noise = ((static_cast<float>(hsh & 255u) / 127.5f) - 1.0f) * 16.0f;
                    px[static_cast<size_t>(y) * kW + x] =
                        static_cast<uint16_t>(std::clamp(std::lround(val + noise), 0L, 1023L));
                }
            }
            const auto raw = packY10(px, kW, kH, kW * 2u);
            return preprocessIrFrame(raw.data(), raw.size(), kW, kH, kW * 2u, kPixFmtY10, Rotation::Deg0);
        };

        std::vector<Embedding> smallCandidates;
        for (int i = 0; i < 6; ++i) {
            const auto st = renderSmallNoisyCanthusGlare(
                (i % 3 - 1) * 1.5f, (i % 2) * 1.5f, 0.30f + (i % 2) * 0.01f, 100 + i);
            const auto det = detectFaceNir(st);
            SPX_ASSERT(det.detected && det.quality == FaceQuality::GOOD);
            const float io = std::hypot(det.rightEye.x - det.leftEye.x, det.rightEye.y - det.leftEye.y);
            SPX_ASSERT(io / det.box.w >= 0.30f);
            smallCandidates.push_back(extractEmbedding(st, det));
        }
        for (int i = 0; i < 10; ++i) {
            const auto st = renderSmallNoisyCanthusGlare(
                static_cast<float>((i % 5) - 2) * 2.5f,
                static_cast<float>((i % 3) - 1) * 2.5f,
                0.295f + static_cast<float>(i % 3) * 0.015f,
                200 + i);
            const auto det = detectFaceNir(st);
            SPX_ASSERT(det.detected);
            const float io = std::hypot(det.rightEye.x - det.leftEye.x, det.rightEye.y - det.leftEye.y);
            SPX_ASSERT(io / det.box.w >= 0.30f);
            const auto probeEmb = extractEmbedding(st, det);
            const auto verified = matchCandidatesWithAlignmentRefinement(
                st, det, probeEmb, smallCandidates, nullptr, kDefaultMatchThreshold);
            SPX_ASSERT(verified.match.matched);
            SPX_ASSERT(verified.match.bestSimilarity >= kDefaultMatchThreshold);
        }
        const auto rawImpB = renderFace3D(480u, 640u, impB, 0.25f, 0.0f, 0.95f, 0.0f, 0.0f, 1.0f, false);
        const auto stImpB = preprocessIrFrame(
            rawImpB.data(), rawImpB.size(), 480u, 640u, 960u, kPixFmtY10, Rotation::Deg0);
        const auto detImpB = detectFaceNir(stImpB);
        SPX_ASSERT(detImpB.detected);
        const auto impBCheck = matchCandidatesWithAlignmentRefinement(
            stImpB, detImpB, extractEmbedding(stImpB, detImpB), smallCandidates, nullptr, kDefaultMatchThreshold);
        SPX_ASSERT(!impBCheck.match.matched);
    }

    // Liveness: strong side-lit 3D faces (including glasses/nose-bridge shadow where classic
    // nose-to-mid-cheek drops < 0.90) pass via lateral ellipsoid convexity, while 0°/30°/45°
    // flat prints still reject with REJECT_FLAT_OR_PRINT.
    for (float lx : {0.45f, 0.75f, -0.75f}) {
        const auto raw3d = renderFace3D(480u, 640u, owner, lx, 0.0f, 0.85f, 0.0f, 0.0f, 1.0f, false);
        const auto st3d = preprocessIrFrame(
            raw3d.data(), raw3d.size(), 480u, 640u, 960u, kPixFmtY10, Rotation::Deg0);
        const auto det3d = detectFaceNir(st3d);
        SPX_ASSERT(det3d.detected);
        const auto live3d = evaluateLiveness(st3d, det3d, Embedding{}, nullptr, false);
        SPX_ASSERT(live3d.live);
        SPX_ASSERT(live3d.centerToRimRatio >= 1.04f);

        // Darken narrow nasal bridge [42..58%]x[40..58%] to simulate side-bezel Portrait shadow
        // or glasses nose-pad occlusion (like live 10:47/10:48 c2r=0.79..0.89).
        IrFrameStats shadowed = st3d;
        const unsigned sx0 = static_cast<unsigned>(det3d.box.x + 0.42f * det3d.box.w);
        const unsigned sx1 = static_cast<unsigned>(det3d.box.x + 0.58f * det3d.box.w);
        const unsigned sy0 = static_cast<unsigned>(det3d.box.y + 0.40f * det3d.box.h);
        const unsigned sy1 = static_cast<unsigned>(det3d.box.y + 0.58f * det3d.box.h);
        for (unsigned y = sy0; y < sy1 && y < shadowed.height; ++y) {
            for (unsigned x = sx0; x < sx1 && x < shadowed.width; ++x) {
                shadowed.upright10[static_cast<size_t>(y) * shadowed.width + x] =
                    static_cast<uint16_t>(
                        shadowed.upright10[static_cast<size_t>(y) * shadowed.width + x] * 65u / 100u);
            }
        }
        const auto liveShadow = evaluateLiveness(shadowed, det3d, Embedding{}, nullptr, false);
        SPX_ASSERT(liveShadow.live);
        SPX_ASSERT(liveShadow.centerToRimRatio >= 1.04f);
    }
    for (float tilt : {0.0f, 30.0f, 45.0f}) {
        const auto rawFlat = renderFace3D(480u, 640u, owner, 0.45f, 0.0f, 0.85f, 0.0f, 0.0f, 1.0f, true, tilt);
        const auto stFlat = preprocessIrFrame(
            rawFlat.data(), rawFlat.size(), 480u, 640u, 960u, kPixFmtY10, Rotation::Deg0);
        const auto detFlat = detectFaceNir(stFlat);
        SPX_ASSERT(detFlat.detected);
        const auto liveFlat = evaluateLiveness(stFlat, detFlat, Embedding{}, nullptr, false);
        SPX_ASSERT(!liveFlat.live);
        SPX_ASSERT(liveFlat.status == LivenessStatus::REJECT_FLAT_OR_PRINT);
    }

    // Multi-candidate scale-aware NMS: a competing high-contrast small background face-like
    // distractor must not hide the real owner's face from detectFaceCandidatesNir().
    {
        auto rawClutter = renderFace3D(480u, 640u, owner, 0.25f, 0.0f, 0.95f, 0.0f, 0.0f, 0.82f, false);
        // Inject a high-contrast small distractor (boxW ~ 96px) in upper-left corner.
        const unsigned dx0 = 24u, dy0 = 36u, dw = 96u, dh = 110u;
        for (unsigned y = 0; y < dh; ++y) {
            for (unsigned x = 0; x < dw; ++x) {
                const float nx = (static_cast<float>(x) / static_cast<float>(dw)) * 2.0f - 1.0f;
                const float ny = (static_cast<float>(y) / static_cast<float>(dh)) * 2.0f - 1.0f;
                if (nx * nx + ny * ny > 1.0f) continue;
                uint16_t v = 720u;
                const bool inLeftEye = (x >= 16u && x < 38u && y >= 24u && y < 46u);
                const bool inRightEye = (x >= 58u && x < 80u && y >= 24u && y < 46u);
                const bool inMouth = (x >= 26u && x < 70u && y >= 74u && y < 90u);
                if (inLeftEye || inRightEye) v = 40u;
                else if (inMouth) v = 120u;
                const size_t idx = (static_cast<size_t>(dy0 + y) * 480u + (dx0 + x)) * 2u;
                rawClutter[idx] = static_cast<uint8_t>(v & 0xFFu);
                rawClutter[idx + 1] = static_cast<uint8_t>((v >> 8) & 0x03u);
            }
        }
        const auto stClutter = preprocessIrFrame(
            rawClutter.data(), rawClutter.size(), 480u, 640u, 960u, kPixFmtY10, Rotation::Deg0);
        const auto cands = detectFaceCandidatesNir(stClutter, 4u);
        SPX_ASSERT(cands.size() >= 2u);
        bool matchedRealOwner = false;
        for (const auto& c : cands) {
            const auto emb = extractEmbedding(stClutter, c);
            const auto live = evaluateLiveness(stClutter, c, emb, nullptr, false);
            if (!live.live) continue;
            const auto res = matchWithAlignmentRefinement(
                stClutter, c, emb, store, nullptr, kDefaultMatchThreshold);
            if (res.match.matched) {
                matchedRealOwner = true;
                break;
            }
        }
        SPX_ASSERT(matchedRealOwner);
    }

    // Multi-candidate scale quota (Power-at-TOP 640x480 regression): 4 high-contrast tiny 64px
    // background specks must NOT crowd out a real 160px bottom-left-lit yawed owner face.
    {
        auto rawSpecks =
            renderFace3D(640u, 480u, owner, 0.0f, 0.35f, 0.95f, 16.0f, 0.0f, 0.85f, false);
        const std::array<std::pair<unsigned, unsigned>, 4> speckPos{{
            {20u, 20u},
            {540u, 20u},
            {20u, 370u},
            {540u, 370u},
        }};
        const unsigned sw = 64u, sh = 74u;
        for (const auto& [sx0, sy0] : speckPos) {
            for (unsigned y = 0; y < sh; ++y) {
                for (unsigned x = 0; x < sw; ++x) {
                    const float nx = (static_cast<float>(x) / static_cast<float>(sw)) * 2.0f - 1.0f;
                    const float ny = (static_cast<float>(y) / static_cast<float>(sh)) * 2.0f - 1.0f;
                    if (nx * nx + ny * ny > 1.0f) continue;
                    uint16_t v = 760u;
                    const bool inLeftEye = (x >= 11u && x < 25u && y >= 17u && y < 31u);
                    const bool inRightEye = (x >= 39u && x < 53u && y >= 17u && y < 31u);
                    const bool inMouth = (x >= 18u && x < 46u && y >= 50u && y < 61u);
                    if (inLeftEye || inRightEye) v = 35u;
                    else if (inMouth) v = 110u;
                    const size_t idx = (static_cast<size_t>(sy0 + y) * 640u + (sx0 + x)) * 2u;
                    rawSpecks[idx] = static_cast<uint8_t>(v & 0xFFu);
                    rawSpecks[idx + 1] = static_cast<uint8_t>((v >> 8) & 0x03u);
                }
            }
        }
        const auto stSpecks = preprocessIrFrame(
            rawSpecks.data(), rawSpecks.size(), 640u, 480u, 1280u, kPixFmtY10, Rotation::Deg0);
        const auto cands = detectFaceCandidatesNir(stSpecks, 6u);
        SPX_ASSERT(cands.size() >= 4u);
        bool matchedRealOwner = false;
        for (const auto& c : cands) {
            if (c.box.w < 106.0f) continue;
            const auto emb = extractEmbedding(stSpecks, c);
            const auto live = evaluateLiveness(stSpecks, c, emb, nullptr, false);
            if (!live.live) continue;
            const auto res = matchWithAlignmentRefinement(
                stSpecks, c, emb, store, nullptr, kDefaultMatchThreshold);
            if (res.match.matched) {
                matchedRealOwner = true;
                break;
            }
        }
        SPX_ASSERT(matchedRealOwner);
    }

    // probeAlternateOrientationMatch must reject non-live flat print switches even if rotated.
    {
        const auto rawFlatRot =
            renderFace3D(480u, 640u, owner, 0.25f, 0.0f, 0.95f, 0.0f, 0.0f, 1.0f, true, 0.0f);
        const auto stFlatRot = preprocessIrFrame(
            rawFlatRot.data(), rawFlatRot.size(), 480u, 640u, 960u, kPixFmtY10, Rotation::Deg90CW);
        const auto hint = probeAlternateOrientationMatch(
            stFlatRot, Rotation::Deg270CW, 0.0f, store, nullptr, kDefaultMatchThreshold, false);
        SPX_ASSERT(!hint.shouldSwitch);
    }

    removeTree(storeRoot);
}

}  // namespace

int main() {
    testIrPreprocess();
    testCryptoAndHat();
    testEngineAndLiveness();
    testFaceStore();
    testLockoutTracker();
    testMultiOrientationSupport();
    testLidMonitor();
    testCrossOrientationRobustness();
    std::puts("PASS spx_face_test (ASan/UBSan)");
    return 0;
}
