// SPDX-License-Identifier: GPL-2.0-only
/* Android AIDL android.hardware.gnss.IGnss/default (V4) HAL for Surface Pro X
 * (SC8180X MPSS over AF_QIPCRTR / QMI LOC v2).
 *
 * Power & s2idle policy:
 * - Opens AF_QIPCRTR and issues QMI_LOC_START_REQ strictly while IGnss is active.
 * - Immediately issues QMI_LOC_STOP_REQ and closes the QRTR socket on stop()/close().
 * - All QRTR I/O runs exclusively on a single worker thread woken via eventfd.
 */
#include "spx-nmea.h"
#include "spx-qmi-loc.h"

#include <aidl/android/hardware/gnss/BnAGnss.h>
#include <aidl/android/hardware/gnss/BnAGnssRil.h>
#include <aidl/android/hardware/gnss/BnGnss.h>
#include <aidl/android/hardware/gnss/BnGnssAntennaInfo.h>
#include <aidl/android/hardware/gnss/BnGnssConfiguration.h>
#include <aidl/android/hardware/gnss/BnGnssDebug.h>
#include <aidl/android/hardware/gnss/BnGnssMeasurementInterface.h>
#include <aidl/android/hardware/gnss/BnGnssPowerIndication.h>
#include <aidl/android/hardware/gnss/BnGnssPsds.h>
#include <aidl/android/hardware/gnss/visibility_control/BnGnssVisibilityControl.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <poll.h>
#include <set>
#include <string>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace aidl::android::hardware::gnss {
namespace {

using ::ndk::ScopedAStatus;
namespace vc = ::aidl::android::hardware::gnss::visibility_control;

constexpr const char *kLogTag = "SpxGnssHal";
constexpr int kQmiTimeoutMs = 3000;
constexpr int kReconnectDelayMs = 2000;
constexpr float kMaxAcceptableInProgressHorUncM = 50000.0f;
constexpr float kDefaultHorUncM = 25.0f;

static void logInfo(const char *msg) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", msg);
}

static void logWarn(const char *msg) {
    __android_log_print(ANDROID_LOG_WARN, kLogTag, "%s", msg);
}

static int64_t wallTimeMs() {
    struct timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000LL;
}

static int64_t bootTimeNs() {
    struct timespec ts {};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

static GnssConstellationType mapQmiSystem(uint32_t sys) {
    switch (sys) {
    case SPX_QMI_LOC_SYSTEM_GPS:
        return GnssConstellationType::GPS;
    case SPX_QMI_LOC_SYSTEM_SBAS:
        return GnssConstellationType::SBAS;
    case SPX_QMI_LOC_SYSTEM_GLONASS:
        return GnssConstellationType::GLONASS;
    case SPX_QMI_LOC_SYSTEM_COMPASS:
        return GnssConstellationType::BEIDOU;
    case SPX_QMI_LOC_SYSTEM_GALILEO:
        return GnssConstellationType::GALILEO;
    default:
        return GnssConstellationType::UNKNOWN;
    }
}

static GnssConstellationType mapNmeaTalker(enum spx_nmea_talker talker) {
    switch (talker) {
    case SPX_NMEA_TALKER_GP:
        return GnssConstellationType::GPS;
    case SPX_NMEA_TALKER_GL:
        return GnssConstellationType::GLONASS;
    case SPX_NMEA_TALKER_GA:
        return GnssConstellationType::GALILEO;
    case SPX_NMEA_TALKER_GB:
        return GnssConstellationType::BEIDOU;
    case SPX_NMEA_TALKER_GQ:
        return GnssConstellationType::QZSS;
    default:
        return GnssConstellationType::UNKNOWN;
    }
}

static float carrierFrequencyForConstellation(GnssConstellationType c) {
    switch (c) {
    case GnssConstellationType::GPS:
    case GnssConstellationType::GALILEO:
    case GnssConstellationType::QZSS:
    case GnssConstellationType::SBAS:
        return 1575420000.0f;
    case GnssConstellationType::GLONASS:
        return 1602000000.0f;
    case GnssConstellationType::BEIDOU:
        return 1561098000.0f;
    default:
        return 0.0f;
    }
}

static bool extractHorUncMeters(const spx_qmi_loc_position &pos, float *outUnc) {
    if (pos.has_hor_unc_circular && std::isfinite(pos.hor_unc_circular) &&
        pos.hor_unc_circular >= 0.0f) {
        *outUnc = pos.hor_unc_circular;
        return true;
    }
    if (pos.has_hor_unc_ellipse_major && std::isfinite(pos.hor_unc_ellipse_major) &&
        pos.hor_unc_ellipse_major >= 0.0f) {
        *outUnc = pos.hor_unc_ellipse_major;
        return true;
    }
    return false;
}

static bool positionShouldReport(const spx_qmi_loc_position &pos, GnssLocation *out) {
    if (!pos.has_lat || !pos.has_lon)
        return false;
    if (!std::isfinite(pos.latitude) || !std::isfinite(pos.longitude))
        return false;
    if (pos.latitude == 0.0 && pos.longitude == 0.0)
        return false;

    float horUnc = 0.0f;
    bool hasHorUnc = extractHorUncMeters(pos, &horUnc);

    bool isSuccess = (pos.session_status == SPX_QMI_LOC_SESSION_SUCCESS);
    bool isInProgressSat =
        (pos.session_status == SPX_QMI_LOC_SESSION_IN_PROGRESS) &&
        (pos.sats_used_count > 0 ||
         (pos.has_technology && (pos.technology & SPX_QMI_LOC_TECH_SATELLITE) != 0));

    if (!isSuccess && !isInProgressSat)
        return false;
    if (!isSuccess && hasHorUnc && horUnc > kMaxAcceptableInProgressHorUncM)
        return false;

    GnssLocation loc{};
    loc.gnssLocationFlags =
        static_cast<int32_t>(GnssLocation::HAS_LAT_LONG) |
        static_cast<int32_t>(GnssLocation::HAS_HORIZONTAL_ACCURACY);
    loc.latitudeDegrees = pos.latitude;
    loc.longitudeDegrees = pos.longitude;
    loc.horizontalAccuracyMeters = (hasHorUnc && horUnc > 0.0f) ? horUnc : kDefaultHorUncM;

    if (pos.has_alt_sealevel || pos.has_alt_ellipsoid) {
        loc.altitudeMeters = pos.has_alt_sealevel ? pos.alt_sealevel : pos.alt_ellipsoid;
        loc.gnssLocationFlags |= static_cast<int32_t>(GnssLocation::HAS_ALTITUDE);
    }
    if (pos.has_speed_horizontal) {
        loc.speedMetersPerSec = pos.speed_horizontal;
        loc.gnssLocationFlags |= static_cast<int32_t>(GnssLocation::HAS_SPEED);
    }
    if (pos.has_heading) {
        loc.bearingDegrees = pos.heading;
        loc.gnssLocationFlags |= static_cast<int32_t>(GnssLocation::HAS_BEARING);
    }
    if (pos.has_vert_unc && pos.vert_unc > 0.0f) {
        loc.verticalAccuracyMeters = pos.vert_unc;
        loc.gnssLocationFlags |= static_cast<int32_t>(GnssLocation::HAS_VERTICAL_ACCURACY);
    }
    if (pos.has_speed_unc && pos.speed_unc > 0.0f) {
        loc.speedAccuracyMetersPerSecond = pos.speed_unc;
        loc.gnssLocationFlags |= static_cast<int32_t>(GnssLocation::HAS_SPEED_ACCURACY);
    }
    if (pos.has_heading_unc && pos.heading_unc > 0.0f) {
        loc.bearingAccuracyDegrees = pos.heading_unc;
        loc.gnssLocationFlags |= static_cast<int32_t>(GnssLocation::HAS_BEARING_ACCURACY);
    }

    loc.timestampMillis = (pos.has_utc_timestamp_ms && pos.utc_timestamp_ms > 0)
                              ? static_cast<int64_t>(pos.utc_timestamp_ms)
                              : wallTimeMs();
    loc.elapsedRealtime.flags =
        static_cast<int32_t>(ElapsedRealtime::HAS_TIMESTAMP_NS) |
        static_cast<int32_t>(ElapsedRealtime::HAS_TIME_UNCERTAINTY_NS);
    loc.elapsedRealtime.timestampNs = bootTimeNs();
    loc.elapsedRealtime.timeUncertaintyNs = 10000000.0;
    *out = loc;
    return true;
}

class SpxGnssConfiguration final : public BnGnssConfiguration {
  public:
    ScopedAStatus setSuplVersion(int32_t) override { return ScopedAStatus::ok(); }
    ScopedAStatus setSuplMode(int32_t) override { return ScopedAStatus::ok(); }
    ScopedAStatus setLppProfile(int32_t) override { return ScopedAStatus::ok(); }
    ScopedAStatus setGlonassPositioningProtocol(int32_t) override { return ScopedAStatus::ok(); }
    ScopedAStatus setEmergencySuplPdn(bool) override { return ScopedAStatus::ok(); }
    ScopedAStatus setEsExtensionSec(int32_t) override { return ScopedAStatus::ok(); }

    ScopedAStatus setBlocklist(const std::vector<BlocklistedSource> &blocklist) override {
        std::lock_guard<std::mutex> lock(mMutex);
        mBlocklist = blocklist;
        return ScopedAStatus::ok();
    }

    bool isBlocklisted(GnssConstellationType constellation, int32_t svid) const {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto &entry : mBlocklist) {
            if (entry.constellation == constellation &&
                (entry.svid == 0 || entry.svid == svid)) {
                return true;
            }
        }
        return false;
    }

  private:
    mutable std::mutex mMutex;
    std::vector<BlocklistedSource> mBlocklist;
};

