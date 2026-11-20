#pragma once

#include "sq2-v4l2-enc-session.h"

#include <C2Config.h>
#include <C2PlatformSupport.h>
#include <SimpleC2Interface.h>
#include <system/graphics.h>
#include <linux/videodev2.h>
#include <algorithm>
#include <memory>
#include <vector>

class Sq2EncInterface final : public android::SimpleInterface<void>::BaseParams {
public:
    explicit Sq2EncInterface(const std::shared_ptr<C2ReflectorHelper> &helper,
                             const Sq2EncCodecInfo &info)
        : android::SimpleInterface<void>::BaseParams(
                helper,
                info.c2Name,
                C2Component::KIND_ENCODER,
                C2Component::DOMAIN_VIDEO,
                info.mime),
          mCodecInfo(info) {
        noPrivateBuffers();
        noInputReferences();
        noOutputReferences();
        noTimeStretch();
        setDerivedInstance(this);

        addParameter(
                DefineParam(mAttrib, C2_PARAMKEY_COMPONENT_ATTRIBUTES)
                .withConstValue(new C2ComponentAttributesSetting(
                        C2Component::ATTRIB_IS_TEMPORAL))
                .build());

        addParameter(
                DefineParam(mUsage, C2_PARAMKEY_INPUT_STREAM_USAGE)
                .withConstValue(new C2StreamUsageTuning::input(
                        0u, static_cast<uint64_t>(C2MemoryUsage::CPU_READ)))
                .build());

        addParameter(
                DefineParam(mPixelFormat, C2_PARAMKEY_PIXEL_FORMAT)
                .withDefault(new C2StreamPixelFormatInfo::input(
                        0u, HAL_PIXEL_FORMAT_YCBCR_420_888))
                .withFields({
                    C2F(mPixelFormat, value).oneOf({
                        HAL_PIXEL_FORMAT_YCBCR_420_888,
                        HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
                        HAL_PIXEL_FORMAT_RGBA_8888,
                    }),
                })
                .withSetter(android::Setter<decltype(*mPixelFormat)>::NonStrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mSize, C2_PARAMKEY_PICTURE_SIZE)
                .withDefault(new C2StreamPictureSizeInfo::input(0u, 1280, 720))
                .withFields({
                    C2F(mSize, width).inRange(info.minWidth, info.maxWidth, info.stepWidth),
                    C2F(mSize, height).inRange(info.minHeight, info.maxHeight, info.stepHeight),
                })
                .withSetter(SizeSetter)
                .build());

        addParameter(
                DefineParam(mFrameRate, C2_PARAMKEY_FRAME_RATE)
                .withDefault(new C2StreamFrameRateInfo::output(0u, 30.f))
                .withFields({C2F(mFrameRate, value).inRange(1.f, static_cast<float>(info.maxFps))})
                .withSetter(android::Setter<decltype(*mFrameRate)>::NonStrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mBitrateMode, C2_PARAMKEY_BITRATE_MODE)
                .withDefault(new C2StreamBitrateModeTuning::output(
                        0u, C2Config::BITRATE_CONST))
                .withFields({
                    C2F(mBitrateMode, value).oneOf({
                        C2Config::BITRATE_CONST,
                    }),
                })
                .withSetter(android::Setter<decltype(*mBitrateMode)>::StrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mBitrate, C2_PARAMKEY_BITRATE)
                .withDefault(new C2StreamBitrateInfo::output(0u, 4000000))
                .withFields({C2F(mBitrate, value).inRange(32000u, info.maxBitrate)})
                .withSetter(BitrateSetter)
                .build());

        addParameter(
                DefineParam(mGop, C2_PARAMKEY_GOP)
                .withDefault(C2StreamGopTuning::output::AllocShared(0, 0u))
                .withFields({
                    C2F(mGop, m.values[0].type_).any(),
                    C2F(mGop, m.values[0].count).any(),
                })
                .withSetter(GopSetter)
                .build());

        addParameter(
                DefineParam(mSyncFramePeriod, C2_PARAMKEY_SYNC_FRAME_INTERVAL)
                .withDefault(new C2StreamSyncFrameIntervalTuning::output(0u, 1000000))
                .withFields({C2F(mSyncFramePeriod, value).any()})
                .withSetter(android::Setter<decltype(*mSyncFramePeriod)>::StrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mRequestSync, C2_PARAMKEY_REQUEST_SYNC_FRAME)
                .withDefault(new C2StreamRequestSyncFrameTuning::output(0u, C2_FALSE))
                .withFields({C2F(mRequestSync, value).oneOf({C2_FALSE, C2_TRUE})})
                .withSetter(android::Setter<decltype(*mRequestSync)>::NonStrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mPrependHeader, C2_PARAMKEY_PREPEND_HEADER_MODE)
                .withDefault(new C2PrependHeaderModeSetting(
                        C2Config::PREPEND_HEADER_TO_ALL_SYNC))
                .withFields({
                    C2F(mPrependHeader, value).oneOf({
                        C2Config::PREPEND_HEADER_TO_NONE,
                        C2Config::PREPEND_HEADER_ON_CHANGE,
                        C2Config::PREPEND_HEADER_TO_ALL_SYNC,
                    }),
                })
                .withSetter(android::Setter<decltype(*mPrependHeader)>::StrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mColorAspects, C2_PARAMKEY_COLOR_ASPECTS)
                .withDefault(new C2StreamColorAspectsInfo::input(
                        0u, C2Color::RANGE_UNSPECIFIED, C2Color::PRIMARIES_UNSPECIFIED,
                        C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED))
                .withFields({
                    C2F(mColorAspects, range).inRange(C2Color::RANGE_UNSPECIFIED, C2Color::RANGE_OTHER),
                    C2F(mColorAspects, primaries).inRange(C2Color::PRIMARIES_UNSPECIFIED, C2Color::PRIMARIES_OTHER),
                    C2F(mColorAspects, transfer).inRange(C2Color::TRANSFER_UNSPECIFIED, C2Color::TRANSFER_OTHER),
                    C2F(mColorAspects, matrix).inRange(C2Color::MATRIX_UNSPECIFIED, C2Color::MATRIX_OTHER),
                })
                .withSetter(ColorAspectsSetter)
                .build());

        addParameter(
                DefineParam(mCodedColorAspects, C2_PARAMKEY_VUI_COLOR_ASPECTS)
                .withDefault(new C2StreamColorAspectsInfo::output(
                        0u, C2Color::RANGE_LIMITED, C2Color::PRIMARIES_BT601_625,
                        C2Color::TRANSFER_170M, C2Color::MATRIX_BT601))
                .withFields({
                    C2F(mCodedColorAspects, range).inRange(C2Color::RANGE_UNSPECIFIED, C2Color::RANGE_OTHER),
                    C2F(mCodedColorAspects, primaries).inRange(C2Color::PRIMARIES_UNSPECIFIED, C2Color::PRIMARIES_OTHER),
                    C2F(mCodedColorAspects, transfer).inRange(C2Color::TRANSFER_UNSPECIFIED, C2Color::TRANSFER_OTHER),
                    C2F(mCodedColorAspects, matrix).inRange(C2Color::MATRIX_UNSPECIFIED, C2Color::MATRIX_OTHER),
                })
                .withSetter(CodedColorAspectsSetter, mColorAspects)
                .build());

        addParameter(
                DefineParam(mDataSpace, C2_PARAMKEY_DATA_SPACE)
                .withDefault(new C2StreamDataSpaceInfo::input(0u, 0u))
                .withFields({C2F(mDataSpace, value).any()})
                .withSetter(android::Setter<decltype(*mDataSpace)>::NonStrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mIntraRefresh, C2_PARAMKEY_INTRA_REFRESH)
                .withConstValue(new C2StreamIntraRefreshTuning::output(
                        0u, C2Config::INTRA_REFRESH_DISABLED, 0.f))
                .build());

        if (info.codec == Sq2EncCodec::H264) {
            addParameter(
                    DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                    .withDefault(new C2StreamProfileLevelInfo::output(
                            0u, C2Config::PROFILE_AVC_HIGH, C2Config::LEVEL_AVC_5_1))
                    .withFields({
                        C2F(mProfileLevel, profile).oneOf({
                            C2Config::PROFILE_AVC_BASELINE,
                            C2Config::PROFILE_AVC_CONSTRAINED_BASELINE,
                            C2Config::PROFILE_AVC_MAIN,
                            C2Config::PROFILE_AVC_HIGH,
                            C2Config::PROFILE_AVC_CONSTRAINED_HIGH,
                        }),
                        C2F(mProfileLevel, level).oneOf({
                            C2Config::LEVEL_AVC_1, C2Config::LEVEL_AVC_1B,
                            C2Config::LEVEL_AVC_1_1, C2Config::LEVEL_AVC_1_2, C2Config::LEVEL_AVC_1_3,
                            C2Config::LEVEL_AVC_2, C2Config::LEVEL_AVC_2_1, C2Config::LEVEL_AVC_2_2,
                            C2Config::LEVEL_AVC_3, C2Config::LEVEL_AVC_3_1, C2Config::LEVEL_AVC_3_2,
                            C2Config::LEVEL_AVC_4, C2Config::LEVEL_AVC_4_1, C2Config::LEVEL_AVC_4_2,
                            C2Config::LEVEL_AVC_5, C2Config::LEVEL_AVC_5_1, C2Config::LEVEL_AVC_5_2,
                            C2Config::LEVEL_AVC_6, C2Config::LEVEL_AVC_6_1, C2Config::LEVEL_AVC_6_2,
                        }),
                    })
                    .withSetter(ProfileLevelSetter)
                    .build());
        } else if (info.codec == Sq2EncCodec::HEVC) {
            addParameter(
                    DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                    .withDefault(new C2StreamProfileLevelInfo::output(
                            0u, C2Config::PROFILE_HEVC_MAIN, C2Config::LEVEL_HEVC_MAIN_5_1))
                    .withFields({
                        C2F(mProfileLevel, profile).oneOf({
                            C2Config::PROFILE_HEVC_MAIN,
                            C2Config::PROFILE_HEVC_MAIN_STILL,
                        }),
                        C2F(mProfileLevel, level).oneOf({
                            C2Config::LEVEL_HEVC_MAIN_1, C2Config::LEVEL_HEVC_MAIN_2,
                            C2Config::LEVEL_HEVC_MAIN_2_1, C2Config::LEVEL_HEVC_MAIN_3,
                            C2Config::LEVEL_HEVC_MAIN_3_1, C2Config::LEVEL_HEVC_MAIN_4,
                            C2Config::LEVEL_HEVC_MAIN_4_1, C2Config::LEVEL_HEVC_MAIN_5,
                            C2Config::LEVEL_HEVC_MAIN_5_1, C2Config::LEVEL_HEVC_MAIN_5_2,
                            C2Config::LEVEL_HEVC_MAIN_6,
                        }),
                    })
                    .withSetter(ProfileLevelSetter)
                    .build());
        } else {
            addParameter(
                    DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                    .withDefault(new C2StreamProfileLevelInfo::output(
                            0u, C2Config::PROFILE_VP8_0, C2Config::LEVEL_UNUSED))
                    .withFields({
                        C2F(mProfileLevel, profile).oneOf({C2Config::PROFILE_VP8_0}),
                        C2F(mProfileLevel, level).oneOf({C2Config::LEVEL_UNUSED}),
                    })
                    .withSetter(ProfileLevelSetter)
                    .build());
        }
    }

