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

spx::face::Rotation currentSensorRotation() {
    const std::string val = getSystemProperty("persist.spx.face.rotation", "90");
    return spx::face::parseRotationDegrees(val.c_str(), spx::face::Rotation::Deg90CW);
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
                        unsigned height) {
    if (!window || gray8.empty() || width == 0 || height == 0) return;
    ANativeWindow_Buffer buf{};
    if (ANativeWindow_lock(window, &buf, nullptr) != 0 || !buf.bits) return;
    if (buf.width >= static_cast<int32_t>(width) &&
        buf.height >= static_cast<int32_t>(height) &&
        (buf.format == WINDOW_FORMAT_RGBA_8888 || buf.format == WINDOW_FORMAT_RGBX_8888)) {
        spx::face::gray8ToRgba(gray8.data(),
                               width,
                               height,
                               static_cast<uint8_t*>(buf.bits),
                               static_cast<unsigned>(buf.stride));
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
        const bool foldClosed = (in_context.foldState == common::FoldState::FULLY_CLOSED);
        const bool displayInactive =
            (in_context.displayState == common::DisplayState::NO_UI ||
             in_context.displayState == common::DisplayState::SCREENSAVER ||
             in_context.displayState == common::DisplayState::AOD);
        {
            std::lock_guard<std::mutex> lk(opMu_);
            latestFoldClosed_ = foldClosed;
            latestDisplayInactive_ = displayInactive;
            if ((foldClosed || displayInactive) && activeCancel_) {
                activeCancel_->store(true);
            }
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
            if (store_.hasFeatureBit(1u)) feats.push_back(Feature::REQUIRE_ATTENTION);
            if (store_.hasFeatureBit(2u)) feats.push_back(Feature::REQUIRE_DIVERSE_POSES);
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

    bool shouldGateOperation(const std::optional<common::OperationContext>& ctx) {
        if (!isFaceUnlockEnabled()) return true;
        if (lidMonitor_ && lidMonitor_->isLidClosed()) return true;
        std::lock_guard<std::mutex> lk(opMu_);
        if (latestFoldClosed_ || latestDisplayInactive_) return true;
        if (ctx.has_value()) {
            if (ctx->foldState == common::FoldState::FULLY_CLOSED) return true;
            if (ctx->displayState == common::DisplayState::NO_UI ||
                ctx->displayState == common::DisplayState::SCREENSAVER ||
                ctx->displayState == common::DisplayState::AOD) {
                return true;
            }
        }
        return false;
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
        const std::vector<Feature>& /*features*/,
        std::shared_ptr<ANativeWindow> previewWindow,
        std::optional<common::OperationContext> ctx,
        std::shared_ptr<common::ICancellationSignal>* _aidl_return) {
        auto flag = registerCancelFlag();
        *_aidl_return = SharedRefBase::make<SpxCancellationSignal>(flag);

        enqueue([this, hat, previewWindow = std::move(previewWindow), ctx, flag]() mutable {
            if (!model_.loaded) model_.loadFromFile(kOptionalModelPath);
            if (flag->load() || closed_.load() || shouldGateOperation(ctx)) {
                clearCancelFlag(flag);
                cb_->onError(Error::CANCELED, 0);
                return;
            }
            pruneExpiredChallenges();
            if (!validateHat(hat)) {
                clearCancelFlag(flag);
                cb_->onError(Error::UNABLE_TO_PROCESS, 0);
                return;
            }
            if (store_.enrollmentCount() >= spx::face::kMaxEnrollmentsPerUser) {
                clearCancelFlag(flag);
                cb_->onError(Error::NO_SPACE, 0);
                return;
            }

            if (previewWindow) {
                ANativeWindow_setBuffersGeometry(previewWindow.get(),
                                                 kPreviewWidth,
                                                 kPreviewHeight,
                                                 WINDOW_FORMAT_RGBA_8888);
            }

            std::vector<spx::face::Embedding> samples;
            samples.reserve(spx::face::kSamplesPerEnrollment);
            bool canceledByLidOrUser = false;
            bool timedOut = false;

            try {
                auto cap = spx::Capture::openIr(640, 480, 30);
                const int64_t startMs = bootTimeMs();
                const auto rot = currentSensorRotation();
                spx::face::FaceSessionAccumulator acc;
                acc.reset(1);

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
                unsigned frameIdx = 0;

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
                    cap->autoExposure(stats.roiLinearMean);

                    if (previewWindow) {
                        renderPreviewFrame(
                            previewWindow.get(), stats.gray8, stats.width, stats.height);
                    }

                    if (frameIdx++ < 2u) {
                        EnrollmentFrame ef{};
                        ef.stage = EnrollmentStage::FIRST_FRAME_RECEIVED;
                        ef.data.acquiredInfo = AcquiredInfo::FIRST_FRAME_RECEIVED;
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

                    if (!det.detected || det.quality != spx::face::FaceQuality::GOOD) {
                        ef.stage = EnrollmentStage::WAITING_FOR_CENTERING;
                        cb_->onEnrollmentFrame(ef);
                        continue;
                    }

                    const auto emb = spx::face::extractEmbedding(stats, det, &model_);
                    const auto live =
                        spx::face::evaluateLiveness(stats, det, emb, &acc, true);
                    if (!live.live) {
                        ef.data.acquiredInfo =
                            (live.status == spx::face::LivenessStatus::REJECT_MOTION_BLUR)
                                ? AcquiredInfo::TOO_MUCH_MOTION
                                : AcquiredInfo::INSUFFICIENT;
                        ef.stage = EnrollmentStage::HOLD_STILL_IN_CENTER;
                        cb_->onEnrollmentFrame(ef);
                        continue;
                    }

                    // Encourage mild pose diversity on later samples when requested.
                    if (samples.size() >= 2 && store_.hasFeatureBit(2u)) {
                        const float maxSim = [&]() {
                            float hi = 0.0f;
                            for (const auto& prev : samples) {
                                hi = std::max(hi, spx::face::cosineSimilarity(prev, emb));
                            }
                            return hi;
                        }();
                        if (maxSim > 0.992f && (bootTimeMs() - startMs) < 15000) {
                            ef.stage = (samples.size() < 4)
                                           ? EnrollmentStage::ENROLLING_MOVEMENT_1
                                           : EnrollmentStage::ENROLLING_MOVEMENT_2;
                            ef.data.acquiredInfo = AcquiredInfo::TOO_SIMILAR;
                            cb_->onEnrollmentFrame(ef);
                            continue;
                        }
                    }

                    samples.push_back(emb);
                    const int32_t remaining =
                        static_cast<int32_t>(spx::face::kSamplesPerEnrollment - samples.size());
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

            clearCancelFlag(flag);
            if (canceledByLidOrUser) {
                cb_->onError(Error::CANCELED, 0);
                return;
            }
            if (timedOut || samples.size() < spx::face::kSamplesPerEnrollment) {
                cb_->onError(Error::TIMEOUT, 0);
                return;
            }
            const int32_t storedId = store_.addEnrollment(samples);
            if (storedId <= 0) {
                cb_->onError(Error::UNABLE_TO_PROCESS, 0);
                return;
            }
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
            if (flag->load() || closed_.load() || shouldGateOperation(ctx)) {
                clearCancelFlag(flag);
                cb_->onError(Error::CANCELED, 0);
                return;
            }

            const auto lockSt = lockout_.checkState();
            if (lockSt.mode == spx::face::LockoutMode::PERMANENT) {
                clearCancelFlag(flag);
                cb_->onLockoutPermanent();
                cb_->onError(Error::HW_UNAVAILABLE, 0);
                return;
            }
            if (lockSt.mode == spx::face::LockoutMode::TIMED) {
                clearCancelFlag(flag);
                cb_->onLockoutTimed(lockSt.remainingMs);
                cb_->onError(Error::HW_UNAVAILABLE, 0);
                return;
            }

            if (store_.enrollmentCount() == 0) {
                clearCancelFlag(flag);
                cb_->onError(Error::UNABLE_TO_PROCESS, 0);
                return;
            }

            bool matched = false;
            int32_t matchedId = 0;
            bool canceledByLidOrUser = false;
            bool sawFaceOrSpoof = false;

            try {
                auto cap = spx::Capture::openIr(640, 480, 30);
                const int64_t startMs = bootTimeMs();
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

                unsigned frameIdx = 0;
                unsigned consecutiveMatchedFrames = 0;
                int32_t lastMatchedId = 0;

                while (true) {
                    if (flag->load() || closed_.load() ||
                        (lidMonitor_ && lidMonitor_->isLidClosed())) {
                        canceledByLidOrUser = true;
                        break;
                    }
                    const int64_t elapsed = bootTimeMs() - startMs;
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
                    cap->autoExposure(stats.roiLinearMean);

                    if (frameIdx++ < 2u) {
                        AuthenticationFrame af{};
                        af.data.acquiredInfo =
                            (frameIdx == 1u) ? AcquiredInfo::START
                                             : AcquiredInfo::FIRST_FRAME_RECEIVED;
                        cb_->onAuthenticationFrame(af);
                        continue;
                    }

                    const auto det = spx::face::detectFaceNir(stats);
                    if (!det.detected) {
                        consecutiveMatchedFrames = 0;
                        AuthenticationFrame af{};
                        af.data.acquiredInfo = mapQualityToAcquired(det.quality);
                        af.data.isCancellable = true;
                        cb_->onAuthenticationFrame(af);
                        continue;
                    }

                    sawFaceOrSpoof = true;
                    const auto emb = spx::face::extractEmbedding(stats, det, &model_);
                    const auto live =
                        spx::face::evaluateLiveness(stats, det, emb, &acc, true);
                    if (!live.live) {
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

                    const auto res = store_.match(emb, spx::face::kDefaultMatchThreshold);
                    if (res.matched) {
                        if (lastMatchedId == res.enrollmentId) {
                            ++consecutiveMatchedFrames;
                        } else {
                            lastMatchedId = res.enrollmentId;
                            consecutiveMatchedFrames = 1;
                        }
                        if (consecutiveMatchedFrames >= 2u) {
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

            clearCancelFlag(flag);
            if (canceledByLidOrUser) {
                cb_->onError(Error::CANCELED, 0);
                return;
            }
            if (matched) {
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
