// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "spx-capture.h"
#include <array>
#include <cstdint>
#include <vector>

namespace spx {
struct Image {
    unsigned width, height;
    std::vector<uint8_t> rgb;
    float linearMean;
};
class Isp {
public:
    using Ccm = std::array<float, 9>;
    Image process(const Frame&, unsigned width, unsigned height);
    void prepare(const Frame&, bool fullResolution);
    Image render(unsigned width, unsigned height) const;

    static float estimateColorTemperature(float redGain, float blueGain);
    static Ccm ccmForColorTemperature(float kelvin);

    float colorTemperature() const { return colorTempK; }
    Ccm currentCcm() const { return ccmForColorTemperature(colorTempK); }
    float currentRedGain() const { return redGain; }
    float currentBlueGain() const { return blueGain; }
private:
    float redGain = 1.f, blueGain = 1.f, mean = 0.f, colorTempK = 4200.f;
    unsigned bw = 0, bh = 0;
    // Interleaved linear RGB in Q5 fixed-point ([0, 32736] = [0, 1023] * 32).
    std::vector<uint16_t> cells;
    // Reusable 3-row ring buffer for zero-allocation steady-state full-res demosaic.
    std::vector<uint16_t> scratchRows;
};
}