    const Sq2EncCodecInfo &codecInfo() const { return mCodecInfo; }

    Sq2EncParams snapshotParams() {
        auto l = lock();
        Sq2EncParams p{};
        p.codec = mCodecInfo.codec;
        p.width = mSize->width;
        p.height = mSize->height;
        p.fps = static_cast<uint32_t>(std::max(1.f, mFrameRate->value + 0.5f));
        p.bitrate = mBitrate->value;
        p.bitrateMode = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
        int64_t syncUs = mSyncFramePeriod->value;
        if (syncUs <= 0) {
            p.gopSize = p.fps * 600u;
        } else {
            uint64_t g = (static_cast<uint64_t>(syncUs) * p.fps + 500000ULL) / 1000000ULL;
            p.gopSize = static_cast<uint32_t>(std::max<uint64_t>(1, g));
        }
        for (size_t i = 0; i < mGop->flexCount(); ++i) {
            const auto &layer = mGop->m.values[i];
            if ((layer.type_ & C2Config::picture_type_t::I_FRAME) != 0 && layer.count > 0 && layer.count < UINT32_MAX) {
                p.gopSize = layer.count;
            }
        }
        p.prependHeader = (mPrependHeader->value != C2Config::PREPEND_HEADER_TO_NONE);
        p.profile = mapProfile(mCodecInfo.codec, mProfileLevel->profile);
        p.level = mapLevel(mCodecInfo.codec, mProfileLevel->level);
        return p;
    }

