// SPDX-License-Identifier: GPL-2.0-only
#include "../probe/camera/spx-capture.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using spx::AutoExposureDecision;
using spx::ControlRange;
using spx::MediaNodeSummary;
using spx::Sensor;

std::vector<MediaNodeSummary> makeMultiIfeGraph() {
    // IDs: 1..3 = csiphy0/2/3; 10..13 = csid0..3; 20..23 = vfe0..3_rdi0; 30..33 = vfe0..3_video0.
    return {
        {1, "msm_csiphy0", {{1, 10}, {1, 11}, {1, 12}, {1, 13}}},
        {2, "msm_csiphy2", {{1, 10}, {1, 11}, {1, 12}, {1, 13}}},
        {3, "msm_csiphy3", {{1, 10}, {1, 11}, {1, 12}, {1, 13}}},
        {10, "msm_csid0", {{1, 20}}},
        {11, "msm_csid1", {{1, 21}}},
        {12, "msm_csid2", {{1, 22}}},
        {13, "msm_csid3", {{1, 23}}},
        {20, "msm_vfe0_rdi0", {{1, 30}}},
        {21, "msm_vfe1_rdi0", {{1, 31}}},
        {22, "msm_vfe2_rdi0", {{1, 32}}},
        {23, "msm_vfe3_rdi0", {{1, 33}}},
        {30, "msm_vfe0_video0", {}},
        {31, "msm_vfe1_video0", {}},
        {32, "msm_vfe2_video0", {}},
        {33, "msm_vfe3_video0", {}},
    };
}

std::vector<MediaNodeSummary> makeSingleIfeGraph() {
    return {
        {1, "msm_csiphy0", {{1, 10}}},
        {2, "msm_csiphy2", {{1, 10}}},
        {3, "msm_csiphy3", {{1, 10}}},
        {10, "msm_csid0", {{1, 20}}},
        {20, "msm_vfe0_rdi0", {{1, 30}}},
        {30, "msm_vfe0_video0", {}},
    };
}

void testDynamicPipelineSelection() {
    // Multi-IFE DT: Front -> vfe0, Rear -> vfe1, FrontIr -> vfe3 (vfe_lite1).
    auto multi = makeMultiIfeGraph();
    auto front = spx::selectPipeline(Sensor::Front, 1, multi);
    assert(front.csidName == "msm_csid0" && front.vfeName == "msm_vfe0_rdi0" &&
           front.videoName == "msm_vfe0_video0");

    auto rear = spx::selectPipeline(Sensor::Rear, 2, multi);
    assert(rear.csidName == "msm_csid1" && rear.vfeName == "msm_vfe1_rdi0" &&
           rear.videoName == "msm_vfe1_video0");

    auto ir = spx::selectPipeline(Sensor::FrontIr, 3, multi);
    assert(ir.csidName == "msm_csid3" && ir.vfeName == "msm_vfe3_rdi0" &&
           ir.videoName == "msm_vfe3_video0");

    // Partial VFE Lite DT (no vfe3, vfe2 present): IR falls back to vfe2 (vfe_lite0).
    std::vector<MediaNodeSummary> noVfe3;
    for (const auto& n : multi) {
        if (n.id != 13 && n.id != 23 && n.id != 33) noVfe3.push_back(n);
    }
    auto irLite0 = spx::selectPipeline(Sensor::FrontIr, 3, noVfe3);
    assert(irLite0.csidName == "msm_csid2" && irLite0.vfeName == "msm_vfe2_rdi0" &&
           irLite0.videoName == "msm_vfe2_video0");

    // Legacy single-IFE DT: Rear and IR both fall back seamlessly to vfe0.
    auto single = makeSingleIfeGraph();
    auto frontSingle = spx::selectPipeline(Sensor::Front, 1, single);
    auto rearSingle = spx::selectPipeline(Sensor::Rear, 2, single);
    auto irSingle = spx::selectPipeline(Sensor::FrontIr, 3, single);
    assert(frontSingle.vfeName == "msm_vfe0_rdi0");
    assert(rearSingle.vfeName == "msm_vfe0_rdi0");
    assert(irSingle.vfeName == "msm_vfe0_rdi0");

    // Unlinked csid1 (entity present, but phy2 only links to csid0): Rear falls back to csid0.
    auto partialLink = multi;
    partialLink[1].links = {{1, 10}};
    auto rearFallback = spx::selectPipeline(Sensor::Rear, 2, partialLink);
    assert(rearFallback.csidName == "msm_csid0" && rearFallback.vfeName == "msm_vfe0_rdi0");

    // Missing PHY or disconnected graph throws std::runtime_error cleanly.
    bool caught = false;
    try { (void)spx::selectPipeline(Sensor::Rear, 999, multi); }
    catch (const std::runtime_error&) { caught = true; }
    assert(caught);

    caught = false;
    std::vector<MediaNodeSummary> disconnected{{2, "msm_csiphy2", {}}};
    try { (void)spx::selectPipeline(Sensor::Rear, 2, disconnected); }
    catch (const std::runtime_error&) { caught = true; }
    assert(caught);
}

