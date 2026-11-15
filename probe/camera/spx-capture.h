// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace spx {
struct Frame {
    unsigned width, height, stride, sequence;
    uint64_t timestampNs;
    bool front;
    std::vector<uint8_t> raw10;
};
// One CAMSS RDI pipeline is shared by the two built-in cameras.
class Capture {
public:
    Capture(bool front, unsigned width, unsigned height, unsigned maxFps = 0);
    ~Capture();
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    Frame next(int timeoutMs = 2000);
    void autoExposure(float linearMean);
private:
    struct State;
    std::unique_ptr<State> s;
};
}