    uint32_t getBitrateValue() { auto l = lock(); return mBitrate->value; }
    bool consumeSyncRequest() {
        auto l = lock();
        if (mRequestSync->value == C2_FALSE) return false;
        l.unlock();
        C2StreamRequestSyncFrameTuning::output clearSync(0u, C2_FALSE);
        std::vector<std::unique_ptr<C2SettingResult>> failures;
        config({&clearSync}, C2_MAY_BLOCK, &failures);
        return true;
    }

    C2BlockPool::local_id_t outputPoolId() {
        auto l = lock();
        if (mOutputPoolIds && mOutputPoolIds->flexCount() > 0) {
            return mOutputPoolIds->m.values[0];
        }
        return C2BlockPool::BASIC_LINEAR;
    }

private:
    static C2R SizeSetter(bool, const C2P<C2StreamPictureSizeInfo::input> &oldMe,
                          C2P<C2StreamPictureSizeInfo::input> &me) {
        C2R res = C2R::Ok();
        if (!me.F(me.v.width).supportsAtAll(me.v.width)) {
            res = res.plus(C2SettingResultBuilder::BadValue(me.F(me.v.width)));
            me.set().width = oldMe.v.width;
        }
        if (!me.F(me.v.height).supportsAtAll(me.v.height)) {
            res = res.plus(C2SettingResultBuilder::BadValue(me.F(me.v.height)));
            me.set().height = oldMe.v.height;
        }
        return res;
    }

