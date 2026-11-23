// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "spx-ir-preprocess.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace spx::face {

inline constexpr size_t kEmbeddingDim = 256;
inline constexpr size_t kMaxEnrollmentsPerUser = 2;
inline constexpr size_t kSamplesPerEnrollment = 6;
inline constexpr float kDefaultMatchThreshold = 0.74f;
inline constexpr float kDefaultLiveConsistencyThreshold = 0.72f;

enum class FaceQuality {
    GOOD = 0,
    NOT_DETECTED,
    TOO_DARK,
    TOO_BRIGHT,
    TOO_CLOSE,
    TOO_FAR,
    FACE_TOO_LEFT,
    FACE_TOO_RIGHT,
    FACE_TOO_HIGH,
    FACE_TOO_LOW,
    PAN_TOO_EXTREME,
    TILT_TOO_EXTREME,
    TOO_MUCH_MOTION,
    SPOOF_DETECTED,
};

enum class LivenessStatus {
    PASS = 0,
    NEED_MORE_FRAMES,
    REJECT_SCREEN_EMISSION,
    REJECT_FLAT_OR_PRINT,
    REJECT_SPECULAR_GLARE,
    REJECT_HALFTONE_PATTERN,
    REJECT_FROZEN_REPLAY,
    REJECT_MOTION_BLUR,
    REJECT_UNSTABLE_IDENTITY,
};

struct Point2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct FaceBox {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct FaceDetection {
    bool detected = false;
    FaceQuality quality = FaceQuality::NOT_DETECTED;
    float score = 0.0f;
    FaceBox box{};
    Point2f leftEye{};
    Point2f rightEye{};
    Point2f noseTip{};
    Point2f mouthLeft{};
    Point2f mouthRight{};
    float panDeg = 0.0f;
    float tiltDeg = 0.0f;
    float distanceCm = 0.0f;
};

struct LivenessReport {
    bool live = false;
    LivenessStatus status = LivenessStatus::NEED_MORE_FRAMES;
    uint16_t faceP05 = 0;
    uint16_t faceP95 = 0;
    float centerToRimRatio = 0.0f;
    float halftoneRatio = 0.0f;
    float temporalMad = 0.0f;
};

using Embedding = std::array<float, kEmbeddingDim>;

struct OptionalModelWeights {
    std::vector<float> mean;
    std::vector<float> proj;  // kEmbeddingDim x kEmbeddingDim row-major
    bool loaded = false;

    bool loadFromFile(const std::string& path);
    void unload();
};

struct FaceSessionAccumulator {
    unsigned minConsecutiveFrames = 2;
    unsigned frameCount = 0;
    unsigned consecutiveLiveFrames = 0;
    bool hasPrevPatch = false;
    std::array<uint8_t, 32 * 32> prevPatch{};
    bool hasPrevEmbedding = false;
    Embedding prevEmbedding{};

    void reset(unsigned requiredConsecutiveFrames = 2);
};

struct MultiOrientationResult {
    FaceDetection detection{};
    Rotation rotation = Rotation::Deg0;
    bool usedRotatedStats = false;
    IrFrameStats rotatedStats{};
};

FaceDetection detectFaceNir(const IrFrameStats& frame);

std::vector<FaceDetection> detectFaceCandidatesNir(const IrFrameStats& frame,
                                                   size_t maxCandidates = 4,
                                                   FaceQuality* outEmptyQuality = nullptr);

MultiOrientationResult detectFaceNirMultiOrientation(const IrFrameStats& baseStats,
                                                     Rotation baseRotation,
                                                     bool allowFallback = true,
                                                     bool forceCompareAll = false);

Embedding extractEmbedding(const IrFrameStats& frame,
                           const FaceDetection& det,
                           const OptionalModelWeights* model = nullptr);

float cosineSimilarity(const Embedding& a, const Embedding& b);

LivenessReport evaluateLiveness(const IrFrameStats& frame,
                                const FaceDetection& det,
                                const Embedding& emb,
                                FaceSessionAccumulator* acc,
                                bool enforceTemporalReplayCheck = true);

struct EnrollmentRecord {
    int32_t enrollmentId = 0;
    uint32_t sampleCount = 0;
    std::array<Embedding, kSamplesPerEnrollment> samples{};
};

struct MatchResult {
    bool matched = false;
    int32_t enrollmentId = 0;
    float bestSimilarity = 0.0f;
};

enum class LockoutMode {
    NONE = 0,
    TIMED,
    PERMANENT,
};

struct LockoutState {
    LockoutMode mode = LockoutMode::NONE;
    uint32_t failedAttempts = 0;
    int64_t remainingMs = 0;
};

class SpxLockoutTracker {
public:
    using ClockFn = std::function<int64_t()>;

