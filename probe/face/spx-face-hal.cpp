// SPDX-License-Identifier: GPL-2.0-only
/* Surface Pro X Android 17 IFace AIDL V4 HAL (Class 2 / WEAK)
 * Hardware: OV7251 640x480 Y10/Y10P 850nm active IR + TLMM GPIO 121 Hall SW_LID.
 *
 * Security & Power Policy:
 * - No QSEE fTPM; declares SensorStrength::WEAK (Class 2).
 * - Zero idle camera power: Capture opens strictly inside enroll()/authenticate()
 *   and is destroyed before returning final result/error.
 * - Real-time SW_LID=1 / FoldState::FULLY_CLOSED gate & immediate stream abort
 *   with zero lockout penalty.
 * - Hard emergency watchdog: 8s authenticate, 45s enroll.
 */
#include "../camera/spx-capture.h"
#include "spx-face-crypto.h"
#include "spx-face-engine.h"
#include "spx-ir-preprocess.h"
#include "spx-lid-monitor.h"

#include <aidl/android/hardware/biometrics/common/BnCancellationSignal.h>
#include <aidl/android/hardware/biometrics/common/DisplayState.h>
#include <aidl/android/hardware/biometrics/common/FoldState.h>
#include <aidl/android/hardware/biometrics/face/BnFace.h>
#include <aidl/android/hardware/biometrics/face/BnSession.h>
#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/log.h>
#include <android/native_window.h>
#include <sys/system_properties.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace aidl::android::hardware::biometrics::face {
namespace {

constexpr const char* kLogTag = "SpxFaceHal";
constexpr int32_t kSensorId = 1;
constexpr int32_t kPreviewWidth = 480;
constexpr int32_t kPreviewHeight = 640;
constexpr int64_t kAuthSessionDeadlineMs = 7000;
constexpr int64_t kAuthNoFaceTimeoutMs = 4500;
constexpr int64_t kAuthWatchdogHardMs = 8000;
constexpr int64_t kEnrollWatchdogHardMs = 45000;
constexpr int64_t kEnrollWarmupMs = 500;
constexpr int64_t kEnrollMinSampleIntervalMs = 280;
constexpr int64_t kChallengeTtlMs = 300000;
constexpr const char* kOptionalModelPath = "/vendor/etc/spx-face/model.bin";

void logInfo(const char* msg) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", msg);
}

void logWarn(const char* msg) {
    __android_log_print(ANDROID_LOG_WARN, kLogTag, "%s", msg);
}

int64_t bootTimeMs() {
    struct timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000LL;
}

std::string getSystemProperty(const char* name, const char* fallback) {
    char buf[PROP_VALUE_MAX]{};
    int len = __system_property_get(name, buf);
    if (len > 0) return std::string(buf, static_cast<size_t>(len));
    return fallback ? std::string(fallback) : std::string();
}

bool isFaceUnlockEnabled() {
    return getSystemProperty("persist.spx.face.enabled", "1") != "0";
}

spx::face::Rotation currentSensorRotation(std::string* outStr = nullptr) {
    const std::string val = getSystemProperty("persist.spx.face.rotation", "270");
    if (outStr) *outStr = val;
    return spx::face::parseRotationDegrees(val.c_str(), spx::face::Rotation::Deg270CW);
}

float computeIrAeMetric(const spx::face::IrFrameStats& stats) {
    float aeMetric = stats.roiLinearMean;
    if (stats.p99 >= 920u) {
        aeMetric = std::max(aeMetric, 0.34f + 0.40f * stats.saturatedRatio);
    }
    return aeMetric;
}

AcquiredInfo mapQualityToAcquired(spx::face::FaceQuality q) {
    using Q = spx::face::FaceQuality;
    switch (q) {
    case Q::GOOD:
        return AcquiredInfo::GOOD;
    case Q::NOT_DETECTED:
        return AcquiredInfo::NOT_DETECTED;
    case Q::TOO_DARK:
        return AcquiredInfo::TOO_DARK;
    case Q::TOO_BRIGHT:
        return AcquiredInfo::TOO_BRIGHT;
    case Q::TOO_CLOSE:
        return AcquiredInfo::TOO_CLOSE;
    case Q::TOO_FAR:
        return AcquiredInfo::TOO_FAR;
    case Q::FACE_TOO_LEFT:
        return AcquiredInfo::FACE_TOO_LEFT;
    case Q::FACE_TOO_RIGHT:
        return AcquiredInfo::FACE_TOO_RIGHT;
    case Q::FACE_TOO_HIGH:
        return AcquiredInfo::FACE_TOO_HIGH;
    case Q::FACE_TOO_LOW:
        return AcquiredInfo::FACE_TOO_LOW;
    case Q::PAN_TOO_EXTREME:
        return AcquiredInfo::PAN_TOO_EXTREME;
    case Q::TILT_TOO_EXTREME:
        return AcquiredInfo::TILT_TOO_EXTREME;
    case Q::TOO_MUCH_MOTION:
        return AcquiredInfo::TOO_MUCH_MOTION;
    case Q::SPOOF_DETECTED:
        return AcquiredInfo::INSUFFICIENT;
    }
    return AcquiredInfo::UNKNOWN;
}

class SpxCancellationSignal final : public common::BnCancellationSignal {
public:
    explicit SpxCancellationSignal(std::shared_ptr<std::atomic<bool>> flag)
        : flag_(std::move(flag)) {}

