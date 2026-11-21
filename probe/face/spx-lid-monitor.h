// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace spx::face {

struct LidStatusSnapshot {
    bool lidDeviceFound = false;
    bool tabletSwitchFound = false;
    bool rawSwLidClosed = false;
    bool rawSwTabletMode = false;
    std::string ssamCoverState = "unavailable";
    bool effectiveLidClosed = false;
};

// Returns true when SSAM cover posture string indicates keyboard folded behind screen
// ("folded-back", "folded-canvas", or "book"), where Hall SW_LID=1 is suppressed.
bool isSsamCoverFolded(const std::string& state);

// Compute whether the lid is genuinely closed given raw SW_LID and SSAM posture.
bool evaluateEffectiveLidClosed(bool rawSwLidClosed, const std::string& ssamCoverState);

// Read SSAM tablet switch posture from <ssamDevicesDir>/*/state
// (defaults to "/sys/bus/surface_aggregator/devices", with "/sys/bus/ssam/devices" fallback).
std::string readSsamCoverState(
    const std::string& ssamDevicesDir = "/sys/bus/surface_aggregator/devices");

class SpxLidMonitor {
public:
    using LidClosedCallback = std::function<void()>;

    SpxLidMonitor(std::string inputDir = "/dev/input",
                  std::string ssamDevicesDir = "/sys/bus/surface_aggregator/devices");
    ~SpxLidMonitor();

    SpxLidMonitor(const SpxLidMonitor&) = delete;
    SpxLidMonitor& operator=(const SpxLidMonitor&) = delete;

    void start();
    void stop();

    // Non-blocking snapshot (performs an immediate SSAM sysfs read when queried).
    LidStatusSnapshot snapshot();
    bool isLidClosed();

    void setOnLidClosedCallback(LidClosedCallback cb);

    // Deterministic state injection for host unit tests without root evdev nodes.
    void updateStateForTest(bool rawLidClosed,
                            bool rawTabletMode,
                            const std::string& ssamCoverState);

private:
    struct WatchedEvdev {
        std::string path;
        int fd = -1;
        bool hasLid = false;
        bool hasTabletMode = false;
        bool lidClosed = false;
        bool tabletMode = false;
    };

    void runLoop();
    void rescanEvdevsLocked(std::vector<WatchedEvdev>& devs);

    std::string inputDir_;
    std::string ssamDevicesDir_;

    std::mutex mu_;
    LidStatusSnapshot state_;
    LidClosedCallback onLidClosed_;
    bool running_ = false;
    int stopEventFd_ = -1;
    std::thread worker_;
};

}  // namespace spx::face