    static C2R BitrateSetter(bool, C2P<C2StreamBitrateInfo::output> &me) {
        if (me.v.value < 32000u) me.set().value = 32000u;
        return C2R::Ok();
    }

    static C2R GopSetter(bool, C2P<C2StreamGopTuning::output> &) { return C2R::Ok(); }

    static C2R ColorAspectsSetter(bool, const C2P<C2StreamColorAspectsInfo::input> &,
                                  C2P<C2StreamColorAspectsInfo::input> &) {
        return C2R::Ok();
    }

    static C2R CodedColorAspectsSetter(bool,
                                       C2P<C2StreamColorAspectsInfo::output> &me,
                                       const C2P<C2StreamColorAspectsInfo::input> &coded) {
        (void)coded;
        if (me.v.range == C2Color::RANGE_UNSPECIFIED) me.set().range = C2Color::RANGE_LIMITED;
        if (me.v.primaries == C2Color::PRIMARIES_UNSPECIFIED) me.set().primaries = C2Color::PRIMARIES_BT601_625;
        if (me.v.transfer == C2Color::TRANSFER_UNSPECIFIED) me.set().transfer = C2Color::TRANSFER_170M;
        if (me.v.matrix == C2Color::MATRIX_UNSPECIFIED) me.set().matrix = C2Color::MATRIX_BT601;
        return C2R::Ok();
    }

    static C2R ProfileLevelSetter(bool, C2P<C2StreamProfileLevelInfo::output> &me) {
        if (!me.F(me.v.profile).supportsAtAll(me.v.profile)) {
            return C2R(C2SettingResultBuilder::BadValue(me.F(me.v.profile)));
        }
        if (!me.F(me.v.level).supportsAtAll(me.v.level)) {
            return C2R(C2SettingResultBuilder::BadValue(me.F(me.v.level)));
        }
        return C2R::Ok();
    }