    ::ndk::ScopedAStatus cancel() override {
        if (flag_) flag_->store(true);
        return ::ndk::ScopedAStatus::ok();
    }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

void renderPreviewFrame(ANativeWindow* window,
                        const std::vector<uint8_t>& gray8,
                        unsigned width,
                        unsigned height,
                        bool& loggedOnce,
                        int& lockFailCount) {
    if (!window || gray8.empty() || width == 0 || height == 0 || lockFailCount >= 3) return;
    ANativeWindow_Buffer buf{};
    const int lockRc = ANativeWindow_lock(window, &buf, nullptr);
    if (lockRc != 0 || !buf.bits) {
        if (lockRc == -ENODEV) {
            lockFailCount = 3;
        } else {
            ++lockFailCount;
        }
        if (lockFailCount >= 3) {
            __android_log_print(
                ANDROID_LOG_WARN,
                kLogTag,
                "preview ANativeWindow_lock failed rc=%d; disabling preview for session",
                lockRc);
        }
        return;
    }
    lockFailCount = 0;
    if (!loggedOnce) {
        __android_log_print(ANDROID_LOG_INFO,
                            kLogTag,
                            "preview locked buf=%dx%d stride=%d fmt=%d src=%ux%u",
                            buf.width,
                            buf.height,
                            buf.stride,
                            buf.format,
                            width,
                            height);
        loggedOnce = true;
    }
    if (buf.width > 0 && buf.height > 0 && buf.stride >= buf.width) {
        constexpr float kPreviewZoom = 1.35f;
        if (buf.format == WINDOW_FORMAT_RGBA_8888 || buf.format == WINDOW_FORMAT_RGBX_8888) {
            spx::face::renderIrPreviewRgba(gray8.data(),
                                           width,
                                           height,
                                           static_cast<uint8_t*>(buf.bits),
                                           static_cast<unsigned>(buf.width),
                                           static_cast<unsigned>(buf.height),
                                           static_cast<unsigned>(buf.stride),
                                           true,
                                           kPreviewZoom);
        } else if (buf.format == WINDOW_FORMAT_RGB_565) {
            spx::face::renderIrPreviewRgb565(gray8.data(),
                                             width,
                                             height,
                                             static_cast<uint16_t*>(buf.bits),
                                             static_cast<unsigned>(buf.width),
                                             static_cast<unsigned>(buf.height),
                                             static_cast<unsigned>(buf.stride),
                                             true,
                                             kPreviewZoom);
        }
    }
    ANativeWindow_unlockAndPost(window);
}

class SpxSession final : public BnSession,
                         public std::enable_shared_from_this<SpxSession> {
public:
    SpxSession(int32_t userId,
               std::shared_ptr<ISessionCallback> cb,
               std::shared_ptr<spx::face::SpxLidMonitor> lidMonitor)
        : userId_(userId),
          cb_(std::move(cb)),
          lidMonitor_(std::move(lidMonitor)),
          store_(userId) {
        if (!store_.load()) {
            logWarn("Failed loading face store; continuing with empty template set");
        }
        lockout_.setFailedAttempts(store_.failedAttempts());
        if (!spx::face::csprngBytes(hatKey_.data(), hatKey_.size())) {
            hatKey_.fill(0x5Au);
        }
        worker_ = std::thread(&SpxSession::workerLoop, this);
    }

    ~SpxSession() override {
        shutdownWorker();
        model_.unload();
    }

    void attachClientDeathRecipient() {
        deathRecipient_ = AIBinder_DeathRecipient_new(&SpxSession::onBinderDied);
        if (!deathRecipient_) return;
        AIBinder_DeathRecipient_setOnUnlinked(deathRecipient_, &SpxSession::onBinderUnlinked);
        auto* cookie = new std::weak_ptr<SpxSession>(weak_from_this());
        if (AIBinder_linkToDeath(cb_->asBinder().get(), deathRecipient_, cookie) == STATUS_OK) {
            deathCookie_ = cookie;
        } else {
            delete cookie;
        }
    }

    ::ndk::ScopedAStatus generateChallenge() override {
        enqueue([this] {
            pruneExpiredChallenges();
            const int64_t ch = spx::face::csprngInt64NonZero();
            challenges_[ch] = bootTimeMs() + kChallengeTtlMs;
            cb_->onChallengeGenerated(ch);
        });
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus revokeChallenge(int64_t in_challenge) override {
        enqueue([this, in_challenge] {
            challenges_.erase(in_challenge);
            cb_->onChallengeRevoked(in_challenge);
        });
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus getEnrollmentConfig(
        EnrollmentType /*in_enrollmentType*/,
        std::vector<EnrollmentStageConfig>* _aidl_return) override {
        if (!_aidl_return) return ::ndk::ScopedAStatus::fromStatus(STATUS_BAD_VALUE);
        *_aidl_return = {
            {EnrollmentStage::FIRST_FRAME_RECEIVED, {}},
            {EnrollmentStage::WAITING_FOR_CENTERING, {}},
            {EnrollmentStage::HOLD_STILL_IN_CENTER, {}},
            {EnrollmentStage::ENROLLING_MOVEMENT_1, {}},
            {EnrollmentStage::ENROLLING_MOVEMENT_2, {}},
            {EnrollmentStage::ENROLLMENT_FINISHED, {}},
        };
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus enroll(
        const keymaster::HardwareAuthToken& in_hat,
        EnrollmentType in_type,
        const std::vector<Feature>& in_features,
        const std::optional<::aidl::android::hardware::common::NativeHandle>& /*in_previewSurface*/,
        std::shared_ptr<common::ICancellationSignal>* _aidl_return) override {
        return startEnroll(in_hat, in_type, in_features, nullptr, std::nullopt, _aidl_return);
    }

    ::ndk::ScopedAStatus enrollWithContext(
        const keymaster::HardwareAuthToken& in_hat,
        EnrollmentType in_type,
        const std::vector<Feature>& in_features,
        const std::optional<::aidl::android::hardware::common::NativeHandle>& /*in_previewSurface*/,
        const common::OperationContext& in_context,
        std::shared_ptr<common::ICancellationSignal>* _aidl_return) override {
        return startEnroll(in_hat, in_type, in_features, nullptr, in_context, _aidl_return);
    }

    ::ndk::ScopedAStatus enrollWithOptions(
        const FaceEnrollOptions& in_options,
        std::shared_ptr<common::ICancellationSignal>* _aidl_return) override {
        std::shared_ptr<ANativeWindow> window;
        if (in_options.surfacePreview.has_value() && in_options.surfacePreview->get()) {
            ANativeWindow* raw = in_options.surfacePreview->get();
            ANativeWindow_acquire(raw);
            window = std::shared_ptr<ANativeWindow>(raw, &ANativeWindow_release);
        }
        return startEnroll(in_options.hardwareAuthToken,
                           in_options.enrollmentType,
                           in_options.features,
                           std::move(window),
                           in_options.context,
                           _aidl_return);
    }

    ::ndk::ScopedAStatus authenticate(
        int64_t in_operationId,
        std::shared_ptr<common::ICancellationSignal>* _aidl_return) override {
        return startAuthenticate(in_operationId, std::nullopt, _aidl_return);
    }

    ::ndk::ScopedAStatus authenticateWithContext(
        int64_t in_operationId,
        const common::OperationContext& in_context,
        std::shared_ptr<common::ICancellationSignal>* _aidl_return) override {
        return startAuthenticate(in_operationId, in_context, _aidl_return);
    }

    ::ndk::ScopedAStatus detectInteraction(
        std::shared_ptr<common::ICancellationSignal>* _aidl_return) override {
        return startDetectInteraction(_aidl_return);
    }

    ::ndk::ScopedAStatus detectInteractionWithContext(
        const common::OperationContext& /*in_context*/,
        std::shared_ptr<common::ICancellationSignal>* _aidl_return) override {
        return startDetectInteraction(_aidl_return);
    }

    ::ndk::ScopedAStatus onContextChanged(
        const common::OperationContext& in_context) override {
        std::lock_guard<std::mutex> lk(opMu_);
        syncContextLocked(in_context);
        if ((latestFoldClosed_ || latestDisplayInactive_) && activeCancel_) {
            activeCancel_->store(true);
        }
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus enumerateEnrollments() override {
        enqueue([this] { cb_->onEnrollmentsEnumerated(store_.listEnrollmentIds()); });
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus removeEnrollments(const std::vector<int32_t>& in_enrollmentIds) override {
        enqueue([this, in_enrollmentIds] {
            auto removed = store_.removeEnrollments(in_enrollmentIds);
            cb_->onEnrollmentsRemoved(removed);
        });
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus getFeatures() override {
        enqueue([this] {
            std::vector<Feature> feats;
            // Android 17 FaceGetFeatureClient allocates int[1] for REQUIRE_ATTENTION only;
            // advertising additional features grows featureMap to size 2 and crashes system_server.
            if (store_.hasFeatureBit(1u)) feats.push_back(Feature::REQUIRE_ATTENTION);
            cb_->onFeaturesRetrieved(feats);
        });
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus setFeature(const keymaster::HardwareAuthToken& in_hat,
                                    Feature in_feature,
                                    bool in_enabled) override {
        enqueue([this, in_hat, in_feature, in_enabled] {
            pruneExpiredChallenges();
            if (!validateHat(in_hat)) {
                cb_->onError(Error::UNABLE_TO_PROCESS, 0);
                return;
            }
            if (in_feature == Feature::REQUIRE_ATTENTION) {
                store_.setFeatureBit(1u, in_enabled);
            } else if (in_feature == Feature::REQUIRE_DIVERSE_POSES) {
                store_.setFeatureBit(2u, in_enabled);
            }
            cb_->onFeatureSet(in_feature);
        });
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus getAuthenticatorId() override {
        enqueue([this] { cb_->onAuthenticatorIdRetrieved(store_.authenticatorId()); });
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus invalidateAuthenticatorId() override {
        enqueue([this] {
            const int64_t newId = store_.invalidateAuthenticatorId();
            cb_->onAuthenticatorIdInvalidated(newId);
        });
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus resetLockout(const keymaster::HardwareAuthToken& in_hat) override {
        enqueue([this, in_hat] {
            if (in_hat.mac.empty()) {
                cb_->onError(Error::UNABLE_TO_PROCESS, 0);
                return;
            }
            lockout_.reset();
            store_.setFailedAttempts(0);
            cb_->onLockoutCleared();
        });
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus close() override {
        bool alreadyClosed = closed_.exchange(true);
        if (!alreadyClosed) {
            cancelActiveOp();
            shutdownWorker();
            model_.unload();
            if (cb_) cb_->onSessionClosed();
            if (deathRecipient_) {
                if (deathCookie_ && cb_) {
                    AIBinder_unlinkToDeath(cb_->asBinder().get(), deathRecipient_, deathCookie_);
                    deathCookie_ = nullptr;
                }
                AIBinder_DeathRecipient_delete(deathRecipient_);
                deathRecipient_ = nullptr;
            }
        }
        return ::ndk::ScopedAStatus::ok();
    }

private:
    static void onBinderDied(void* cookie) {
        auto* weakPtr = static_cast<std::weak_ptr<SpxSession>*>(cookie);
        if (!weakPtr) return;
        if (auto session = weakPtr->lock()) {
            logWarn("FaceService binder client died; closing session and stopping IR");
            session->close();
        }
    }

    static void onBinderUnlinked(void* cookie) {
        auto* weakPtr = static_cast<std::weak_ptr<SpxSession>*>(cookie);
        delete weakPtr;
    }

    void cancelActiveOp() {
        std::lock_guard<std::mutex> lk(opMu_);
        if (activeCancel_) activeCancel_->store(true);
    }

    void enqueue(std::function<void()> fn) {
        if (closed_.load()) return;
        {
            std::lock_guard<std::mutex> lk(queueMu_);
            if (stopping_) return;
            queue_.push_back(std::move(fn));
        }
        queueCv_.notify_one();
    }

    void shutdownWorker() {
        cancelActiveOp();
        {
            std::lock_guard<std::mutex> lk(queueMu_);
            stopping_ = true;
        }
        queueCv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    void workerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(queueMu_);
                queueCv_.wait(lk, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_ && queue_.empty()) break;
                task = std::move(queue_.front());
                queue_.pop_front();
            }
            if (task) task();
        }
    }

    void pruneExpiredChallenges() {
        const int64_t now = bootTimeMs();
        for (auto it = challenges_.begin(); it != challenges_.end();) {
            if (it->second <= now) {
                it = challenges_.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool validateHat(const keymaster::HardwareAuthToken& hat) {
        std::set<int64_t> active;
        for (const auto& [ch, _exp] : challenges_) active.insert(ch);
        if (active.empty()) return false;
        return spx::face::isPlausibleIncomingHat(
            hat.mac.data(), hat.mac.size(), hat.challenge, &active);
    }

    void syncContextLocked(const common::OperationContext& ctx) {
        latestFoldClosed_ = (ctx.foldState == common::FoldState::FULLY_CLOSED);
        latestDisplayInactive_ =
            (ctx.displayState == common::DisplayState::NO_UI ||
             ctx.displayState == common::DisplayState::SCREENSAVER ||
             ctx.displayState == common::DisplayState::AOD);
    }

    bool shouldGateOperation(const std::optional<common::OperationContext>& ctx) {
        if (!isFaceUnlockEnabled()) return true;
        if (lidMonitor_ && lidMonitor_->isLidClosed()) return true;
        std::lock_guard<std::mutex> lk(opMu_);
        if (ctx.has_value()) {
            syncContextLocked(*ctx);
        }
        return latestFoldClosed_ || latestDisplayInactive_;
    }

    std::shared_ptr<std::atomic<bool>> registerCancelFlag() {
        auto flag = std::make_shared<std::atomic<bool>>(false);
        std::lock_guard<std::mutex> lk(opMu_);
        activeCancel_ = flag;
        return flag;
    }

    void clearCancelFlag(const std::shared_ptr<std::atomic<bool>>& flag) {
        std::lock_guard<std::mutex> lk(opMu_);
        if (activeCancel_ == flag) activeCancel_.reset();
    }

    ::ndk::ScopedAStatus startDetectInteraction(
        std::shared_ptr<common::ICancellationSignal>* _aidl_return) {
        auto flag = registerCancelFlag();
        *_aidl_return = SharedRefBase::make<SpxCancellationSignal>(flag);
        enqueue([this, flag] {
            clearCancelFlag(flag);
            cb_->onError(Error::UNABLE_TO_PROCESS, 0);
        });
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus startEnroll(
        const keymaster::HardwareAuthToken& hat,
        EnrollmentType /*type*/,
        const std::vector<Feature>& features,
        std::shared_ptr<ANativeWindow> previewWindow,
        std::optional<common::OperationContext> ctx,
        std::shared_ptr<common::ICancellationSignal>* _aidl_return) {
        auto flag = registerCancelFlag();
        *_aidl_return = SharedRefBase::make<SpxCancellationSignal>(flag);
        const bool requireDiversity =
            std::find(features.begin(), features.end(), Feature::REQUIRE_DIVERSE_POSES) !=
            features.end();

        enqueue([this,
                 hat,
                 previewWindow = std::move(previewWindow),
                 ctx,
                 flag,
                 requireDiversity]() mutable {
            if (!model_.loaded) model_.loadFromFile(kOptionalModelPath);
            if (flag->load() || closed_.load() || shouldGateOperation(ctx)) {
                logWarn("enroll gated or canceled before start");
                clearCancelFlag(flag);
                cb_->onError(Error::CANCELED, 0);
                return;
            }
            pruneExpiredChallenges();
            if (!validateHat(hat)) {
                logWarn("enroll rejected invalid HAT");
                clearCancelFlag(flag);
                cb_->onError(Error::UNABLE_TO_PROCESS, 0);
                return;
            }
            if (store_.enrollmentCount() >= spx::face::kMaxEnrollmentsPerUser) {
                logWarn("enroll rejected: no space");
                clearCancelFlag(flag);
                cb_->onError(Error::NO_SPACE, 0);
                return;
            }

            std::string rotStr;
            const auto rot = currentSensorRotation(&rotStr);
            __android_log_print(ANDROID_LOG_INFO,
                                kLogTag,
                                "enroll start hasPreview=%d rot=%s requireDiversity=%d",
                                previewWindow ? 1 : 0,
                                rotStr.c_str(),
                                requireDiversity ? 1 : 0);

            if (previewWindow) {
                const int geomRc = ANativeWindow_setBuffersGeometry(previewWindow.get(),
                                                                    kPreviewWidth,
                                                                    kPreviewHeight,
                                                                    WINDOW_FORMAT_RGBA_8888);
                if (geomRc != 0) {
                    __android_log_print(ANDROID_LOG_WARN,
                                        kLogTag,
                                        "ANativeWindow_setBuffersGeometry(%dx%d) rc=%d",
                                        kPreviewWidth,
                                        kPreviewHeight,
                                        geomRc);
                }
            }

            std::vector<spx::face::Embedding> samples;
            samples.reserve(spx::face::kSamplesPerEnrollment);
            bool canceledByLidOrUser = false;
            bool timedOut = false;
            int64_t startMs = bootTimeMs();
            unsigned frameIdx = 0;

            try {
                auto cap = spx::Capture::openIr(640, 480, 30);
                startMs = bootTimeMs();
                spx::face::FaceSessionAccumulator acc;
                acc.reset(2);

                // Emergency hard watchdog thread ensures V4L2/850nm strobe closes <= 45s.
                std::atomic<bool> finished{false};
                std::thread watchdog([&] {
                    while (!finished.load()) {
                        if (bootTimeMs() - startMs >= kEnrollWatchdogHardMs ||
                            (lidMonitor_ && lidMonitor_->isLidClosed())) {
                            flag->store(true);
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                });

                const int32_t prospectiveId =
                    store_.listEnrollmentIds().empty() ? 1 : (store_.listEnrollmentIds().back() + 1);
                int64_t lastSampleMs = 0;
                int64_t lastDiagMs = startMs;
                bool previewLogged = false;
                int lockFailCount = 0;
                float anchorBoxW = 0.0f;
                int inconsistentStreak = 0;

                while (samples.size() < spx::face::kSamplesPerEnrollment) {
                    if (flag->load() || closed_.load() ||
                        (lidMonitor_ && lidMonitor_->isLidClosed())) {
                        canceledByLidOrUser = true;
                        break;
                    }
                    if (bootTimeMs() - startMs >= kEnrollWatchdogHardMs) {
                        timedOut = true;
                        break;
                    }
                    auto frame = cap->next(250);
                    const auto stats = spx::face::preprocessIrFrame(
                        frame.raw10.data(),
                        frame.raw10.size(),
                        frame.width,
                        frame.height,
                        frame.stride,
                        frame.pixelFormat ? frame.pixelFormat : spx::face::kPixFmtY10,
                        rot);
                    cap->autoExposure(computeIrAeMetric(stats));

                    if (previewWindow) {
                        renderPreviewFrame(previewWindow.get(),
                                           stats.gray8,
                                           stats.width,
                                           stats.height,
                                           previewLogged,
                                           lockFailCount);
                    }

                    if (frameIdx++ < 2u) {
                        EnrollmentFrame ef{};
                        ef.stage = EnrollmentStage::FIRST_FRAME_RECEIVED;
                        ef.data.acquiredInfo = AcquiredInfo::FIRST_FRAME_RECEIVED;
                        cb_->onEnrollmentFrame(ef);
                        continue;
                    }

                    const int64_t nowMs = bootTimeMs();
                    if (nowMs - startMs < kEnrollWarmupMs) {
                        EnrollmentFrame ef{};
                        ef.stage = EnrollmentStage::WAITING_FOR_CENTERING;
                        ef.data.acquiredInfo = AcquiredInfo::GOOD;
                        ef.data.isCancellable = true;
                        cb_->onEnrollmentFrame(ef);
                        continue;
                    }

                    const auto det = spx::face::detectFaceNir(stats);
                    EnrollmentFrame ef{};
                    ef.data.acquiredInfo = mapQualityToAcquired(det.quality);
                    ef.data.pan = det.panDeg;
                    ef.data.tilt = det.tiltDeg;
                    ef.data.distance = det.distanceCm;
                    ef.data.isCancellable = true;

                    auto maybeLogDiag = [&](int liveStatus, float c2r, float hf) {
                        if ((nowMs - lastDiagMs) >= 1000) {
                            lastDiagMs = nowMs;
                            const float interOcular =
                                det.detected ? std::hypot(det.rightEye.x - det.leftEye.x,
                                                          det.rightEye.y - det.leftEye.y)
                                             : 0.0f;
                            __android_log_print(
                                ANDROID_LOG_INFO,
                                kLogTag,
                                "enroll diag frame=%u samples=%zu det=%d q=%d boxW=%.0f io=%.1f "
                                "score=%.1f live=%d c2r=%.2f hf=%.2f p50=%u p99=%u roi=%.3f",
                                frameIdx,
                                samples.size(),
                                det.detected ? 1 : 0,
                                static_cast<int>(det.quality),
                                det.box.w,
                                interOcular,
                                det.score,
                                liveStatus,
                                c2r,
                                hf,
                                stats.p50,
                                stats.p99,
                                stats.roiLinearMean);
                        }
                    };

                    if (!det.detected || det.quality != spx::face::FaceQuality::GOOD) {
                        maybeLogDiag(-1, 0.0f, 0.0f);
                        ef.stage = EnrollmentStage::WAITING_FOR_CENTERING;
                        cb_->onEnrollmentFrame(ef);
                        continue;
                    }

                    const auto emb = spx::face::extractEmbedding(stats, det, &model_);
                    const auto live =
                        spx::face::evaluateLiveness(stats, det, emb, &acc, true);
                    maybeLogDiag(static_cast<int>(live.status),
                                 live.centerToRimRatio,
                                 live.halftoneRatio);
                    if (!live.live) {
                        ef.data.acquiredInfo =
                            (live.status == spx::face::LivenessStatus::REJECT_MOTION_BLUR)
                                ? AcquiredInfo::TOO_MUCH_MOTION
                                : AcquiredInfo::INSUFFICIENT;
                        ef.stage = EnrollmentStage::HOLD_STILL_IN_CENTER;
                        cb_->onEnrollmentFrame(ef);
                        continue;
                    }

                    if (lastSampleMs != 0 && (nowMs - lastSampleMs) < kEnrollMinSampleIntervalMs) {
                        ef.stage = EnrollmentStage::HOLD_STILL_IN_CENTER;
                        ef.data.acquiredInfo = AcquiredInfo::GOOD;
                        cb_->onEnrollmentFrame(ef);
                        continue;
                    }

                    if (!samples.empty()) {
                        const bool scaleOk =
                            det.box.w >= 0.72f * anchorBoxW && det.box.w <= 1.38f * anchorBoxW;
                        const bool simOk =
                            spx::face::cosineSimilarity(samples.front(), emb) >= 0.65f;
                        if (!scaleOk || !simOk) {
                            ++inconsistentStreak;
                            if (samples.size() == 1 && inconsistentStreak >= 4) {
                                samples.clear();
                                anchorBoxW = 0.0f;
                                inconsistentStreak = 0;
                            } else {
                                ef.stage = EnrollmentStage::HOLD_STILL_IN_CENTER;
                                ef.data.acquiredInfo = AcquiredInfo::GOOD;
                                cb_->onEnrollmentFrame(ef);
                                continue;
                            }
                        }
                    }
                    inconsistentStreak = 0;
                    if (samples.empty()) {
                        anchorBoxW = det.box.w;
                    }

                    samples.push_back(emb);
                    lastSampleMs = nowMs;
                    const size_t groupIdx =
                        std::min(samples.size() - 1u, static_cast<size_t>(5));
                    const int32_t remaining =
                        static_cast<int32_t>(spx::face::kSamplesPerEnrollment - samples.size());
                    static constexpr int32_t kBucketGroups[6][5] = {
                        {113, 108, 112, 114, 118},
                        {103, 107, 109, 117, 0},
                        {119, 123, 102, 104, 0},
                        {106, 110, 116, 120, 0},
                        {122, 124, 101, 105, 0},
                        {111, 115, 121, 125, 0},
                    };
                    for (int32_t code : kBucketGroups[groupIdx]) {
                        if (code == 0) break;
                        EnrollmentFrame bf{};
                        bf.stage = EnrollmentStage::HOLD_STILL_IN_CENTER;
                        bf.data.acquiredInfo = AcquiredInfo::VENDOR;
                        bf.data.vendorCode = code;
                        bf.data.pan = det.panDeg;
                        bf.data.tilt = det.tiltDeg;
                        bf.data.distance = det.distanceCm;
                        bf.data.isCancellable = true;
                        cb_->onEnrollmentFrame(bf);
                    }
                    ef.stage = (remaining == 0) ? EnrollmentStage::ENROLLMENT_FINISHED
                                                : EnrollmentStage::HOLD_STILL_IN_CENTER;
                    cb_->onEnrollmentFrame(ef);
                    if (remaining > 0) {
                        cb_->onEnrollmentProgress(prospectiveId, remaining);
                    }
                }

                finished.store(true);
                watchdog.join();
                // Explicitly close OV7251 & 850nm STROBE before committing/final callback.
                cap.reset();
            } catch (const std::exception& ex) {
                logWarn(ex.what());
                clearCancelFlag(flag);
                cb_->onError(Error::HW_UNAVAILABLE, 0);
                return;
            }

            const long long elapsedMs = static_cast<long long>(bootTimeMs() - startMs);
            clearCancelFlag(flag);
            if (canceledByLidOrUser) {
                __android_log_print(ANDROID_LOG_INFO,
                                    kLogTag,
                                    "enroll canceled samples=%zu frames=%u elapsedMs=%lld",
                                    samples.size(),
                                    frameIdx,
                                    elapsedMs);
                cb_->onError(Error::CANCELED, 0);
                return;
            }
            if (timedOut || samples.size() < spx::face::kSamplesPerEnrollment) {
                __android_log_print(ANDROID_LOG_INFO,
                                    kLogTag,
                                    "enroll timeout samples=%zu frames=%u elapsedMs=%lld",
                                    samples.size(),
                                    frameIdx,
                                    elapsedMs);
                cb_->onError(Error::TIMEOUT, 0);
                return;
            }
            const int32_t storedId = store_.addEnrollment(samples);
            if (storedId <= 0) {
                logWarn("enroll failed saving template store");
                cb_->onError(Error::UNABLE_TO_PROCESS, 0);
                return;
            }
            __android_log_print(ANDROID_LOG_INFO,
                                kLogTag,
                                "enroll done id=%d samples=%zu frames=%u elapsedMs=%lld",
                                storedId,
                                samples.size(),
                                frameIdx,
                                elapsedMs);
            cb_->onEnrollmentProgress(storedId, 0);
        });
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus startAuthenticate(
        int64_t operationId,
        std::optional<common::OperationContext> ctx,
        std::shared_ptr<common::ICancellationSignal>* _aidl_return) {
        auto flag = registerCancelFlag();
        *_aidl_return = SharedRefBase::make<SpxCancellationSignal>(flag);

        enqueue([this, operationId, ctx, flag]() {
            if (!model_.loaded) model_.loadFromFile(kOptionalModelPath);
            const int displayState = ctx.has_value() ? static_cast<int>(ctx->displayState) : -1;
            const int foldState = ctx.has_value() ? static_cast<int>(ctx->foldState) : -1;
            __android_log_print(ANDROID_LOG_INFO,
                                kLogTag,
                                "auth start enrolled=%zu display=%d fold=%d",
                                store_.enrollmentCount(),
                                displayState,
                                foldState);
            if (flag->load() || closed_.load() || shouldGateOperation(ctx)) {
                logWarn("auth gated or canceled before camera open");
                clearCancelFlag(flag);
                cb_->onError(Error::CANCELED, 0);
                return;
            }

            const auto lockSt = lockout_.checkState();
            if (lockSt.mode == spx::face::LockoutMode::PERMANENT) {
                logWarn("auth blocked by permanent lockout");
                clearCancelFlag(flag);
                cb_->onLockoutPermanent();
                cb_->onError(Error::HW_UNAVAILABLE, 0);
                return;
            }
            if (lockSt.mode == spx::face::LockoutMode::TIMED) {
                __android_log_print(ANDROID_LOG_WARN,
                                    kLogTag,
                                    "auth blocked by timed lockout remainingMs=%lld",
                                    static_cast<long long>(lockSt.remainingMs));
                clearCancelFlag(flag);
                cb_->onLockoutTimed(lockSt.remainingMs);
                cb_->onError(Error::HW_UNAVAILABLE, 0);
                return;
            }

            if (store_.enrollmentCount() == 0) {
                logWarn("auth aborted: zero enrolled templates");
                clearCancelFlag(flag);
                cb_->onError(Error::UNABLE_TO_PROCESS, 0);
                return;
            }

            bool matched = false;
            int32_t matchedId = 0;
            bool canceledByLidOrUser = false;
            bool sawFaceOrSpoof = false;
            int64_t startMs = bootTimeMs();
            unsigned frameIdx = 0;
            unsigned detectedFrames = 0;
            unsigned livePassFrames = 0;
            float bestSim = 0.0f;
            float maxDetScore = 0.0f;
            int lastQ = -1;
            int lastLive = -1;

            try {
                auto cap = spx::Capture::openIr(640, 480, 30);
                startMs = bootTimeMs();
                const auto rot = currentSensorRotation();
                spx::face::FaceSessionAccumulator acc;
                acc.reset(2);

                std::atomic<bool> finished{false};
                std::thread watchdog([&] {
                    while (!finished.load()) {
                        if (bootTimeMs() - startMs >= kAuthWatchdogHardMs ||
                            (lidMonitor_ && lidMonitor_->isLidClosed())) {
                            flag->store(true);
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(25));
                    }
                });

                unsigned consecutiveMatchedFrames = 0;
                unsigned missStreak = 0;
                int32_t lastMatchedId = 0;
                int64_t lastDiagMs = startMs;

                while (true) {
                    if (flag->load() || closed_.load() ||
                        (lidMonitor_ && lidMonitor_->isLidClosed())) {
                        canceledByLidOrUser = true;
                        break;
                    }
                    const int64_t nowMs = bootTimeMs();
                    const int64_t elapsed = nowMs - startMs;
                    if (elapsed >= kAuthSessionDeadlineMs ||
                        (!sawFaceOrSpoof && elapsed >= kAuthNoFaceTimeoutMs)) {
                        break;
                    }

                    auto frame = cap->next(250);
                    const auto stats = spx::face::preprocessIrFrame(
                        frame.raw10.data(),
                        frame.raw10.size(),
                        frame.width,
                        frame.height,
                        frame.stride,
                        frame.pixelFormat ? frame.pixelFormat : spx::face::kPixFmtY10,
                        rot);
                    cap->autoExposure(computeIrAeMetric(stats));

                    if (frameIdx++ < 2u) {
                        AuthenticationFrame af{};
                        af.data.acquiredInfo =
                            (frameIdx == 1u) ? AcquiredInfo::START
                                             : AcquiredInfo::FIRST_FRAME_RECEIVED;
                        cb_->onAuthenticationFrame(af);
                        continue;
                    }

                    const auto det = spx::face::detectFaceNir(stats);
                    lastQ = static_cast<int>(det.quality);

                    auto maybeLogAuthDiag = [&](int liveStatus, float c2r, float hf) {
                        if ((nowMs - lastDiagMs) >= 1000) {
                            lastDiagMs = nowMs;
                            const float interOcular =
                                det.detected ? std::hypot(det.rightEye.x - det.leftEye.x,
                                                          det.rightEye.y - det.leftEye.y)
                                             : 0.0f;
                            __android_log_print(
                                ANDROID_LOG_INFO,
                                kLogTag,
                                "auth diag frame=%u det=%d q=%d boxW=%.0f io=%.1f score=%.1f "
                                "live=%d c2r=%.2f hf=%.2f bestSim=%.3f p50=%u p99=%u roi=%.3f",
                                frameIdx,
                                det.detected ? 1 : 0,
                                static_cast<int>(det.quality),
                                det.box.w,
                                interOcular,
                                det.score,
                                liveStatus,
                                c2r,
                                hf,
                                bestSim,
                                stats.p50,
                                stats.p99,
                                stats.roiLinearMean);
                        }
                    };

                    if (!det.detected) {
                        maybeLogAuthDiag(-1, 0.0f, 0.0f);
                        ++missStreak;
                        if (missStreak >= 2u) {
                            consecutiveMatchedFrames = 0;
                        }
                        if (missStreak >= 4u) {
                            acc.reset(2);
                        }
                        AuthenticationFrame af{};
                        af.data.acquiredInfo = mapQualityToAcquired(det.quality);
                        af.data.isCancellable = true;
                        cb_->onAuthenticationFrame(af);
                        continue;
                    }

                    missStreak = 0;
                    maxDetScore = std::max(maxDetScore, det.score);
                    ++detectedFrames;
                    sawFaceOrSpoof = true;
                    const auto emb = spx::face::extractEmbedding(stats, det, &model_);
                    const auto live =
                        spx::face::evaluateLiveness(stats, det, emb, &acc, true);
                    lastLive = static_cast<int>(live.status);
                    if (!live.live) {
                        maybeLogAuthDiag(lastLive, live.centerToRimRatio, live.halftoneRatio);
                        consecutiveMatchedFrames = 0;
                        if (live.status != spx::face::LivenessStatus::NEED_MORE_FRAMES) {
                            AuthenticationFrame af{};
                            af.data.acquiredInfo =
                                (live.status == spx::face::LivenessStatus::REJECT_MOTION_BLUR)
                                    ? AcquiredInfo::TOO_MUCH_MOTION
                                    : AcquiredInfo::INSUFFICIENT;
                            af.data.pan = det.panDeg;
                            af.data.tilt = det.tiltDeg;
                            af.data.distance = det.distanceCm;
                            af.data.isCancellable = true;
                            cb_->onAuthenticationFrame(af);
                        }
                        continue;
                    }

                    ++livePassFrames;
                    const auto res = store_.match(emb, spx::face::kDefaultMatchThreshold);
                    bestSim = std::max(bestSim, res.bestSimilarity);
                    maybeLogAuthDiag(lastLive, live.centerToRimRatio, live.halftoneRatio);
                    if (res.matched) {
                        if (lastMatchedId == res.enrollmentId) {
                            ++consecutiveMatchedFrames;
                        } else {
                            lastMatchedId = res.enrollmentId;
                            consecutiveMatchedFrames = 1;
                        }
                        if (res.bestSimilarity >= spx::face::kDefaultMatchThreshold + 0.06f ||
                            consecutiveMatchedFrames >= 2u) {
                            matched = true;
                            matchedId = res.enrollmentId;
                            break;
                        }
                    } else {
                        consecutiveMatchedFrames = 0;
                    }
                }

                finished.store(true);
                watchdog.join();
                // Always stop OV7251 & 850nm STROBE before dispatching unlock/failure.
                cap.reset();
            } catch (const std::exception& ex) {
                logWarn(ex.what());
                clearCancelFlag(flag);
                cb_->onError(Error::HW_UNAVAILABLE, 0);
                return;
            }

            const long long elapsedMs = static_cast<long long>(bootTimeMs() - startMs);
            clearCancelFlag(flag);
            if (canceledByLidOrUser) {
                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "auth canceled bestSim=%.3f frames=%u detected=%u livePass=%u "
                    "maxScore=%.1f lastQ=%d lastLive=%d elapsedMs=%lld",
                    bestSim,
                    frameIdx,
                    detectedFrames,
                    livePassFrames,
                    maxDetScore,
                    lastQ,
                    lastLive,
                    elapsedMs);
                cb_->onError(Error::CANCELED, 0);
                return;
            }
            if (matched) {
                __android_log_print(ANDROID_LOG_INFO,
                                    kLogTag,
                                    "auth match id=%d sim=%.3f frames=%u elapsedMs=%lld",
                                    matchedId,
                                    bestSim,
                                    frameIdx,
                                    elapsedMs);
                lockout_.reset();
                store_.setFailedAttempts(0);

                AuthenticationFrame okFrame{};
                okFrame.data.acquiredInfo = AcquiredInfo::GOOD;
                cb_->onAuthenticationFrame(okFrame);

                const int64_t tsMs = bootTimeMs();
                keymaster::HardwareAuthToken hat{};
                hat.challenge = operationId;
                hat.userId = userId_;
                hat.authenticatorId = store_.authenticatorId();
                hat.authenticatorType = keymaster::HardwareAuthenticatorType::FINGERPRINT;
                hat.timestamp.milliSeconds = tsMs;
                const auto mac = spx::face::computeAuthTokenMac(
                    hatKey_,
                    hat.challenge,
                    hat.userId,
                    hat.authenticatorId,
                    static_cast<uint32_t>(hat.authenticatorType),
                    tsMs);
                hat.mac.assign(mac.begin(), mac.end());

                cb_->onAuthenticationSucceeded(matchedId, hat);
                return;
            }

            if (sawFaceOrSpoof) {
                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "auth failed bestSim=%.3f frames=%u detected=%u livePass=%u "
                    "maxScore=%.1f lastQ=%d lastLive=%d elapsedMs=%lld",
                    bestSim,
                    frameIdx,
                    detectedFrames,
                    livePassFrames,
                    maxDetScore,
                    lastQ,
                    lastLive,
                    elapsedMs);
                const auto st = lockout_.addFailedAttempt();
                store_.setFailedAttempts(st.failedAttempts);
                cb_->onAuthenticationFailed();
                if (st.mode == spx::face::LockoutMode::PERMANENT) {
                    cb_->onLockoutPermanent();
                } else if (st.mode == spx::face::LockoutMode::TIMED) {
                    cb_->onLockoutTimed(st.remainingMs);
                }
                return;
            }

            // Timeout with no face detected: do not burn lockout attempts.
            __android_log_print(ANDROID_LOG_INFO,
                                kLogTag,
                                "auth timeout bestSim=%.3f frames=%u detected=%u lastQ=%d lastLive=%d elapsedMs=%lld",
                                bestSim,
                                frameIdx,
                                detectedFrames,
                                lastQ,
                                lastLive,
                                elapsedMs);
            cb_->onError(Error::TIMEOUT, 0);
        });
        return ::ndk::ScopedAStatus::ok();
    }

    int32_t userId_;
    std::shared_ptr<ISessionCallback> cb_;
    std::shared_ptr<spx::face::SpxLidMonitor> lidMonitor_;
    spx::face::SpxFaceStore store_;
    spx::face::SpxLockoutTracker lockout_;
    spx::face::OptionalModelWeights model_;
    std::array<uint8_t, 32> hatKey_{};
    std::map<int64_t, int64_t> challenges_;

    std::mutex opMu_;
    std::shared_ptr<std::atomic<bool>> activeCancel_;
    bool latestFoldClosed_ = false;
    bool latestDisplayInactive_ = false;

    std::mutex queueMu_;
    std::condition_variable queueCv_;
    std::deque<std::function<void()>> queue_;
    bool stopping_ = false;
    std::thread worker_;
    std::atomic<bool> closed_{false};
    AIBinder_DeathRecipient* deathRecipient_ = nullptr;
    void* deathCookie_ = nullptr;
};

class SpxFace final : public BnFace {
public:
    SpxFace() : lidMonitor_(std::make_shared<spx::face::SpxLidMonitor>()) {
        lidMonitor_->start();
    }

    ~SpxFace() override {
        std::shared_ptr<SpxSession> old;
        {
            std::lock_guard<std::mutex> lk(mu_);
            old = activeSession_.lock();
        }
        if (old) old->close();
        if (lidMonitor_) lidMonitor_->stop();
    }

    ::ndk::ScopedAStatus getSensorProps(std::vector<SensorProps>* _aidl_return) override {
        if (!_aidl_return) return ::ndk::ScopedAStatus::fromStatus(STATUS_BAD_VALUE);

        common::ComponentInfo hwInfo;
        hwInfo.componentId = "ov7251-ir";
        hwInfo.hardwareVersion = "ovti,ov7251/850nm-strobe";
        hwInfo.firmwareVersion = "linux-6.18.60-camss";
        hwInfo.serialNumber = "";
        hwInfo.softwareVersion = "";

        common::ComponentInfo swInfo;
        swInfo.componentId = "spx-nir-engine";
        swInfo.hardwareVersion = "";
        swInfo.firmwareVersion = "";
        swInfo.serialNumber = "";
        swInfo.softwareVersion = "1.0.0-class2-weak";

        common::CommonProps commonProps;
        commonProps.sensorId = kSensorId;
        commonProps.sensorStrength = common::SensorStrength::WEAK;
        commonProps.maxEnrollmentsPerUser = spx::face::kMaxEnrollmentsPerUser;
        commonProps.componentInfo = {std::move(hwInfo), std::move(swInfo)};

        SensorProps props;
        props.commonProps = std::move(commonProps);
        props.sensorType = FaceSensorType::IR;
        props.halControlsPreview = true;
        props.previewDisplayId = 0;
        props.enrollPreviewWidth = kPreviewWidth;
        props.enrollPreviewHeight = kPreviewHeight;
        props.enrollTranslationX = 0.0f;
        props.enrollTranslationY = 0.0f;
        props.enrollPreviewScale = 1.0f;
        props.supportsDetectInteraction = false;

        *_aidl_return = {std::move(props)};
        return ::ndk::ScopedAStatus::ok();
    }

    ::ndk::ScopedAStatus createSession(int32_t in_sensorId,
                                       int32_t in_userId,
                                       const std::shared_ptr<ISessionCallback>& in_cb,
                                       std::shared_ptr<ISession>* _aidl_return) override {
        if (!in_cb || !_aidl_return || in_sensorId != kSensorId || in_userId < 0) {
            return ::ndk::ScopedAStatus::fromStatus(STATUS_BAD_VALUE);
        }

        std::shared_ptr<SpxSession> old;
        std::shared_ptr<SpxSession> created;
        {
            std::lock_guard<std::mutex> lk(mu_);
            old = activeSession_.lock();
        }
        if (old) {
            old->close();
        }
        created = ndk::SharedRefBase::make<SpxSession>(in_userId, in_cb, lidMonitor_);
        created->attachClientDeathRecipient();
        {
            std::lock_guard<std::mutex> lk(mu_);
            activeSession_ = created;
        }
        *_aidl_return = std::move(created);
        return ::ndk::ScopedAStatus::ok();
    }

private:
    std::mutex mu_;
    std::shared_ptr<spx::face::SpxLidMonitor> lidMonitor_;
    std::weak_ptr<SpxSession> activeSession_;
};

int runSelfTest() {
    auto payload = spx::face::serializeAuthTokenPayload(123, 0, 456, spx::face::kAuthTypeBiometric, 789);
    if (payload.size() != 37 || payload[0] != 0) return 1;
    if (spx::face::evaluateEffectiveLidClosed(true, "folded-back")) return 1;
    if (!spx::face::evaluateEffectiveLidClosed(true, "closed")) return 1;
    std::puts("PASS spx-face-hal --self-test");
    return 0;
}

}  // namespace
}  // namespace aidl::android::hardware::biometrics::face

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--self-test") == 0) {
        return aidl::android::hardware::biometrics::face::runSelfTest();
    }

    ABinderProcess_setThreadPoolMaxThreadCount(2);
    ABinderProcess_startThreadPool();

    auto face = ndk::SharedRefBase::make<aidl::android::hardware::biometrics::face::SpxFace>();
    const std::string instance =
        std::string(aidl::android::hardware::biometrics::face::IFace::descriptor) + "/default";
    binder_status_t status =
        AServiceManager_addService(face->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        __android_log_print(ANDROID_LOG_ERROR,
                            aidl::android::hardware::biometrics::face::kLogTag,
                            "AServiceManager_addService(%s) failed: %d",
                            instance.c_str(),
                            status);
        std::fprintf(stderr, "spx-face-hal: addService(%s) failed: %d\n", instance.c_str(), status);
        return 1;
    }
    aidl::android::hardware::biometrics::face::logInfo(
        "Registered android.hardware.biometrics.face.IFace/default (Class 2 WEAK / IR)");
    ABinderProcess_joinThreadPool();
    return 1;
}
