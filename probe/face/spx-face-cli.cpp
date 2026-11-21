// SPDX-License-Identifier: GPL-2.0-only
/* Diagnostic & benchmark CLI for Surface Pro X OV7251 IR + Hall lid pipeline.
 * Never saves raw face images to persistent storage.
 */
#include "../camera/spx-capture.h"
#include "spx-face-crypto.h"
#include "spx-face-engine.h"
#include "spx-ir-preprocess.h"
#include "spx-lid-monitor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

namespace {

bool hasOv7251DriverDevice() {
    DIR* dir = opendir("/sys/bus/i2c/drivers/ov7251");
    if (!dir) return false;
    bool found = false;
    struct dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (std::strchr(ent->d_name, '-') != nullptr) {
            found = true;
            break;
        }
    }
    closedir(dir);
    return found;
}

std::vector<uint8_t> makeSyntheticIrY10Face(unsigned w,
                                            unsigned h,
                                            bool screenSpoof,
                                            bool flatPrint) {
    std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 2u, 0);
    const float cx = w * 0.50f;
    const float cy = h * 0.48f;
    const float rx = w * 0.24f;
    const float ry = h * 0.28f;

    for (unsigned y = 0; y < h; ++y) {
        for (unsigned x = 0; x < w; ++x) {
            const float dx = (static_cast<float>(x) - cx) / rx;
            const float dy = (static_cast<float>(y) - cy) / ry;
            const float r2 = dx * dx + dy * dy;
            float val = 28.0f;
            if (r2 <= 1.0f) {
                if (screenSpoof) {
                    val = 14.0f + 4.0f * (1.0f - r2);
                } else if (flatPrint) {
                    val = 340.0f;
                } else {
                    const float z = std::sqrt(std::max(0.0f, 1.0f - r2));
                    val = 220.0f + 420.0f * z;
                }
                if (!screenSpoof) {
                    // Bilateral dark eye sockets & brows
                    const float le = (dx + 0.38f) * (dx + 0.38f) / 0.04f +
                                     (dy + 0.26f) * (dy + 0.26f) / 0.028f;
                    const float re = (dx - 0.38f) * (dx - 0.38f) / 0.04f +
                                     (dy + 0.26f) * (dy + 0.26f) / 0.028f;
                    if (le < 1.0f || re < 1.0f) {
                        val *= 0.42f;
                    }
                    // Brighter nose bridge / tip in 3D face
                    const float nose = (dx * dx) / 0.028f + (dy - 0.04f) * (dy - 0.04f) / 0.09f;
                    if (nose < 1.0f && !flatPrint) {
                        val += 140.0f * (1.0f - nose);
                    }
                    // Darker mouth slit
                    const float mouth = (dx * dx) / 0.14f + (dy - 0.48f) * (dy - 0.48f) / 0.015f;
                    if (mouth < 1.0f) {
                        val *= 0.48f;
                    }
                }
            }
            // Mild deterministic texture variation
            val += static_cast<float>(((x * 7u + y * 13u) & 7u));
            const uint16_t u10 =
                static_cast<uint16_t>(std::clamp(std::lround(val), 0L, 1023L));
            const size_t idx = (static_cast<size_t>(y) * w + x) * 2u;
            buf[idx] = static_cast<uint8_t>(u10 & 0xFFu);
            buf[idx + 1] = static_cast<uint8_t>((u10 >> 8) & 0x03u);
        }
    }
    return buf;
}