    explicit SpxLockoutTracker(ClockFn clockMs = nullptr);

    void setFailedAttempts(uint32_t count);
    uint32_t failedAttempts() const { return failedAttempts_; }

    LockoutState checkState() const;
    // Record a failed biometric attempt (face seen, did not match / spoofed).
    LockoutState addFailedAttempt();
    void reset();

private:
    ClockFn clockMs_;
    uint32_t failedAttempts_ = 0;
    int64_t timedUntilMs_ = 0;
};

class SpxFaceStore {
public:
    SpxFaceStore(int32_t userId, std::string baseDirOverride = {});
    ~SpxFaceStore();

    SpxFaceStore(const SpxFaceStore&) = delete;
    SpxFaceStore& operator=(const SpxFaceStore&) = delete;

    const std::string& storageDir() const { return dirPath_; }
    bool load();
    bool save();

    int32_t userId() const { return userId_; }
    int64_t authenticatorId() const { return authenticatorId_; }
    int64_t invalidateAuthenticatorId();

    uint32_t featureMask() const { return featureMask_; }
    void setFeatureBit(uint32_t bit, bool enabled);
    bool hasFeatureBit(uint32_t bit) const;

    uint32_t failedAttempts() const { return failedAttempts_; }
    void setFailedAttempts(uint32_t count);

    std::vector<int32_t> listEnrollmentIds() const;
    size_t enrollmentCount() const { return enrollments_.size(); }
    int32_t addEnrollment(const std::vector<Embedding>& samples);
    std::vector<int32_t> removeEnrollments(const std::vector<int32_t>& ids);
    void wipeAll();

    MatchResult match(const Embedding& probe, float threshold = kDefaultMatchThreshold) const;

private:
    bool ensureKeyLoadedOrCreated(bool createIfMissing);

    int32_t userId_ = 0;
    std::string dirPath_;
    std::string dataPath_;
    std::string keyPath_;
    std::array<uint8_t, 32> masterKey_{};
    bool keyLoaded_ = false;

    int64_t authenticatorId_ = 0;
    uint32_t featureMask_ = 0x03u;  // bit 0: REQUIRE_ATTENTION, bit 1: REQUIRE_DIVERSE_POSES
    uint32_t failedAttempts_ = 0;
    int32_t nextEnrollmentId_ = 1;
    std::vector<EnrollmentRecord> enrollments_;
};

struct RefinedMatchResult {
    MatchResult match{};
    bool usedRefinement = false;
};

MatchResult matchEmbeddings(const std::vector<Embedding>& candidates,
                            const Embedding& probe,
                            float threshold = kDefaultMatchThreshold);

RefinedMatchResult matchWithAlignmentRefinement(
    const IrFrameStats& frame,
    const FaceDetection& det,
    const Embedding& baseEmb,
    const SpxFaceStore& store,
    const OptionalModelWeights* model = nullptr,
    float threshold = kDefaultMatchThreshold);

RefinedMatchResult matchCandidatesWithAlignmentRefinement(
    const IrFrameStats& frame,
    const FaceDetection& det,
    const Embedding& baseEmb,
    const std::vector<Embedding>& candidates,
    const OptionalModelWeights* model = nullptr,
    float threshold = kDefaultMatchThreshold);

struct OrientationSwitchHint {
    bool shouldSwitch = false;
    Rotation rotation = Rotation::Deg0;
    float bestSimilarity = 0.0f;
    float detScore = 0.0f;
};

OrientationSwitchHint probeAlternateOrientationMatch(
    const IrFrameStats& baseStats,
    Rotation baseRotation,
    float baseSimilarity,
    const SpxFaceStore& store,
    const OptionalModelWeights* model = nullptr,
    float matchThreshold = kDefaultMatchThreshold,
    bool requireFullMatch = false);

}  // namespace spx::face
