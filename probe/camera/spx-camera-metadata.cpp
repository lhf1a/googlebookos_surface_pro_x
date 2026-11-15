// SPDX-License-Identifier: GPL-2.0-only
#include "spx-camera-metadata.h"
#include "spx-camera-formats.h"
namespace spx {
Metadata defaults(bool front,int intent) {
    Meta m;
    m.set<uint8_t>(ANDROID_CONTROL_MODE,{ANDROID_CONTROL_MODE_AUTO});
    m.set<uint8_t>(ANDROID_CONTROL_CAPTURE_INTENT,{uint8_t(intent)});
    m.set<uint8_t>(ANDROID_CONTROL_AE_MODE,{ANDROID_CONTROL_AE_MODE_ON});
    m.set<uint8_t>(ANDROID_CONTROL_AE_ANTIBANDING_MODE,{ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF});
    m.set<int32_t>(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,{0});
    m.set<uint8_t>(ANDROID_CONTROL_AE_LOCK,{0});
    m.set<uint8_t>(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER,{0});
    m.set<int32_t>(ANDROID_CONTROL_AE_TARGET_FPS_RANGE,{15,30});
    m.set<uint8_t>(ANDROID_CONTROL_AF_MODE,{ANDROID_CONTROL_AF_MODE_OFF});
    m.set<uint8_t>(ANDROID_CONTROL_AF_TRIGGER,{0});
    m.set<uint8_t>(ANDROID_CONTROL_AWB_MODE,{ANDROID_CONTROL_AWB_MODE_AUTO});
    m.set<uint8_t>(ANDROID_CONTROL_AWB_LOCK,{0});
    m.set<uint8_t>(ANDROID_CONTROL_EFFECT_MODE,{0});
    m.set<uint8_t>(ANDROID_CONTROL_SCENE_MODE,{0});
    m.set<uint8_t>(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,{0});
    m.set<uint8_t>(ANDROID_COLOR_CORRECTION_ABERRATION_MODE,{0});
    m.set<uint8_t>(ANDROID_FLASH_MODE,{0});
    m.set<uint8_t>(ANDROID_LENS_OPTICAL_STABILIZATION_MODE,{0});
    m.set<float>(ANDROID_LENS_FOCUS_DISTANCE,{0.f});
    m.set<uint8_t>(ANDROID_NOISE_REDUCTION_MODE,{0});
    m.set<uint8_t>(ANDROID_EDGE_MODE,{0});
    m.set<uint8_t>(ANDROID_STATISTICS_FACE_DETECT_MODE,{0});
    m.set<uint8_t>(ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE,{0});
    m.set<uint8_t>(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE,{0});
    m.set<int32_t>(ANDROID_SENSOR_TEST_PATTERN_MODE,{0});
    m.set<int32_t>(ANDROID_SCALER_CROP_REGION,{0,0,nativeSize(front).first,nativeSize(front).second});
    m.set<int32_t>(ANDROID_JPEG_ORIENTATION,{0});
    m.set<uint8_t>(ANDROID_JPEG_QUALITY,{90});
    m.set<uint8_t>(ANDROID_JPEG_THUMBNAIL_QUALITY,{90});
    m.set<int32_t>(ANDROID_JPEG_THUMBNAIL_SIZE,{0,0});
    return m.pack();
}
Metadata characteristics(bool front) {
    Meta m;
    m.set<uint8_t>(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,{ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED});
    m.set<uint8_t>(ANDROID_REQUEST_AVAILABLE_CAPABILITIES,{ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE});
    m.set<uint8_t>(ANDROID_LENS_FACING,{uint8_t(front?ANDROID_LENS_FACING_FRONT:ANDROID_LENS_FACING_BACK)});
    m.set<int32_t>(ANDROID_SENSOR_ORIENTATION,{180});
    m.set<uint8_t>(ANDROID_SENSOR_READOUT_TIMESTAMP,{ANDROID_SENSOR_READOUT_TIMESTAMP_NOT_SUPPORTED});
    m.set<uint8_t>(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,{ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN});
    m.set<int32_t>(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,{0,0,nativeSize(front).first,nativeSize(front).second});
    m.set<int32_t>(ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,{0,0,nativeSize(front).first,nativeSize(front).second});
    m.set<int32_t>(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,{nativeSize(front).first,nativeSize(front).second});
    m.set<int32_t>(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,{0});
    m.set<uint8_t>(ANDROID_CONTROL_AVAILABLE_MODES,{ANDROID_CONTROL_MODE_AUTO});
    m.set<uint8_t>(ANDROID_CONTROL_AE_AVAILABLE_MODES,{ANDROID_CONTROL_AE_MODE_ON});
    m.set<uint8_t>(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,{0});
    m.set<int32_t>(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,{15,30});
    m.set<int32_t>(ANDROID_CONTROL_AE_COMPENSATION_RANGE,{0,0});
    m.set<camera_metadata_rational_t>(ANDROID_CONTROL_AE_COMPENSATION_STEP,{{0,1}});
    m.set<uint8_t>(ANDROID_CONTROL_AE_LOCK_AVAILABLE,{0});
    m.set<uint8_t>(ANDROID_CONTROL_AWB_LOCK_AVAILABLE,{0});
    m.set<uint8_t>(ANDROID_CONTROL_AF_AVAILABLE_MODES,{0});
    m.set<uint8_t>(ANDROID_CONTROL_AWB_AVAILABLE_MODES,{ANDROID_CONTROL_AWB_MODE_AUTO});
    m.set<uint8_t>(ANDROID_CONTROL_AVAILABLE_EFFECTS,{0});
    m.set<uint8_t>(ANDROID_CONTROL_AVAILABLE_SCENE_MODES,{0});
    m.set<uint8_t>(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,{0});
    m.set<int32_t>(ANDROID_CONTROL_MAX_REGIONS,{0,0,0});
    m.set<uint8_t>(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,{0});
    m.set<uint8_t>(ANDROID_EDGE_AVAILABLE_EDGE_MODES,{0});
    m.set<uint8_t>(ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES,{0});
    m.set<uint8_t>(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,{0});
    m.set<uint8_t>(ANDROID_SHADING_AVAILABLE_MODES,{0});
    m.set<uint8_t>(ANDROID_FLASH_INFO_AVAILABLE,{0});
    m.set<uint8_t>(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,{0});
    m.set<float>(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,{0.f});
    m.set<float>(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE,{0.f});
    m.set<uint8_t>(ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,{0});
    m.set<uint8_t>(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,{0});
    m.set<int32_t>(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,{0});
    m.set<uint8_t>(ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,{0});
    m.set<uint8_t>(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,{0});
    m.set<int32_t>(ANDROID_REQUEST_PARTIAL_RESULT_COUNT,{1});
    m.set<uint8_t>(ANDROID_REQUEST_PIPELINE_MAX_DEPTH,{4});
    m.set<int32_t>(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,{0,2,1});
    m.set<int32_t>(ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS,{0});
    m.set<int32_t>(ANDROID_SYNC_MAX_LATENCY,{ANDROID_SYNC_MAX_LATENCY_UNKNOWN});
    m.set<float>(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,{1.f});
    m.set<uint8_t>(ANDROID_SCALER_CROPPING_TYPE,{ANDROID_SCALER_CROPPING_TYPE_CENTER_ONLY});
    m.set<int32_t>(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,{0,0});
    m.set<int32_t>(ANDROID_JPEG_MAX_SIZE,{jpegMaxSize(front)});
    std::vector<int32_t> configs;
    std::vector<int64_t> durations,stalls;
    for(int format:{33,34,35}) for(auto wh:outputSizes(front)) {
        configs.insert(configs.end(),{format,wh.first,wh.second,0});
        durations.insert(durations.end(),{format,wh.first,wh.second,33333333});
        stalls.insert(stalls.end(),{format,wh.first,wh.second,format==33?3000000000LL:0});
    }
    m.set(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,configs.data(),configs.size());
    m.set(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,durations.data(),durations.size());
    m.set(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,stalls.data(),stalls.size());
    Meta req(defaults(front,1));
    auto rk=req.keys();
    m.set(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,rk.data(),rk.size());
    rk.insert(rk.end(),{ANDROID_CONTROL_AE_STATE,ANDROID_CONTROL_AF_STATE,ANDROID_CONTROL_AWB_STATE,
        ANDROID_FLASH_STATE,ANDROID_LENS_STATE,ANDROID_SENSOR_TIMESTAMP,ANDROID_REQUEST_PIPELINE_DEPTH,
        ANDROID_STATISTICS_SCENE_FLICKER});
    m.set(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,rk.data(),rk.size());
    auto ck=m.keys(); ck.push_back(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS);
    m.set(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,ck.data(),ck.size());
    return m.pack();
}
}
