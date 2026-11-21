// SPDX-License-Identifier: GPL-2.0-only
#include "spx-lid-monitor.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace spx::face {
namespace {

constexpr size_t kSwLongs = (SW_MAX / (sizeof(unsigned long) * 8u)) + 1u;

bool testBit(const unsigned long* bits, unsigned bit) {
    const unsigned word = bit / (sizeof(unsigned long) * 8u);
    const unsigned shift = bit % (sizeof(unsigned long) * 8u);
    return (bits[word] & (1ul << shift)) != 0;
}

std::string trimAscii(std::string s) {
    while (!s.empty() &&
           (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) ++start;
    return s.substr(start);
}

bool readSmallFile(const std::string& path, std::string& out) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return false;
    buf[n] = '\0';
    out = trimAscii(std::string(buf));
    return !out.empty();
}

}  // namespace

bool isSsamCoverFolded(const std::string& state) {
    return state == "folded-back" || state == "folded-canvas" || state == "book";
}

bool evaluateEffectiveLidClosed(bool rawSwLidClosed, const std::string& ssamCoverState) {
    if (!rawSwLidClosed) return false;
    if (isSsamCoverFolded(ssamCoverState)) return false;
    return true;
}

static std::string scanSsamDir(const std::string& dirPath) {
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return {};
    std::string found;
    struct dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string val;
        if (readSmallFile(dirPath + "/" + ent->d_name + "/state", val)) {
            if (val == "laptop" || val == "closed" || val == "disconnected" ||
                val == "folded-back" || val == "folded-canvas" || val == "book" ||
                val == "slate" || val == "tablet") {
                found = val;
                break;
            }
        }
    }
    closedir(dir);
    return found;
}

std::string readSsamCoverState(const std::string& ssamDevicesDir) {
    std::string found = scanSsamDir(ssamDevicesDir);
    if (found.empty() && ssamDevicesDir == "/sys/bus/surface_aggregator/devices") {
        found = scanSsamDir("/sys/bus/ssam/devices");
    }
    return found.empty() ? "unavailable" : found;
}

SpxLidMonitor::SpxLidMonitor(std::string inputDir, std::string ssamDevicesDir)
    : inputDir_(std::move(inputDir)), ssamDevicesDir_(std::move(ssamDevicesDir)) {
    std::vector<WatchedEvdev> probe;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (inputDir_ != ":test:") {
            rescanEvdevsLocked(probe);
            for (const auto& d : probe) {
                if (d.hasLid) {
                    state_.lidDeviceFound = true;
                    state_.rawSwLidClosed = state_.rawSwLidClosed || d.lidClosed;
                }
                if (d.hasTabletMode) {
                    state_.tabletSwitchFound = true;
                    state_.rawSwTabletMode = state_.rawSwTabletMode || d.tabletMode;
                }
            }
        }
        if (ssamDevicesDir_ != ":test:") {
            state_.ssamCoverState = readSsamCoverState(ssamDevicesDir_);
        }
        state_.effectiveLidClosed =
            evaluateEffectiveLidClosed(state_.rawSwLidClosed, state_.ssamCoverState);
    }
    for (auto& d : probe) {
        if (d.fd >= 0) close(d.fd);
    }
}

SpxLidMonitor::~SpxLidMonitor() {
    stop();
}

void SpxLidMonitor::start() {
    std::lock_guard<std::mutex> lk(mu_);
    if (running_) return;
    stopEventFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (stopEventFd_ < 0) return;
    running_ = true;
    worker_ = std::thread(&SpxLidMonitor::runLoop, this);
}

void SpxLidMonitor::stop() {
    std::thread toJoin;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!running_) return;
        running_ = false;
        if (stopEventFd_ >= 0) {
            uint64_t one = 1;
            ssize_t unused = write(stopEventFd_, &one, sizeof(one));
            (void)unused;
        }
        toJoin = std::move(worker_);
    }
    if (toJoin.joinable()) toJoin.join();
    std::lock_guard<std::mutex> lk(mu_);
    if (stopEventFd_ >= 0) {
        close(stopEventFd_);
        stopEventFd_ = -1;
    }
}