    static int32_t mapProfile(Sq2EncCodec codec, C2Config::profile_t p) {
        if (codec == Sq2EncCodec::H264) {
            switch (p) {
                case C2Config::PROFILE_AVC_BASELINE:
                case C2Config::PROFILE_AVC_CONSTRAINED_BASELINE:
                    return V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE;
                case C2Config::PROFILE_AVC_MAIN:
                    return V4L2_MPEG_VIDEO_H264_PROFILE_MAIN;
                default:
                    return V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
            }
        }
        return V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN;
    }

    static int32_t mapLevel(Sq2EncCodec codec, C2Config::level_t l) {
        if (codec == Sq2EncCodec::H264) {
            switch (l) {
                case C2Config::LEVEL_AVC_1:   return V4L2_MPEG_VIDEO_H264_LEVEL_1_0;
                case C2Config::LEVEL_AVC_1B:  return V4L2_MPEG_VIDEO_H264_LEVEL_1B;
                case C2Config::LEVEL_AVC_1_1: return V4L2_MPEG_VIDEO_H264_LEVEL_1_1;
                case C2Config::LEVEL_AVC_1_2: return V4L2_MPEG_VIDEO_H264_LEVEL_1_2;
                case C2Config::LEVEL_AVC_1_3: return V4L2_MPEG_VIDEO_H264_LEVEL_1_3;
                case C2Config::LEVEL_AVC_2:   return V4L2_MPEG_VIDEO_H264_LEVEL_2_0;
                case C2Config::LEVEL_AVC_2_1: return V4L2_MPEG_VIDEO_H264_LEVEL_2_1;
                case C2Config::LEVEL_AVC_2_2: return V4L2_MPEG_VIDEO_H264_LEVEL_2_2;
                case C2Config::LEVEL_AVC_3:   return V4L2_MPEG_VIDEO_H264_LEVEL_3_0;
                case C2Config::LEVEL_AVC_3_1: return V4L2_MPEG_VIDEO_H264_LEVEL_3_1;
                case C2Config::LEVEL_AVC_3_2: return V4L2_MPEG_VIDEO_H264_LEVEL_3_2;
                case C2Config::LEVEL_AVC_4:   return V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
                case C2Config::LEVEL_AVC_4_1: return V4L2_MPEG_VIDEO_H264_LEVEL_4_1;
                case C2Config::LEVEL_AVC_4_2: return V4L2_MPEG_VIDEO_H264_LEVEL_4_2;
                case C2Config::LEVEL_AVC_5:   return V4L2_MPEG_VIDEO_H264_LEVEL_5_0;
                case C2Config::LEVEL_AVC_5_1: return V4L2_MPEG_VIDEO_H264_LEVEL_5_1;
                default:                      return V4L2_MPEG_VIDEO_H264_LEVEL_5_1;
            }
        }
        return V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1;
    }

    Sq2EncCodecInfo mCodecInfo;
    std::shared_ptr<C2StreamUsageTuning::input> mUsage;
    std::shared_ptr<C2StreamPixelFormatInfo::input> mPixelFormat;
    std::shared_ptr<C2StreamPictureSizeInfo::input> mSize;
    std::shared_ptr<C2StreamFrameRateInfo::output> mFrameRate;
    std::shared_ptr<C2StreamBitrateModeTuning::output> mBitrateMode;
    std::shared_ptr<C2StreamBitrateInfo::output> mBitrate;
    std::shared_ptr<C2StreamGopTuning::output> mGop;
    std::shared_ptr<C2StreamSyncFrameIntervalTuning::output> mSyncFramePeriod;
    std::shared_ptr<C2StreamRequestSyncFrameTuning::output> mRequestSync;
    std::shared_ptr<C2PrependHeaderModeSetting> mPrependHeader;
    std::shared_ptr<C2StreamColorAspectsInfo::input> mColorAspects;
    std::shared_ptr<C2StreamColorAspectsInfo::output> mCodedColorAspects;
    std::shared_ptr<C2StreamDataSpaceInfo::input> mDataSpace;
    std::shared_ptr<C2StreamIntraRefreshTuning::output> mIntraRefresh;
    std::shared_ptr<C2StreamProfileLevelInfo::output> mProfileLevel;
};
