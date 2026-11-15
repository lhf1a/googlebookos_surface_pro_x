// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "spx-capture.h"
#include <array>
namespace spx {
struct Image {
    unsigned width, height;
    std::vector<uint8_t> rgb;
    float linearMean;
};
class Isp {
public:
    Image process(const Frame&, unsigned width, unsigned height);
    void prepare(const Frame&, bool fullResolution);
    Image render(unsigned width, unsigned height) const;
private:
    float redGain = 1, blueGain = 1, mean = 0;
    unsigned bw = 0, bh = 0;
    std::vector<std::array<float,3>> cells;
};
}