class SpxGnssMeasurement final : public BnGnssMeasurementInterface {
  public:
    ScopedAStatus setCallback(const std::shared_ptr<IGnssMeasurementCallback> &cb,
                              bool, bool) override {
        std::lock_guard<std::mutex> lock(mMutex);
        mCb = cb;
        return ScopedAStatus::ok();
    }
    ScopedAStatus close() override {
        std::lock_guard<std::mutex> lock(mMutex);
        mCb.reset();
        return ScopedAStatus::ok();
    }
    ScopedAStatus setCallbackWithOptions(const std::shared_ptr<IGnssMeasurementCallback> &cb,
                                         const Options &) override {
        std::lock_guard<std::mutex> lock(mMutex);
        mCb = cb;
        return ScopedAStatus::ok();
    }

  private:
    std::mutex mMutex;
    std::shared_ptr<IGnssMeasurementCallback> mCb;
};

class SpxGnssPowerIndication final : public BnGnssPowerIndication {
  public:
    ScopedAStatus setCallback(const std::shared_ptr<IGnssPowerIndicationCallback> &cb) override {
        std::lock_guard<std::mutex> lock(mMutex);
        mCb = cb;
        if (mCb) {
            mCb->setCapabilitiesCb(0);
        }
        return ScopedAStatus::ok();
    }
    ScopedAStatus requestGnssPowerStats() override {
        std::shared_ptr<IGnssPowerIndicationCallback> cb;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            cb = mCb;
        }
        if (cb) {
            GnssPowerStats stats{};
            stats.elapsedRealtime.flags =
                static_cast<int32_t>(ElapsedRealtime::HAS_TIMESTAMP_NS) |
                static_cast<int32_t>(ElapsedRealtime::HAS_TIME_UNCERTAINTY_NS);
            stats.elapsedRealtime.timestampNs = bootTimeNs();
            stats.elapsedRealtime.timeUncertaintyNs = 10000000.0;
            cb->gnssPowerStatsCb(stats);
        }
        return ScopedAStatus::ok();
    }

  private:
    std::mutex mMutex;
    std::shared_ptr<IGnssPowerIndicationCallback> mCb;
};