LidStatusSnapshot SpxLidMonitor::snapshot() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!running_ && inputDir_ != ":test:" && ssamDevicesDir_ != ":test:") {
        std::vector<WatchedEvdev> probe;
        state_.lidDeviceFound = false;
        state_.tabletSwitchFound = false;
        state_.rawSwLidClosed = false;
        state_.rawSwTabletMode = false;
        rescanEvdevsLocked(probe);
        for (const auto& d : probe) {
            if (d.hasLid) {
                state_.lidDeviceFound = true;
                state_.rawSwLidClosed = state_.rawSwLidClosed || d.lidClosed;
            }
            if (d.hasTabletMode) {
                state_.tabletSwitchFound = true;
                state_.rawSwTabletMode = state_.rawSwTabletMode || d.tabletMode;
            }
            if (d.fd >= 0) close(d.fd);
        }
    }
    if (ssamDevicesDir_ != ":test:" && (!running_ || state_.rawSwLidClosed)) {
        state_.ssamCoverState = readSsamCoverState(ssamDevicesDir_);
    }
    state_.effectiveLidClosed =
        evaluateEffectiveLidClosed(state_.rawSwLidClosed, state_.ssamCoverState);
    return state_;
}

bool SpxLidMonitor::isLidClosed() {
    return snapshot().effectiveLidClosed;
}

void SpxLidMonitor::setOnLidClosedCallback(LidClosedCallback cb) {
    std::lock_guard<std::mutex> lk(mu_);
    onLidClosed_ = std::move(cb);
}

void SpxLidMonitor::updateStateForTest(bool rawLidClosed,
                                       bool rawTabletMode,
                                       const std::string& ssamCoverState) {
    LidClosedCallback cb;
    {
        std::lock_guard<std::mutex> lk(mu_);
        const bool wasClosed = state_.effectiveLidClosed;
        state_.lidDeviceFound = true;
        state_.tabletSwitchFound = true;
        state_.rawSwLidClosed = rawLidClosed;
        state_.rawSwTabletMode = rawTabletMode;
        state_.ssamCoverState = ssamCoverState;
        state_.effectiveLidClosed =
            evaluateEffectiveLidClosed(state_.rawSwLidClosed, state_.ssamCoverState);
        if (!wasClosed && state_.effectiveLidClosed && onLidClosed_) {
            cb = onLidClosed_;
        }
    }
    if (cb) cb();
}

void SpxLidMonitor::rescanEvdevsLocked(std::vector<WatchedEvdev>& devs) {
    for (auto& d : devs) {
        if (d.fd >= 0) close(d.fd);
    }
    devs.clear();

    DIR* dir = opendir(inputDir_.c_str());
    if (!dir) return;
    struct dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (std::strncmp(ent->d_name, "event", 5) != 0) continue;
        const std::string path = inputDir_ + "/" + ent->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        unsigned long swCap[kSwLongs]{};
        if (ioctl(fd, EVIOCGBIT(EV_SW, sizeof(swCap)), swCap) < 0) {
            close(fd);
            continue;
        }
        const bool hasLid = testBit(swCap, SW_LID);
        const bool hasTablet = testBit(swCap, SW_TABLET_MODE);
        if (!hasLid && !hasTablet) {
            close(fd);
            continue;
        }

        unsigned long swVal[kSwLongs]{};
        if (ioctl(fd, EVIOCGSW(sizeof(swVal)), swVal) < 0) {
            std::memset(swVal, 0, sizeof(swVal));
        }

        WatchedEvdev item;
        item.path = path;
        item.fd = fd;
        item.hasLid = hasLid;
        item.hasTabletMode = hasTablet;
        item.lidClosed = hasLid && testBit(swVal, SW_LID);
        item.tabletMode = hasTablet && testBit(swVal, SW_TABLET_MODE);
        devs.push_back(std::move(item));
    }
    closedir(dir);
}