int runSelfTest() {
    // 1. RFC 4231 Test Case 2 HMAC-SHA256 ("Jefe" / "what do ya want for nothing?")
    static constexpr char kKey[] = "Jefe";
    static constexpr char kMsg[] = "what do ya want for nothing?";
    const auto mac = spx::face::hmacSha256(
        reinterpret_cast<const uint8_t*>(kKey), 4, reinterpret_cast<const uint8_t*>(kMsg), 28);
    static constexpr std::array<uint8_t, 32> kExpected{
        0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e, 0x6a, 0x04, 0x24,
        0x26, 0x08, 0x95, 0x75, 0xc7, 0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27,
        0x39, 0x83, 0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43,
    };
    if (mac != kExpected) {
        std::fprintf(stderr, "FAIL: HMAC-SHA256 RFC 4231 mismatch\n");
        return 1;
    }

    // 2. Preprocess + 3D IR face detection + embedding + liveness + spoof rejection.
    const auto rawReal = makeSyntheticIrY10Face(480, 640, false, false);
    const auto realStats = spx::face::preprocessIrFrame(
        rawReal.data(), rawReal.size(), 480, 640, 960, spx::face::kPixFmtY10, spx::face::Rotation::Deg0);
    const auto det = spx::face::detectFaceNir(realStats);
    if (!det.detected) {
        std::fprintf(stderr, "FAIL: synthetic 3D NIR face not detected\n");
        return 1;
    }
    const auto emb = spx::face::extractEmbedding(realStats, det);
    spx::face::FaceSessionAccumulator acc;
    acc.reset(1);
    const auto live = spx::face::evaluateLiveness(realStats, det, emb, &acc, false);
    if (!live.live) {
        std::fprintf(stderr, "FAIL: real synthetic face failed liveness (status=%d)\n",
                     static_cast<int>(live.status));
        return 1;
    }

    const auto rawScreen = makeSyntheticIrY10Face(480, 640, true, false);
    const auto screenStats = spx::face::preprocessIrFrame(
        rawScreen.data(), rawScreen.size(), 480, 640, 960, spx::face::kPixFmtY10, spx::face::Rotation::Deg0);
    const auto detScreen = spx::face::detectFaceNir(screenStats);
    if (detScreen.detected) {
        const auto embScreen = spx::face::extractEmbedding(screenStats, detScreen);
        const auto liveScreen = spx::face::evaluateLiveness(screenStats, detScreen, embScreen, nullptr, false);
        if (liveScreen.live) {
            std::fprintf(stderr, "FAIL: 850nm dark screen spoof accepted\n");
            return 1;
        }
    }

    const auto rawPrint = makeSyntheticIrY10Face(480, 640, false, true);
    const auto printStats = spx::face::preprocessIrFrame(
        rawPrint.data(), rawPrint.size(), 480, 640, 960, spx::face::kPixFmtY10, spx::face::Rotation::Deg0);
    const auto detPrint = spx::face::detectFaceNir(printStats);
    if (detPrint.detected) {
        const auto embPrint = spx::face::extractEmbedding(printStats, detPrint);
        const auto livePrint = spx::face::evaluateLiveness(printStats, detPrint, embPrint, nullptr, false);
        if (livePrint.live) {
            std::fprintf(stderr, "FAIL: flat print spoof accepted (ratio=%.3f)\n",
                         livePrint.centerToRimRatio);
            return 1;
        }
    }

    // 3. Lockout state machine sanity.
    int64_t now = 1000;
    spx::face::SpxLockoutTracker tracker([&]() { return now; });
    for (int i = 0; i < 4; ++i) tracker.addFailedAttempt();
    if (tracker.checkState().mode != spx::face::LockoutMode::NONE) return 1;
    if (tracker.addFailedAttempt().mode != spx::face::LockoutMode::TIMED) return 1;
    now += 31000;
    if (tracker.checkState().mode != spx::face::LockoutMode::NONE) return 1;

    std::puts("PASS spx-face-cli --self-test");
    return 0;
}

int printStatus() {
    spx::face::SpxLidMonitor lid;
    const auto snap = lid.snapshot();
    const bool irPresent = hasOv7251DriverDevice();
    std::printf(
        "{\"lid_device_found\":%s,\"tablet_switch_found\":%s,"
        "\"raw_sw_lid_closed\":%s,\"raw_sw_tablet_mode\":%s,"
        "\"ssam_cover_state\":\"%s\",\"effective_lid_closed\":%s,"
        "\"ov7251_bound\":%s}\n",
        snap.lidDeviceFound ? "true" : "false",
        snap.tabletSwitchFound ? "true" : "false",
        snap.rawSwLidClosed ? "true" : "false",
        snap.rawSwTabletMode ? "true" : "false",
        snap.ssamCoverState.c_str(),
        snap.effectiveLidClosed ? "true" : "false",
        irPresent ? "true" : "false");
    return 0;
}