class SpxAGnss final : public BnAGnss {
  public:
    ScopedAStatus setCallback(const std::shared_ptr<IAGnssCallback> &cb) override {
        std::lock_guard<std::mutex> lock(mMutex);
        mCb = cb;
        return ScopedAStatus::ok();
    }
    ScopedAStatus dataConnClosed() override { return ScopedAStatus::ok(); }
    ScopedAStatus dataConnFailed() override { return ScopedAStatus::ok(); }
    ScopedAStatus setServer(IAGnssCallback::AGnssType, const std::string &, int32_t) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus dataConnOpen(int64_t, const std::string &, IAGnss::ApnIpType) override {
        return ScopedAStatus::ok();
    }

  private:
    std::mutex mMutex;
    std::shared_ptr<IAGnssCallback> mCb;
};

class SpxAGnssRil final : public BnAGnssRil {
  public:
    ScopedAStatus setCallback(const std::shared_ptr<IAGnssRilCallback> &cb) override {
        std::lock_guard<std::mutex> lock(mMutex);
        mCb = cb;
        return ScopedAStatus::ok();
    }
    ScopedAStatus setRefLocation(const IAGnssRil::AGnssRefLocation &) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus setSetId(IAGnssRil::SetIdType, const std::string &) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus updateNetworkState(const IAGnssRil::NetworkAttributes &) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus injectNiSuplMessageData(const std::vector<uint8_t> &, int32_t) override {
        return ScopedAStatus::ok();
    }

  private:
    std::mutex mMutex;
    std::shared_ptr<IAGnssRilCallback> mCb;
};

class SpxGnssAntennaInfo final : public BnGnssAntennaInfo {
  public:
    ScopedAStatus setCallback(const std::shared_ptr<IGnssAntennaInfoCallback> &cb) override {
        std::lock_guard<std::mutex> lock(mMutex);
        mCb = cb;
        return ScopedAStatus::ok();
    }
    ScopedAStatus close() override {
        std::lock_guard<std::mutex> lock(mMutex);
        mCb.reset();
        return ScopedAStatus::ok();
    }

  private:
    std::mutex mMutex;
    std::shared_ptr<IGnssAntennaInfoCallback> mCb;
};

class SpxGnssVisibilityControl final : public vc::BnGnssVisibilityControl {
  public:
    ScopedAStatus enableNfwLocationAccess(const std::vector<std::string> &) override {
        return ScopedAStatus::ok();
    }
    ScopedAStatus setCallback(const std::shared_ptr<vc::IGnssVisibilityControlCallback> &cb) override {
        std::lock_guard<std::mutex> lock(mMutex);
        mCb = cb;
        return ScopedAStatus::ok();
    }

  private:
    std::mutex mMutex;
    std::shared_ptr<vc::IGnssVisibilityControlCallback> mCb;
};

class SpxGnssPsds final : public BnGnssPsds {
  public:
    ScopedAStatus injectPsdsData(PsdsType, const std::vector<uint8_t> &psdsData) override {
        if (psdsData.empty()) {
            return ScopedAStatus::fromServiceSpecificError(IGnss::ERROR_INVALID_ARGUMENT);
        }
        return ScopedAStatus::ok();
    }
    ScopedAStatus setCallback(const std::shared_ptr<IGnssPsdsCallback> &cb) override {
        std::lock_guard<std::mutex> lock(mMutex);
        mCb = cb;
        return ScopedAStatus::ok();
    }

  private:
    std::mutex mMutex;
    std::shared_ptr<IGnssPsdsCallback> mCb;
};

struct CachedState {
    std::mutex mutex;
    bool hasInjectedTime = false;
    int64_t injectedTimeMs = 0;
    int64_t injectedTimeRefMs = 0;
    int32_t injectedTimeUncMs = 0;

    bool hasInjectedPos = false;
    double injectedLat = 0.0;
    double injectedLon = 0.0;
    float injectedHorUncM = 1000.0f;

    bool pendingDeleteAiding = false;
    bool pendingReconfigMode = false;

    bool hasLastFix = false;
    GnssLocation lastFix{};
};

class SpxGnssDebug final : public BnGnssDebug {
  public:
    explicit SpxGnssDebug(std::shared_ptr<CachedState> state) : mState(std::move(state)) {}
    ScopedAStatus getDebugData(DebugData *out) override {
        DebugData d{};
        std::lock_guard<std::mutex> lock(mState->mutex);
        d.time.timeEstimateMs = mState->hasLastFix ? mState->lastFix.timestampMillis : wallTimeMs();
        d.time.timeUncertaintyNs = 10000000.0f;
        if (mState->hasLastFix) {
            d.position.valid = true;
            d.position.latitudeDegrees = mState->lastFix.latitudeDegrees;
            d.position.longitudeDegrees = mState->lastFix.longitudeDegrees;
            d.position.altitudeMeters = mState->lastFix.altitudeMeters;
            d.position.speedMetersPerSec = mState->lastFix.speedMetersPerSec;
            d.position.bearingDegrees = mState->lastFix.bearingDegrees;
            d.position.horizontalAccuracyMeters = mState->lastFix.horizontalAccuracyMeters;
            d.position.verticalAccuracyMeters = mState->lastFix.verticalAccuracyMeters;
            d.position.ageSeconds = 0.0f;
        }
        *out = std::move(d);
        return ScopedAStatus::ok();
    }

  private:
    std::shared_ptr<CachedState> mState;
};

class SpxGnss final : public BnGnss {
  public:
    SpxGnss()
        : mState(std::make_shared<CachedState>()),
          mConfig(ndk::SharedRefBase::make<SpxGnssConfiguration>()),
          mMeasurement(ndk::SharedRefBase::make<SpxGnssMeasurement>()),
          mPower(ndk::SharedRefBase::make<SpxGnssPowerIndication>()),
          mAGnss(ndk::SharedRefBase::make<SpxAGnss>()),
          mAGnssRil(ndk::SharedRefBase::make<SpxAGnssRil>()),
          mAntenna(ndk::SharedRefBase::make<SpxGnssAntennaInfo>()),
          mVisibility(ndk::SharedRefBase::make<SpxGnssVisibilityControl>()),
          mPsds(ndk::SharedRefBase::make<SpxGnssPsds>()),
          mDebug(ndk::SharedRefBase::make<SpxGnssDebug>(mState)) {}

    ~SpxGnss() override { stop(); }