void SpxLidMonitor::runLoop() {
    int inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    int watchWd = -1;
    if (inotifyFd >= 0) {
        watchWd = inotify_add_watch(inotifyFd, inputDir_.c_str(), IN_CREATE | IN_DELETE | IN_ATTRIB);
    }

    std::vector<WatchedEvdev> devs;
    auto syncSnapshot = [&](bool fireCallback) {
        LidClosedCallback cb;
        {
            std::lock_guard<std::mutex> lk(mu_);
            const bool wasClosed = state_.effectiveLidClosed;
            state_.lidDeviceFound = false;
            state_.tabletSwitchFound = false;
            state_.rawSwLidClosed = false;
            state_.rawSwTabletMode = false;
            for (const auto& d : devs) {
                if (d.hasLid) {
                    state_.lidDeviceFound = true;
                    state_.rawSwLidClosed = state_.rawSwLidClosed || d.lidClosed;
                }
                if (d.hasTabletMode) {
                    state_.tabletSwitchFound = true;
                    state_.rawSwTabletMode = state_.rawSwTabletMode || d.tabletMode;
                }
            }
            if (ssamDevicesDir_ != ":test:") {
                state_.ssamCoverState = readSsamCoverState(ssamDevicesDir_);
            }
            state_.effectiveLidClosed =
                evaluateEffectiveLidClosed(state_.rawSwLidClosed, state_.ssamCoverState);
            if (fireCallback && !wasClosed && state_.effectiveLidClosed && onLidClosed_) {
                cb = onLidClosed_;
            }
        }
        if (cb) cb();
    };

    {
        std::lock_guard<std::mutex> lk(mu_);
        rescanEvdevsLocked(devs);
    }
    syncSnapshot(false);

    while (true) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (!running_) break;
        }

        std::vector<pollfd> pfds;
        pfds.push_back({stopEventFd_, POLLIN, 0});
        const size_t inotifyIdx = pfds.size();
        if (inotifyFd >= 0 && watchWd >= 0) {
            pfds.push_back({inotifyFd, POLLIN, 0});
        }
        const size_t devStart = pfds.size();
        for (const auto& d : devs) {
            pfds.push_back({d.fd, POLLIN, 0});
        }

        int rc = poll(pfds.data(), pfds.size(), -1);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pfds[0].revents & POLLIN) break;

        bool needRescan = false;
        if (inotifyFd >= 0 && watchWd >= 0 && pfds.size() > inotifyIdx &&
            (pfds[inotifyIdx].revents & POLLIN)) {
            char buf[512];
            while (read(inotifyFd, buf, sizeof(buf)) > 0) {
            }
            needRescan = true;
        }

        bool stateDirty = false;
        for (size_t i = 0; i < devs.size(); ++i) {
            const short rev = pfds[devStart + i].revents;
            if (rev & (POLLERR | POLLHUP | POLLNVAL)) {
                needRescan = true;
                continue;
            }
            if (!(rev & POLLIN)) continue;
            struct input_event ev{};
            while (true) {
                ssize_t n = read(devs[i].fd, &ev, sizeof(ev));
                if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    if (errno == EINTR) continue;
                    needRescan = true;
                    break;
                }
                if (n != static_cast<ssize_t>(sizeof(ev))) break;
                if (ev.type == EV_SW && ev.code == SW_LID) {
                    devs[i].lidClosed = (ev.value != 0);
                    stateDirty = true;
                } else if (ev.type == EV_SW && ev.code == SW_TABLET_MODE) {
                    devs[i].tabletMode = (ev.value != 0);
                    stateDirty = true;
                }
            }
        }

        if (needRescan) {
            std::lock_guard<std::mutex> lk(mu_);
            rescanEvdevsLocked(devs);
            stateDirty = true;
        }
        if (stateDirty) {
            syncSnapshot(true);
        }
    }

    for (auto& d : devs) {
        if (d.fd >= 0) close(d.fd);
    }
    if (inotifyFd >= 0) close(inotifyFd);
}

}  // namespace spx::face
