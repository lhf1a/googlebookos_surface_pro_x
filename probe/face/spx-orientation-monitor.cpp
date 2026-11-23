// SPDX-License-Identifier: GPL-2.0-only
#include "spx-orientation-monitor.h"

#include <aidl/android/frameworks/sensorservice/BnEventQueueCallback.h>
#include <aidl/android/frameworks/sensorservice/IEventQueue.h>
#include <aidl/android/frameworks/sensorservice/ISensorManager.h>
#include <aidl/android/hardware/sensors/Event.h>
#include <aidl/android/hardware/sensors/SensorInfo.h>
#include <aidl/android/hardware/sensors/SensorType.h>
#include <android/binder_manager.h>
#include <android/log.h>

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <mutex>

namespace spx::face {
namespace {

constexpr const char* kLogTag = "SpxFaceHal";
constexpr int32_t kAccelSamplingPeriodUs = 20000;  // 50 Hz
constexpr int64_t kMaxSampleAgeMs = 2000;

int64_t bootTimeMs() {
    struct timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000LL;
}

using aidl::android::frameworks::sensorservice::BnEventQueueCallback;
using aidl::android::frameworks::sensorservice::IEventQueue;
using aidl::android::frameworks::sensorservice::IEventQueueCallback;
using aidl::android::frameworks::sensorservice::ISensorManager;
using aidl::android::hardware::sensors::Event;
using aidl::android::hardware::sensors::SensorInfo;
using aidl::android::hardware::sensors::SensorType;

}  // namespace

struct SpxOrientationMonitor::Impl : public std::enable_shared_from_this<Impl> {
    class Callback final : public BnEventQueueCallback {
    public:
        explicit Callback(std::weak_ptr<Impl> owner) : owner_(std::move(owner)) {}

        ::ndk::ScopedAStatus onEvent(const Event& in_e) override {
            if (auto impl = owner_.lock()) {
                impl->handleEvent(in_e);
            }
            return ::ndk::ScopedAStatus::ok();
        }

    private:
        std::weak_ptr<Impl> owner_;
    };

    mutable std::mutex mu;
    std::condition_variable cv;

    size_t activeSessions = 0;
    bool streamEnabled = false;
    bool loggedUnavailable = false;

    std::shared_ptr<ISensorManager> manager;
    std::shared_ptr<IEventQueueCallback> callback;
    std::shared_ptr<IEventQueue> queue;
    int32_t accelHandle = -1;
    bool hasAccel = false;

    bool hasDelta = false;
    Rotation delta = Rotation::Deg0;
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    int64_t sampleMs = 0;

    void resetConnectionLocked() {
        queue.reset();
        callback.reset();
        manager.reset();
        accelHandle = -1;
        hasAccel = false;
        streamEnabled = false;
    }

    bool ensureQueueLocked() {
        if (manager && AIBinder_isAlive(manager->asBinder().get()) && queue &&
            AIBinder_isAlive(queue->asBinder().get()) && hasAccel) {
            return true;
        }
        resetConnectionLocked();

        const std::string instance = std::string(ISensorManager::descriptor) + "/default";
        ndk::SpAIBinder binder(AServiceManager_checkService(instance.c_str()));
        if (!binder.get()) {
            if (!loggedUnavailable) {
                __android_log_print(
                    ANDROID_LOG_WARN,
                    kLogTag,
                    "ISensorManager (%s) unavailable; falling back to visual multi-orientation",
                    instance.c_str());
                loggedUnavailable = true;
            }
            return false;
        }

        auto mgr = ISensorManager::fromBinder(binder);
        if (!mgr) {
            if (!loggedUnavailable) {
                __android_log_print(ANDROID_LOG_WARN, kLogTag, "ISensorManager::fromBinder failed");
                loggedUnavailable = true;
            }
            return false;
        }

        SensorInfo info{};
        const auto sensorStatus = mgr->getDefaultSensor(SensorType::ACCELEROMETER, &info);
        if (!sensorStatus.isOk() || info.type != SensorType::ACCELEROMETER) {
            if (!loggedUnavailable) {
                __android_log_print(
                    ANDROID_LOG_WARN,
                    kLogTag,
                    "ISensorManager::getDefaultSensor(ACCELEROMETER) failed: %s",
                    sensorStatus.getDescription().c_str());
                loggedUnavailable = true;
            }
            return false;
        }

        auto cb = ndk::SharedRefBase::make<Callback>(weak_from_this());
        std::shared_ptr<IEventQueue> q;
        const auto queueStatus = mgr->createEventQueue(cb, &q);
        if (!queueStatus.isOk() || !q) {
            if (!loggedUnavailable) {
                __android_log_print(
                    ANDROID_LOG_WARN,
                    kLogTag,
                    "ISensorManager::createEventQueue failed: %s",
                    queueStatus.getDescription().c_str());
                loggedUnavailable = true;
            }
            return false;
        }

        manager = std::move(mgr);
        callback = std::move(cb);
        queue = std::move(q);
        accelHandle = info.sensorHandle;
        hasAccel = true;
        loggedUnavailable = false;
        return true;
    }