    ScopedAStatus setCallback(const std::shared_ptr<IGnssCallback> &callback) override {
        if (!callback) {
            return ScopedAStatus::fromServiceSpecificError(IGnss::ERROR_INVALID_ARGUMENT);
        }
        {
            std::lock_guard<std::mutex> lock(mCbMutex);
            mCallback = callback;
        }
        constexpr int32_t caps =
            static_cast<int32_t>(IGnssCallback::CAPABILITY_SCHEDULING) |
            static_cast<int32_t>(IGnssCallback::CAPABILITY_ON_DEMAND_TIME) |
            static_cast<int32_t>(IGnssCallback::CAPABILITY_SATELLITE_BLOCKLIST);
        callback->gnssSetCapabilitiesCb(caps);

        IGnssCallback::GnssSystemInfo sysInfo{};
        sysInfo.yearOfHw = 2020;
        sysInfo.name = "Surface Pro X SC8180X QMI LOC";
        callback->gnssSetSystemInfoCb(sysInfo);

        std::vector<GnssSignalType> signals = {
            {GnssConstellationType::GPS, 1575420000.0, GnssSignalType::CODE_TYPE_C},
            {GnssConstellationType::GLONASS, 1602000000.0, GnssSignalType::CODE_TYPE_C},
            {GnssConstellationType::GALILEO, 1575420000.0, GnssSignalType::CODE_TYPE_C},
            {GnssConstellationType::BEIDOU, 1561098000.0, GnssSignalType::CODE_TYPE_D},
        };
        callback->gnssSetSignalTypeCapabilitiesCb(signals);
        logInfo("setCallback registered");
        return ScopedAStatus::ok();
    }

    ScopedAStatus start() override {
        std::lock_guard<std::mutex> lock(mSessionMutex);
        if (mRunning.load()) {
            return ScopedAStatus::ok();
        }
        mWakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (mWakeFd < 0) {
            logWarn("eventfd creation failed");
            return ScopedAStatus::fromServiceSpecificError(IGnss::ERROR_GENERIC);
        }
        mStopRequested.store(false);
        mRunning.store(true);
        mWorker = std::thread([this]() { workerLoop(); });
        logInfo("GNSS session started");
        return ScopedAStatus::ok();
    }

    ScopedAStatus stop() override {
        std::unique_lock<std::mutex> lock(mSessionMutex);
        if (!mRunning.load()) {
            return ScopedAStatus::ok();
        }
        mStopRequested.store(true);
        wakeWorker();
        std::thread worker = std::move(mWorker);
        lock.unlock();
        if (worker.joinable()) {
            worker.join();
        }
        lock.lock();
        if (mWakeFd >= 0) {
            ::close(mWakeFd);
            mWakeFd = -1;
        }
        mRunning.store(false);
        logInfo("GNSS session stopped");
        return ScopedAStatus::ok();
    }

    ScopedAStatus close() override {
        stop();
        std::lock_guard<std::mutex> lock(mCbMutex);
        mCallback.reset();
        return ScopedAStatus::ok();
    }

    ScopedAStatus injectTime(int64_t timeMs, int64_t timeReferenceMs, int32_t uncertaintyMs) override {
        if (timeMs <= 0 || uncertaintyMs < 0)
            return ScopedAStatus::ok();
        {
            std::lock_guard<std::mutex> lock(mState->mutex);
            mState->hasInjectedTime = true;
            mState->injectedTimeMs = timeMs;
            mState->injectedTimeRefMs = timeReferenceMs;
            mState->injectedTimeUncMs = uncertaintyMs;
        }
        wakeWorker();
        return ScopedAStatus::ok();
    }

    ScopedAStatus injectLocation(const GnssLocation &location) override {
        return injectBestLocation(location);
    }

    ScopedAStatus injectBestLocation(const GnssLocation &location) override {
        if ((location.gnssLocationFlags & GnssLocation::HAS_LAT_LONG) == 0 ||
            !std::isfinite(location.latitudeDegrees) ||
            !std::isfinite(location.longitudeDegrees)) {
            return ScopedAStatus::ok();
        }
        float acc = (location.gnssLocationFlags & GnssLocation::HAS_HORIZONTAL_ACCURACY) &&
                            location.horizontalAccuracyMeters > 0.0
                        ? static_cast<float>(location.horizontalAccuracyMeters)
                        : 1000.0f;
        {
            std::lock_guard<std::mutex> lock(mState->mutex);
            mState->hasInjectedPos = true;
            mState->injectedLat = location.latitudeDegrees;
            mState->injectedLon = location.longitudeDegrees;
            mState->injectedHorUncM = acc;
        }
        wakeWorker();
        return ScopedAStatus::ok();
    }

    ScopedAStatus deleteAidingData(GnssAidingData aidingDataFlags) override {
        if (aidingDataFlags != static_cast<GnssAidingData>(0)) {
            std::lock_guard<std::mutex> lock(mState->mutex);
            mState->pendingDeleteAiding = true;
        }
        wakeWorker();
        return ScopedAStatus::ok();
    }

    ScopedAStatus setPositionMode(const PositionModeOptions &options) override {
        uint32_t interval = static_cast<uint32_t>(std::clamp(options.minIntervalMs, 1000, 60000));
        uint32_t qmiOpMode = SPX_QMI_LOC_OPMODE_DEFAULT;
        if (options.mode == GnssPositionMode::STANDALONE) {
            qmiOpMode = SPX_QMI_LOC_OPMODE_STANDALONE;
        } else if (options.mode == GnssPositionMode::MS_BASED) {
            qmiOpMode = SPX_QMI_LOC_OPMODE_MSB;
        } else if (options.mode == GnssPositionMode::MS_ASSISTED) {
            qmiOpMode = SPX_QMI_LOC_OPMODE_MSA;
        }
        uint32_t recurrence = (options.recurrence == GnssPositionRecurrence::RECURRENCE_SINGLE)
                                  ? SPX_QMI_LOC_RECURRENCE_SINGLE
                                  : SPX_QMI_LOC_RECURRENCE_PERIODIC;
        mMinIntervalMs.store(interval);
        mOpMode.store(qmiOpMode);
        mRecurrence.store(recurrence);
        {
            std::lock_guard<std::mutex> lock(mState->mutex);
            mState->pendingReconfigMode = mRunning.load();
        }
        wakeWorker();
        return ScopedAStatus::ok();
    }