int runBenchmarkIr(unsigned targetFrames, spx::face::Rotation rot) {
    targetFrames = std::clamp(targetFrames, 1u, 150u);
    spx::face::SpxLidMonitor lid;
    lid.start();
    if (lid.isLidClosed()) {
        std::fprintf(stderr, "ABORT: effective SW_LID is closed; refusing to enable 850nm IR\n");
        return 2;
    }

    std::vector<double> procMs;
    procMs.reserve(targetFrames);
    unsigned detectedCount = 0;
    unsigned goodCount = 0;
    unsigned liveCount = 0;
    float lastRoiMean = 0.0f;
    int lastQ = -1;
    int lastLive = -1;
    float lastBoxW = 0.0f;
    float lastScore = 0.0f;
    float lastC2R = 0.0f;
    float lastHalftone = 0.0f;
    uint16_t lastP50 = 0;
    uint16_t lastP99 = 0;

    try {
        auto cap = spx::Capture::openIr(640, 480, 30);
        const auto startAll = std::chrono::steady_clock::now();
        spx::face::FaceSessionAccumulator acc;
        acc.reset(2);

        for (unsigned i = 0; i < targetFrames; ++i) {
            if (lid.isLidClosed()) {
                std::fprintf(stderr, "ABORT: lid closed during capture; stopping IR immediately\n");
                break;
            }
            const auto elapsedAllMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - startAll)
                                          .count();
            if (elapsedAllMs > 6000) {
                std::fprintf(stderr, "WATCHDOG: 6000ms limit reached at frame %u\n", i);
                break;
            }
            auto frame = cap->next(1500);
            const auto t0 = std::chrono::steady_clock::now();
            const auto stats = spx::face::preprocessIrFrame(frame.raw10.data(),
                                                            frame.raw10.size(),
                                                            frame.width,
                                                            frame.height,
                                                            frame.stride,
                                                            frame.pixelFormat ? frame.pixelFormat
                                                                              : spx::face::kPixFmtY10,
                                                            rot);
            float aeMetric = stats.roiLinearMean;
            if (stats.p99 >= 920u) {
                aeMetric = std::max(aeMetric, 0.34f + 0.40f * stats.saturatedRatio);
            }
            cap->autoExposure(aeMetric);
            lastRoiMean = stats.roiLinearMean;
            lastP50 = stats.p50;
            lastP99 = stats.p99;

            if (i >= 2) {
                const auto det = spx::face::detectFaceNir(stats);
                lastQ = static_cast<int>(det.quality);
                lastBoxW = det.box.w;
                lastScore = det.score;
                if (det.detected) {
                    ++detectedCount;
                    if (det.quality == spx::face::FaceQuality::GOOD) {
                        ++goodCount;
                    }
                    const auto emb = spx::face::extractEmbedding(stats, det);
                    const auto live = spx::face::evaluateLiveness(stats, det, emb, &acc, true);
                    lastLive = static_cast<int>(live.status);
                    lastC2R = live.centerToRimRatio;
                    lastHalftone = live.halftoneRatio;
                    if (live.live) ++liveCount;
                }
            }
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - t0)
                                  .count();
            procMs.push_back(ms);
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "IR benchmark failed: %s\n", ex.what());
        return 1;
    }

    if (procMs.empty()) return 1;
    std::sort(procMs.begin(), procMs.end());
    const double p50 = procMs[procMs.size() / 2];
    const double p95 = procMs[(procMs.size() * 95) / 100];
    std::printf(
        "{\"frames\":%zu,\"detected\":%u,\"good\":%u,\"live\":%u,"
        "\"proc_p50_ms\":%.2f,\"proc_p95_ms\":%.2f,\"last_roi_mean\":%.4f,"
        "\"last_q\":%d,\"last_live\":%d,\"last_box_w\":%.1f,\"last_score\":%.1f,"
        "\"last_c2r\":%.3f,\"last_halftone\":%.3f,\"last_p50\":%u,\"last_p99\":%u}\n",
        procMs.size(),
        detectedCount,
        goodCount,
        liveCount,
        p50,
        p95,
        lastRoiMean,
        lastQ,
        lastLive,
        lastBoxW,
        lastScore,
        lastC2R,
        lastHalftone,
        static_cast<unsigned>(lastP50),
        static_cast<unsigned>(lastP99));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    spx::face::Rotation rot = spx::face::Rotation::Deg270CW;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--rotation") == 0 && i + 1 < argc) {
            rot = spx::face::parseRotationDegrees(argv[++i]);
        }
    }
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--self-test") == 0) {
            return runSelfTest();
        }
        if (std::strcmp(argv[i], "--status") == 0) {
            return printStatus();
        }
        if (std::strcmp(argv[i], "--benchmark-ir") == 0) {
            unsigned n = 30;
            if (i + 1 < argc) n = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
            return runBenchmarkIr(n, rot);
        }
    }
    std::fprintf(stderr,
                 "Usage: %s [--self-test | --status | --benchmark-ir N] [--rotation 0|90|180|270]\n",
                 argv[0]);
    return 2;
}
