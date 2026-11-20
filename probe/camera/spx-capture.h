// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace spx {
enum class Sensor {
    Rear,
    Front,
    FrontIr,
};
struct Frame {
    unsigned width, height, stride, sequence;
    uint64_t timestampNs;
    bool front;
    std::vector<uint8_t> raw10;
    uint32_t pixelFormat = 0; // 0 = legacy Bayer RAW10P; V4L2_PIX_FMT_* when populated by Capture
};
struct MediaNodeSummary {
    unsigned id = 0;
    std::string name;
    // Outgoing media links as (source_pad_index, sink_entity_id).
    std::vector<std::pair<unsigned, unsigned>> links;
};
struct PipelineSelection {
    unsigned csidId = 0;
    unsigned vfeId = 0;
    unsigned videoId = 0;
    std::string csidName;
    std::string vfeName;
    std::string videoName;
};
// Select the preferred CSID/VFE/video route for a sensor from the live media graph,
// preferring multi-IFE entities (vfe1 for rear, vfe3/vfe_lite1 for IR) and falling
// back seamlessly to single-IFE (vfe0) when only one IFE is present.
PipelineSelection selectPipeline(Sensor sensor,
                                 unsigned phyId,
                                 const std::vector<MediaNodeSummary>& nodes);

struct ControlRange {
    int minimum = 0;
    int maximum = 0;
    int step = 1;
    int value = 0;
};
struct AutoExposureDecision {
    enum class Action { None, SetExposure, SetGain };
    Action action = Action::None;
    int value = 0;
};
// Compute the next safe exposure or analogue-gain adjustment, guarding against
// zero/sub-unity ANA_GAIN_MIN floors, small-step stalls, and malformed control ranges.
AutoExposureDecision computeAutoExposure(Sensor sensor,
                                         float linearMean,
                                         const ControlRange& exposure,
                                         const ControlRange& gain);

// CAMSS RDI capture pipeline supporting multi-IFE (vfe0/vfe1/vfe3) and single-IFE fallback.
class Capture {
public:
    Capture(bool front, unsigned width, unsigned height, unsigned maxFps = 0);
    Capture(Sensor sensor, unsigned width, unsigned height, unsigned maxFps = 0);
    ~Capture();
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    static std::unique_ptr<Capture> openIr(unsigned width = 640,
                                           unsigned height = 480,
                                           unsigned maxFps = 0);
    Frame next(int timeoutMs = 2000);
    void autoExposure(float linearMean);
    Sensor sensor() const;
    uint32_t pixelFormat() const;
private:
    struct State;
    std::unique_ptr<State> s;
};
}