    ScopedAStatus startSvStatus() override {
        mSvStatusActive.store(true);
        return ScopedAStatus::ok();
    }
    ScopedAStatus stopSvStatus() override {
        mSvStatusActive.store(false);
        return ScopedAStatus::ok();
    }
    ScopedAStatus startNmea() override {
        mNmeaActive.store(true);
        return ScopedAStatus::ok();
    }
    ScopedAStatus stopNmea() override {
        mNmeaActive.store(false);
        return ScopedAStatus::ok();
    }

    ScopedAStatus getExtensionPsds(std::shared_ptr<IGnssPsds> *out) override {
        *out = mPsds;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getExtensionGnssConfiguration(std::shared_ptr<IGnssConfiguration> *out) override {
        *out = mConfig;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getExtensionGnssMeasurement(std::shared_ptr<IGnssMeasurementInterface> *out) override {
        *out = mMeasurement;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getExtensionGnssPowerIndication(std::shared_ptr<IGnssPowerIndication> *out) override {
        *out = mPower;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getExtensionGnssBatching(std::shared_ptr<IGnssBatching> *out) override {
        *out = nullptr;
        return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    ScopedAStatus getExtensionGnssGeofence(std::shared_ptr<IGnssGeofence> *out) override {
        *out = nullptr;
        return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    ScopedAStatus getExtensionGnssNavigationMessage(
        std::shared_ptr<IGnssNavigationMessageInterface> *out) override {
        *out = nullptr;
        return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    ScopedAStatus getExtensionAGnss(std::shared_ptr<IAGnss> *out) override {
        *out = mAGnss;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getExtensionAGnssRil(std::shared_ptr<IAGnssRil> *out) override {
        *out = mAGnssRil;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getExtensionGnssDebug(std::shared_ptr<IGnssDebug> *out) override {
        *out = mDebug;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getExtensionGnssVisibilityControl(
        std::shared_ptr<vc::IGnssVisibilityControl> *out) override {
        *out = mVisibility;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getExtensionGnssAntennaInfo(std::shared_ptr<IGnssAntennaInfo> *out) override {
        *out = mAntenna;
        return ScopedAStatus::ok();
    }
    ScopedAStatus getExtensionMeasurementCorrections(
        std::shared_ptr<measurement_corrections::IMeasurementCorrectionsInterface> *out) override {
        *out = nullptr;
        return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

  private:
    std::shared_ptr<IGnssCallback> getCallback() const {
        std::lock_guard<std::mutex> lock(mCbMutex);
        return mCallback;
    }

    void wakeWorker() {
        int fd = mWakeFd;
        if (fd >= 0) {
            uint64_t one = 1;
            ssize_t ignored = ::write(fd, &one, sizeof(one));
            (void)ignored;
        }
    }

    void drainWakeFd() {
        uint64_t val = 0;
        while (::read(mWakeFd, &val, sizeof(val)) > 0) {
        }
    }

    void notifyStatus(IGnssCallback::GnssStatusValue val) {
        auto cb = getCallback();
        if (cb) {
            cb->gnssStatusCb(val);
        }
    }

    bool startQmiSession(spx_qmi_loc_client *loc, uint8_t sessionId) {
        if (spx_qmi_loc_open(loc, kQmiTimeoutMs) != 0) {
            logWarn("QRTR lookup for QMI_SERVICE_LOC failed");
            return false;
        }
        spx_qmi_result res{};
        if (spx_qmi_loc_set_engine_lock(loc, SPX_QMI_LOC_LOCK_NONE, kQmiTimeoutMs, &res) != 0) {
            __android_log_print(ANDROID_LOG_WARN, kLogTag,
                                "SET_ENGINE_LOCK(NONE) warning result=%u err=%u", res.result,
                                res.error);
        }
        (void)spx_qmi_loc_set_operation_mode(loc, mOpMode.load(), kQmiTimeoutMs, &res);

        if (spx_qmi_loc_set_nmea_types(loc, SPX_QMI_LOC_NMEA_EXTENDED, kQmiTimeoutMs, &res) != 0) {
            (void)spx_qmi_loc_set_nmea_types(loc, SPX_QMI_LOC_NMEA_DEFAULT, kQmiTimeoutMs, &res);
        }

        constexpr uint64_t kEvents = SPX_QMI_LOC_EVENT_POSITION_REPORT |
                                     SPX_QMI_LOC_EVENT_GNSS_SV_INFO |
                                     SPX_QMI_LOC_EVENT_NMEA |
                                     SPX_QMI_LOC_EVENT_INJECT_TIME_REQ |
                                     SPX_QMI_LOC_EVENT_ENGINE_STATE;
        if (spx_qmi_loc_reg_events(loc, kEvents, kQmiTimeoutMs, &res) != 0) {
            __android_log_print(ANDROID_LOG_WARN, kLogTag,
                                "QMI_LOC_REG_EVENTS failed result=%u err=%u", res.result,
                                res.error);
            spx_qmi_loc_close(loc);
            return false;
        }

        flushPendingInjections(loc, sessionId);

        spx_qmi_loc_start_params sp{};
        sp.session_id = sessionId;
        sp.has_fix_recurrence = true;
        sp.fix_recurrence = mRecurrence.load();
        sp.has_intermediate_report_state = true;
        sp.intermediate_report_state = SPX_QMI_LOC_INTERMEDIATE_ENABLE;
        sp.has_min_interval_ms = true;
        sp.min_interval_ms = mMinIntervalMs.load();
        if (spx_qmi_loc_start(loc, &sp, kQmiTimeoutMs, &res) != 0) {
            __android_log_print(ANDROID_LOG_WARN, kLogTag,
                                "QMI_LOC_START failed result=%u err=%u", res.result, res.error);
            spx_qmi_loc_close(loc);
            return false;
        }

        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "QMI LOC session %u started (node=%u port=%u interval=%ums)",
                            unsigned(sessionId), loc->service_node, loc->service_port,
                            mMinIntervalMs.load());
        notifyStatus(IGnssCallback::GnssStatusValue::ENGINE_ON);
        notifyStatus(IGnssCallback::GnssStatusValue::SESSION_BEGIN);
        return true;
    }

    void stopQmiSession(spx_qmi_loc_client *loc, uint8_t sessionId, bool notifyEnd) {
        if (loc->connected && loc->fd >= 0) {
            spx_qmi_result res{};
            (void)spx_qmi_loc_stop(loc, sessionId, kQmiTimeoutMs, &res);
        }
        spx_qmi_loc_close(loc);
        if (notifyEnd) {
            notifyStatus(IGnssCallback::GnssStatusValue::SESSION_END);
            notifyStatus(IGnssCallback::GnssStatusValue::ENGINE_OFF);
        }
    }

    bool flushPendingInjections(spx_qmi_loc_client *loc, uint8_t sessionId) {
        bool doTime = false, doPos = false, doDel = false, doReconf = false;
        int64_t timeMs = 0;
        int32_t uncMs = 0;
        double lat = 0.0, lon = 0.0;
        float horUnc = 0.0f;
        {
            std::lock_guard<std::mutex> lock(mState->mutex);
            if (mState->hasInjectedTime) {
                doTime = true;
                timeMs = mState->injectedTimeMs;
                uncMs = mState->injectedTimeUncMs;
                mState->hasInjectedTime = false;
            }
            if (mState->hasInjectedPos) {
                doPos = true;
                lat = mState->injectedLat;
                lon = mState->injectedLon;
                horUnc = mState->injectedHorUncM;
                mState->hasInjectedPos = false;
            }
            if (mState->pendingDeleteAiding) {
                doDel = true;
                mState->pendingDeleteAiding = false;
            }
            if (mState->pendingReconfigMode) {
                doReconf = true;
                mState->pendingReconfigMode = false;
            }
        }
        if (!loc->connected)
            return false;

        spx_qmi_result res{};
        if (doDel) {
            (void)spx_qmi_loc_delete_all_aiding(loc, kQmiTimeoutMs, &res);
        }
        if (doTime) {
            (void)spx_qmi_loc_inject_utc_time(loc, static_cast<uint64_t>(timeMs),
                                              static_cast<uint32_t>(std::max(1, uncMs)),
                                              1 /* AP */, kQmiTimeoutMs, &res);
        }
        if (doPos) {
            (void)spx_qmi_loc_inject_position(loc, lat, lon, horUnc, kQmiTimeoutMs, &res);
        }
        if (doReconf) {
            (void)spx_qmi_loc_stop(loc, sessionId, kQmiTimeoutMs, &res);
            (void)spx_qmi_loc_set_operation_mode(loc, mOpMode.load(), kQmiTimeoutMs, &res);
            spx_qmi_loc_start_params sp{};
            sp.session_id = sessionId;
            sp.has_fix_recurrence = true;
            sp.fix_recurrence = mRecurrence.load();
            sp.has_intermediate_report_state = true;
            sp.intermediate_report_state = SPX_QMI_LOC_INTERMEDIATE_ENABLE;
            sp.has_min_interval_ms = true;
            sp.min_interval_ms = mMinIntervalMs.load();
            if (spx_qmi_loc_start(loc, &sp, kQmiTimeoutMs, &res) != 0) {
                return false;
            }
        }
        return true;
    }

    void handleSvInfo(const uint8_t *pkt, size_t len, const spx_gnss_fix &nmeaFix,
                      const std::set<uint16_t> &usedSet) {
        if (!mSvStatusActive.load())
            return;
        auto cb = getCallback();
        if (!cb)
            return;

        spx_qmi_loc_sv_info svInfo{};
        if (spx_qmi_loc_decode_sv_info_ind(pkt, len, &svInfo) != 0)
            return;

        std::vector<IGnssCallback::GnssSvInfo> out;
        out.reserve(svInfo.count);
        for (uint8_t i = 0; i < svInfo.count; ++i) {
            const auto &src = svInfo.svs[i];
            if ((src.valid_mask & SPX_QMI_LOC_SV_VALID_ID) == 0)
                continue;
            GnssConstellationType c = (src.valid_mask & SPX_QMI_LOC_SV_VALID_SYSTEM)
                                            ? mapQmiSystem(src.system)
                                            : GnssConstellationType::GPS;
            IGnssCallback::GnssSvInfo dst{};
            dst.svid = static_cast<int32_t>(src.sv_id);
            dst.constellation = c;
            dst.cN0Dbhz = (src.valid_mask & SPX_QMI_LOC_SV_VALID_SNR) ? src.snr_dbhz : 0.0f;
            dst.basebandCN0DbHz = dst.cN0Dbhz;
            dst.elevationDegrees =
                (src.valid_mask & SPX_QMI_LOC_SV_VALID_ELEVATION) ? src.elevation_deg : 0.0f;
            dst.azimuthDegrees =
                (src.valid_mask & SPX_QMI_LOC_SV_VALID_AZIMUTH) ? src.azimuth_deg : 0.0f;
            float freq = carrierFrequencyForConstellation(c);
            dst.carrierFrequencyHz = static_cast<int64_t>(freq);

            int32_t flags = static_cast<int32_t>(IGnssCallback::GnssSvFlags::NONE);
            if (freq > 0.0f) {
                flags |= static_cast<int32_t>(IGnssCallback::GnssSvFlags::HAS_CARRIER_FREQUENCY);
            }
            if ((src.nav_data_mask & 0x01u) != 0) {
                flags |= static_cast<int32_t>(IGnssCallback::GnssSvFlags::HAS_EPHEMERIS_DATA);
            }
            if ((src.nav_data_mask & 0x02u) != 0) {
                flags |= static_cast<int32_t>(IGnssCallback::GnssSvFlags::HAS_ALMANAC_DATA);
            }
            if (usedSet.count(src.sv_id) && !mConfig->isBlocklisted(c, dst.svid)) {
                flags |= static_cast<int32_t>(IGnssCallback::GnssSvFlags::USED_IN_FIX);
            }
            dst.svFlag = flags;
            out.push_back(dst);
        }

        if (out.empty() && nmeaFix.sv_count > 0) {
            out.reserve(nmeaFix.sv_count);
            for (uint8_t i = 0; i < nmeaFix.sv_count; ++i) {
                const auto &sv = nmeaFix.svs[i];
                GnssConstellationType c = mapNmeaTalker(sv.constellation);
                IGnssCallback::GnssSvInfo dst{};
                dst.svid = sv.prn;
                dst.constellation = c;
                dst.cN0Dbhz = sv.has_snr ? static_cast<float>(sv.snr_dbhz) : 0.0f;
                dst.basebandCN0DbHz = dst.cN0Dbhz;
                dst.elevationDegrees =
                    sv.has_elevation ? static_cast<float>(sv.elevation_deg) : 0.0f;
                dst.azimuthDegrees = sv.has_azimuth ? static_cast<float>(sv.azimuth_deg) : 0.0f;
                float freq = carrierFrequencyForConstellation(c);
                dst.carrierFrequencyHz = static_cast<int64_t>(freq);
                int32_t flags = freq > 0.0f
                                    ? static_cast<int32_t>(
                                          IGnssCallback::GnssSvFlags::HAS_CARRIER_FREQUENCY)
                                    : 0;
                if (usedSet.count(sv.prn) && !mConfig->isBlocklisted(c, dst.svid)) {
                    flags |= static_cast<int32_t>(IGnssCallback::GnssSvFlags::USED_IN_FIX);
                }
                dst.svFlag = flags;
                out.push_back(dst);
            }
        }

        cb->gnssSvStatusCb(out);
    }

    void workerLoop() {
        spx_qmi_loc_client loc{};
        spx_qmi_loc_client_init(&loc);
        spx_gnss_fix fix{};
        spx_gnss_fix_init(&fix);
        std::set<uint16_t> usedSvIds;
        constexpr uint8_t kSessionId = 1;
        bool sessionOpen = startQmiSession(&loc, kSessionId);
        bool unlockedOnce = false;

        while (!mStopRequested.load()) {
            struct pollfd pfds[2];
            nfds_t nfds = 0;
            pfds[nfds++] = {mWakeFd, POLLIN, 0};
            if (sessionOpen && loc.connected && loc.fd >= 0) {
                pfds[nfds++] = {loc.fd, POLLIN, 0};
            }

            int timeoutMs = (sessionOpen && loc.connected) ? -1 : kReconnectDelayMs;
            int pr = ::poll(pfds, nfds, timeoutMs);
            if (pr < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (mStopRequested.load())
                break;

            if (pfds[0].revents & POLLIN) {
                drainWakeFd();
                if (sessionOpen && !flushPendingInjections(&loc, kSessionId)) {
                    stopQmiSession(&loc, kSessionId, true);
                    sessionOpen = false;
                }
            }

            if (!sessionOpen) {
                if (!mStopRequested.load()) {
                    sessionOpen = startQmiSession(&loc, kSessionId);
                }
                continue;
            }

            if (nfds > 1 && (pfds[1].revents & (POLLERR | POLLHUP | POLLNVAL))) {
                logWarn("QRTR LOC socket error");
                stopQmiSession(&loc, kSessionId, true);
                sessionOpen = false;
                continue;
            }

            if (nfds > 1 && (pfds[1].revents & POLLIN)) {
                uint8_t rx[4096];
                size_t len = 0;
                spx_qrtr_service_info ctrl{};
                int r = spx_qmi_loc_recv(&loc, rx, sizeof(rx), &len, &ctrl);
                if (r == 2) {
                    if (spx_qrtr_match_del_server(&ctrl, SPX_QMI_SERVICE_LOC, loc.service_node,
                                                  loc.service_port)) {
                        logWarn("QMI_SERVICE_LOC server departed");
                        stopQmiSession(&loc, kSessionId, true);
                        sessionOpen = false;
                    }
                } else if (r < 0) {
                    stopQmiSession(&loc, kSessionId, true);
                    sessionOpen = false;
                } else if (r == 1 && len >= SPX_QMI_HEADER_LEN &&
                           rx[0] == SPX_QMI_FLAG_INDICATION) {
                    uint16_t mid = static_cast<uint16_t>(rx[3]) |
                                   (static_cast<uint16_t>(rx[4]) << 8);
                    if (mid == SPX_QMI_LOC_EVENT_NMEA_IND) {
                        char nmea[SPX_QMI_LOC_MAX_NMEA_LEN + 2];
                        size_t nlen = 0;
                        if (spx_qmi_loc_decode_nmea_ind(rx, len, nmea, sizeof(nmea), &nlen) == 0 &&
                            spx_nmea_verify_checksum(nmea, nlen)) {
                            spx_nmea_sentence parsed{};
                            if (spx_nmea_parse(nmea, nlen, &parsed) == 0) {
                                spx_gnss_fix_apply_nmea(&fix, &parsed);
                                if (parsed.type == SPX_NMEA_GSA) {
                                    usedSvIds.clear();
                                    for (uint8_t i = 0; i < parsed.u.gsa.sv_count; ++i) {
                                        if (parsed.u.gsa.sv_ids[i] > 0) {
                                            usedSvIds.insert(parsed.u.gsa.sv_ids[i]);
                                        }
                                    }
                                }
                            }
                            if (mNmeaActive.load()) {
                                if (auto cb = getCallback()) {
                                    cb->gnssNmeaCb(wallTimeMs(), std::string(nmea, nlen));
                                }
                            }
                        }
                    } else if (mid == SPX_QMI_LOC_EVENT_GNSS_SV_INFO_IND) {
                        handleSvInfo(rx, len, fix, usedSvIds);
                    } else if (mid == SPX_QMI_LOC_EVENT_POSITION_REPORT_IND) {
                        spx_qmi_loc_position pos{};
                        if (spx_qmi_loc_decode_position_ind(rx, len, &pos) == 0) {
                            spx_gnss_fix_apply_position(&fix, &pos);
                            if (pos.has_sats_used) {
                                usedSvIds.clear();
                                for (uint8_t i = 0; i < pos.sats_used_count; ++i) {
                                    if (pos.sats_used[i] > 0) {
                                        usedSvIds.insert(pos.sats_used[i]);
                                    }
                                }
                            }
                            if ((pos.session_status == SPX_QMI_LOC_SESSION_ENGINE_LOCKED ||
                                 pos.session_status == SPX_QMI_LOC_SESSION_PHONE_OFFLINE) &&
                                !unlockedOnce) {
                                unlockedOnce = true;
                                spx_qmi_result res{};
                                (void)spx_qmi_loc_set_engine_lock(&loc, SPX_QMI_LOC_LOCK_NONE,
                                                                  kQmiTimeoutMs, &res);
                            }
                            GnssLocation locOut{};
                            if (positionShouldReport(pos, &locOut)) {
                                {
                                    std::lock_guard<std::mutex> lock(mState->mutex);
                                    mState->hasLastFix = true;
                                    mState->lastFix = locOut;
                                }
                                if (auto cb = getCallback()) {
                                    cb->gnssLocationCb(locOut);
                                }
                            }
                        }
                    } else if (mid == SPX_QMI_LOC_EVENT_INJECT_TIME_REQ_IND) {
                        if (auto cb = getCallback()) {
                            cb->gnssRequestTimeCb();
                        }
                    }
                }
            }
        }

        if (sessionOpen) {
            stopQmiSession(&loc, kSessionId, true);
        } else {
            spx_qmi_loc_close(&loc);
        }
    }

    std::shared_ptr<CachedState> mState;
    std::shared_ptr<SpxGnssConfiguration> mConfig;
    std::shared_ptr<SpxGnssMeasurement> mMeasurement;
    std::shared_ptr<SpxGnssPowerIndication> mPower;
    std::shared_ptr<SpxAGnss> mAGnss;
    std::shared_ptr<SpxAGnssRil> mAGnssRil;
    std::shared_ptr<SpxGnssAntennaInfo> mAntenna;
    std::shared_ptr<SpxGnssVisibilityControl> mVisibility;
    std::shared_ptr<SpxGnssPsds> mPsds;
    std::shared_ptr<SpxGnssDebug> mDebug;

    mutable std::mutex mCbMutex;
    std::shared_ptr<IGnssCallback> mCallback;

    std::mutex mSessionMutex;
    std::thread mWorker;
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mStopRequested{false};
    std::atomic<bool> mSvStatusActive{false};
    std::atomic<bool> mNmeaActive{false};
    std::atomic<uint32_t> mMinIntervalMs{1000};
    std::atomic<uint32_t> mOpMode{SPX_QMI_LOC_OPMODE_DEFAULT};
    std::atomic<uint32_t> mRecurrence{SPX_QMI_LOC_RECURRENCE_PERIODIC};
    std::atomic<int> mWakeFd{-1};
};

static int runSelfTest() {
    /* Reject coarse 1155 km CELLID IN_PROGRESS report. */
    spx_qmi_loc_position coarse{};
    coarse.session_status = SPX_QMI_LOC_SESSION_IN_PROGRESS;
    coarse.has_lat = true;
    coarse.latitude = 30.0;
    coarse.has_lon = true;
    coarse.longitude = 104.0;
    coarse.has_hor_unc_ellipse_major = true;
    coarse.hor_unc_ellipse_major = 1155132.5f;
    coarse.has_technology = true;
    coarse.technology = SPX_QMI_LOC_TECH_CELLULAR;
    GnssLocation out{};
    if (positionShouldReport(coarse, &out)) {
        fprintf(stderr, "FAIL: coarse CELLID report accepted as GNSS fix\n");
        return 1;
    }

    /* Accept converged satellite fix with elliptical/circular uncertainty. */
    spx_qmi_loc_position good{};
    good.session_status = SPX_QMI_LOC_SESSION_SUCCESS;
    good.has_lat = true;
    good.latitude = 30.6586;
    good.has_lon = true;
    good.longitude = 104.0648;
    good.has_hor_unc_circular = true;
    good.hor_unc_circular = 4.5f;
    good.has_alt_sealevel = true;
    good.alt_sealevel = 500.0f;
    good.has_technology = true;
    good.technology = SPX_QMI_LOC_TECH_SATELLITE;
    good.sats_used_count = 7;
    if (!positionShouldReport(good, &out) ||
        std::abs(out.latitudeDegrees - 30.6586) > 1e-6 ||
        std::abs(out.horizontalAccuracyMeters - 4.5) > 1e-3 ||
        (out.gnssLocationFlags & GnssLocation::HAS_ALTITUDE) == 0) {
        fprintf(stderr, "FAIL: valid GNSS fix rejected or misconverted\n");
        return 1;
    }

    if (mapQmiSystem(SPX_QMI_LOC_SYSTEM_COMPASS) != GnssConstellationType::BEIDOU ||
        mapQmiSystem(SPX_QMI_LOC_SYSTEM_GLONASS) != GnssConstellationType::GLONASS ||
        mapNmeaTalker(SPX_NMEA_TALKER_GA) != GnssConstellationType::GALILEO) {
        fprintf(stderr, "FAIL: constellation mapping mismatch\n");
        return 1;
    }

    puts("PASS spx-gnss-hal --self-test");
    return 0;
}

}  // namespace
}  // namespace aidl::android::hardware::gnss

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--self-test") == 0) {
        return aidl::android::hardware::gnss::runSelfTest();
    }

    ABinderProcess_setThreadPoolMaxThreadCount(2);
    ABinderProcess_startThreadPool();

    auto gnss = ndk::SharedRefBase::make<aidl::android::hardware::gnss::SpxGnss>();
    const std::string instance =
        std::string(aidl::android::hardware::gnss::IGnss::descriptor) + "/default";
    binder_status_t status =
        AServiceManager_addService(gnss->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        __android_log_print(ANDROID_LOG_ERROR, aidl::android::hardware::gnss::kLogTag,
                            "AServiceManager_addService(%s) failed: %d", instance.c_str(), status);
        fprintf(stderr, "spx-gnss-hal: addService(%s) failed: %d\n", instance.c_str(), status);
        return 1;
    }
    aidl::android::hardware::gnss::logInfo("Registered android.hardware.gnss.IGnss/default");
    ABinderProcess_joinThreadPool();
    return 1;
}