    bool enableStreamLocked() {
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (!ensureQueueLocked()) return false;
            const auto st = queue->enableSensor(accelHandle, kAccelSamplingPeriodUs, 0);
            if (st.isOk()) {
                streamEnabled = true;
                return true;
            }
            __android_log_print(ANDROID_LOG_WARN,
                                kLogTag,
                                "IEventQueue::enableSensor(handle=%d) failed: %s",
                                accelHandle,
                                st.getDescription().c_str());
            resetConnectionLocked();
        }
        return false;
    }

    void disableStreamLocked() {
        if (queue && hasAccel && streamEnabled) {
            const auto st = queue->disableSensor(accelHandle);
            if (!st.isOk()) {
                __android_log_print(ANDROID_LOG_WARN,
                                    kLogTag,
                                    "IEventQueue::disableSensor(handle=%d) failed: %s",
                                    accelHandle,
                                    st.getDescription().c_str());
                resetConnectionLocked();
                return;
            }
        }
        streamEnabled = false;
    }

    void handleEvent(const Event& in_e) {
        if (in_e.sensorType != SensorType::ACCELEROMETER ||
            in_e.payload.getTag() != Event::EventPayload::Tag::vec3) {
            return;
        }
        const auto& v = in_e.payload.get<Event::EventPayload::Tag::vec3>();
        const int64_t nowMs = bootTimeMs();
        {
            std::lock_guard<std::mutex> lk(mu);
            Rotation nextDelta = delta;
            if (classifyGravityOrientation(v.x, v.y, v.z, hasDelta, delta, nextDelta)) {
                delta = nextDelta;
                hasDelta = true;
            }
            ax = v.x;
            ay = v.y;
            az = v.z;
            sampleMs = nowMs;
        }
        cv.notify_all();
    }
};

SpxOrientationMonitor::ScopedSession::ScopedSession(SpxOrientationMonitor* monitor)
    : monitor_(monitor) {
    if (monitor_) {
        monitor_->acquireSession();
    }
}

SpxOrientationMonitor::ScopedSession::~ScopedSession() {
    release();
}

SpxOrientationMonitor::ScopedSession::ScopedSession(ScopedSession&& other) noexcept
    : monitor_(other.monitor_) {
    other.monitor_ = nullptr;
}

SpxOrientationMonitor::ScopedSession& SpxOrientationMonitor::ScopedSession::operator=(
    ScopedSession&& other) noexcept {
    if (this != &other) {
        release();
        monitor_ = other.monitor_;
        other.monitor_ = nullptr;
    }
    return *this;
}

void SpxOrientationMonitor::ScopedSession::release() {
    if (monitor_) {
        monitor_->releaseSession();
        monitor_ = nullptr;
    }
}

SpxOrientationMonitor::SpxOrientationMonitor() : impl_(std::make_shared<Impl>()) {}

SpxOrientationMonitor::~SpxOrientationMonitor() {
    if (!impl_) return;
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->disableStreamLocked();
    impl_->resetConnectionLocked();
}

bool SpxOrientationMonitor::acquireSession() {
    if (!impl_) return false;
    std::lock_guard<std::mutex> lk(impl_->mu);
    ++impl_->activeSessions;
    if (impl_->streamEnabled) return true;
    return impl_->enableStreamLocked();
}

void SpxOrientationMonitor::releaseSession() {
    if (!impl_) return;
    std::lock_guard<std::mutex> lk(impl_->mu);
    if (impl_->activeSessions > 0) {
        --impl_->activeSessions;
    }
    if (impl_->activeSessions == 0) {
        impl_->disableStreamLocked();
    }
}

bool SpxOrientationMonitor::waitForFreshSample(int64_t minTimestampMs, int timeoutMs) {
    if (!impl_) return false;
    std::unique_lock<std::mutex> lk(impl_->mu);
    if (impl_->sampleMs >= minTimestampMs && impl_->hasDelta) {
        return true;
    }
    if (!impl_->streamEnabled || timeoutMs <= 0) {
        return impl_->sampleMs >= minTimestampMs && impl_->hasDelta;
    }
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    impl_->cv.wait_until(lk, deadline, [&] {
        return !impl_->streamEnabled || (impl_->sampleMs >= minTimestampMs && impl_->hasDelta);
    });
    return impl_->sampleMs >= minTimestampMs && impl_->hasDelta;
}

OrientationSnapshot SpxOrientationMonitor::snapshot() const {
    OrientationSnapshot out;
    if (!impl_) return out;
    const int64_t nowMs = bootTimeMs();
    std::lock_guard<std::mutex> lk(impl_->mu);
    out.ax = impl_->ax;
    out.ay = impl_->ay;
    out.az = impl_->az;
    out.sampleMs = impl_->sampleMs;
    out.delta = impl_->delta;
    out.valid = impl_->hasDelta && impl_->sampleMs > 0 &&
                (nowMs - impl_->sampleMs) <= kMaxSampleAgeMs;
    return out;
}

}  // namespace spx::face
