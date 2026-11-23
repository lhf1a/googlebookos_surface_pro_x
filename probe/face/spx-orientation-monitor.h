// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "spx-ir-preprocess.h"

#include <cstdint>
#include <memory>

namespace spx::face {

struct OrientationSnapshot {
    bool valid = false;
    Rotation delta = Rotation::Deg0;
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    int64_t sampleMs = 0;
};

// On-demand accelerometer monitor backed by android.frameworks.sensorservice.ISensorManager.
// Only enables SensorType::ACCELEROMETER while at least one ScopedSession is active, ensuring
// zero idle sensor power outside active face enroll()/authenticate() sessions.
class SpxOrientationMonitor {
public:
    class ScopedSession {
    public:
        explicit ScopedSession(SpxOrientationMonitor* monitor = nullptr);
        ~ScopedSession();

        ScopedSession(const ScopedSession&) = delete;
        ScopedSession& operator=(const ScopedSession&) = delete;
        ScopedSession(ScopedSession&& other) noexcept;
        ScopedSession& operator=(ScopedSession&& other) noexcept;

        void release();

    private:
        SpxOrientationMonitor* monitor_ = nullptr;
    };

    SpxOrientationMonitor();
    ~SpxOrientationMonitor();

    SpxOrientationMonitor(const SpxOrientationMonitor&) = delete;
    SpxOrientationMonitor& operator=(const SpxOrientationMonitor&) = delete;

    bool acquireSession();
    void releaseSession();

    // Wait up to timeoutMs for an accelerometer sample recorded at or after minTimestampMs.
    bool waitForFreshSample(int64_t minTimestampMs, int timeoutMs);

    // Non-blocking snapshot; valid is true only when a sample was received within the last 2s
    // and gravity classification succeeded (either dominant axis or prior orientation while flat).
    OrientationSnapshot snapshot() const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace spx::face