void testAutoExposureEdgeCases() {
    // 1. OV13858 with legacy ANA_GAIN_MIN=0 and current gain=0 must immediately lift to unity (128).
    auto d = spx::computeAutoExposure(Sensor::Rear, 0.18f,
                                      ControlRange{4, 3000, 1, 1000},
                                      ControlRange{0, 8191, 1, 0});
    assert(d.action == AutoExposureDecision::Action::SetGain && d.value == 128);

    // 2. OV13858 exposure=0 must immediately lift to >= 1.
    d = spx::computeAutoExposure(Sensor::Rear, 0.18f,
                                 ControlRange{0, 3000, 1, 0},
                                 ControlRange{128, 8191, 1, 128});
    assert(d.action == AutoExposureDecision::Action::SetExposure && d.value == 1);

    // 3. OV5693 (Front) gain range [16..248]: previously triggered clamp(lo=16, hi=8) UB!
    // When exposure is maxed and scene is dark, gain must climb cleanly within [16, 128].
    d = spx::computeAutoExposure(Sensor::Front, 0.05f,
                                 ControlRange{4, 2000, 1, 2000},
                                 ControlRange{16, 248, 1, 16});
    assert(d.action == AutoExposureDecision::Action::SetGain);
    assert(d.value > 16 && d.value <= 128);

    // When OV5693 is already at 8x cap (128) and still dark, no redundant ioctl / no UB.
    d = spx::computeAutoExposure(Sensor::Front, 0.05f,
                                 ControlRange{4, 2000, 1, 2000},
                                 ControlRange{16, 248, 1, 128});
    assert(d.action == AutoExposureDecision::Action::None);

    // 4. When too bright while gain > unity, reduce gain first before reducing exposure.
    d = spx::computeAutoExposure(Sensor::Rear, 0.50f,
                                 ControlRange{4, 3000, 1, 2000},
                                 ControlRange{128, 8191, 1, 512});
    assert(d.action == AutoExposureDecision::Action::SetGain);
    assert(d.value >= 128 && d.value < 512);

    // 5. Small integer exposure must make forward progress (no rounding stall at value=1).
    d = spx::computeAutoExposure(Sensor::FrontIr, 0.15f,
                                 ControlRange{1, 1000, 1, 1},
                                 ControlRange{16, 1023, 1, 16});
    assert(d.action == AutoExposureDecision::Action::SetExposure && d.value == 2);

    // 6. Step alignment (step=4) and step=0 safety.
    d = spx::computeAutoExposure(Sensor::FrontIr, 0.05f,
                                 ControlRange{4, 1000, 4, 100},
                                 ControlRange{16, 1023, 0, 16});
    assert(d.action == AutoExposureDecision::Action::SetExposure);
    assert((d.value - 4) % 4 == 0 && d.value > 100);

    // 7. Malformed control ranges (min > max, max < nominalUnity, NaN/negative/inf mean): zero UB.
    assert(spx::computeAutoExposure(Sensor::Rear, std::numeric_limits<float>::quiet_NaN(),
                                    ControlRange{4, 3000, 1, 500},
                                    ControlRange{128, 8191, 1, 128}).action ==
           AutoExposureDecision::Action::None);
    assert(spx::computeAutoExposure(Sensor::Rear, -0.1f,
                                    ControlRange{4, 3000, 1, 500},
                                    ControlRange{128, 8191, 1, 128}).action ==
           AutoExposureDecision::Action::None);
    assert(spx::computeAutoExposure(Sensor::Rear, 0.05f,
                                    ControlRange{100, 10, 1, 50},
                                    ControlRange{128, 8191, 1, 128}).action ==
           AutoExposureDecision::Action::None);
    // Driver with maximum gain smaller than nominal unity (1..10): must never clamp with lo > hi.
    d = spx::computeAutoExposure(Sensor::Front, 0.05f,
                                 ControlRange{1, 100, 1, 100},
                                 ControlRange{1, 10, 1, 5});
    assert(d.action == AutoExposureDecision::Action::SetGain && d.value == 10);
}

void testFrameBackwardCompatibility() {
    // Legacy 7-field aggregate initialization must continue to compile with zero warnings.
    spx::Frame legacy{640, 480, 800, 1, 123456ULL, true, {}};
    assert(legacy.pixelFormat == 0);
    assert(legacy.front == true);
}

} // namespace

int main() {
    testDynamicPipelineSelection();
    testAutoExposureEdgeCases();
    testFrameBackwardCompatibility();
    std::printf("spx_camera_capture_test: OK\n");
    return 0;
}
