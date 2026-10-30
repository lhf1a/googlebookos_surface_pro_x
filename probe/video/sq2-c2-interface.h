#pragma once
// Internal interface bring-up; not registered and not an advertised capability list.
#include <SimpleC2Interface.h>
#include <cstdio>
#include <cassert>
#include <android/log.h>
#include "sq2-codecs.h"
class Sq2DecodeInterface final : public C2InterfaceHelper {
 std::shared_ptr<C2ComponentKindSetting> kind;
 std::shared_ptr<C2ComponentDomainSetting> domain;
 std::shared_ptr<C2StreamBufferTypeSetting::input> input;
 std::shared_ptr<C2StreamBufferTypeSetting::output> output;
 std::shared_ptr<C2StreamPictureSizeInfo::output> size;
 std::shared_ptr<C2PortBlockPoolsTuning::output> pools;
 std::shared_ptr<C2StreamProfileLevelInfo::input> profileLevel;
 std::shared_ptr<C2StreamPixelFormatInfo::output> pixelFormat;
 std::shared_ptr<C2PortMediaTypeSetting::input> inputMime;
 std::shared_ptr<C2PortMediaTypeSetting::output> outputMime;
 std::shared_ptr<C2PortActualDelayTuning::output> outputDelay;
 static C2R setProfileLevel(bool,const C2P<C2StreamProfileLevelInfo::input>& old,C2P<C2StreamProfileLevelInfo::input>& v){
  __android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","PROFILE_CONFIG profile=%u level=%u",unsigned(v.v.profile),unsigned(v.v.level));
  if(!v.F(v.v.profile).supportsAtAll(v.v.profile)||!v.F(v.v.level).supportsAtAll(v.v.level)){
   auto error=C2R(C2SettingResultBuilder::BadValue(v.F(v.v.profile)));
   v.set().profile=old.v.profile;v.set().level=old.v.level;return error;
  }
  return C2R::Ok();
 }
 static C2R calculatePixel(bool,C2P<C2StreamPixelFormatInfo::output>& v,const C2P<C2StreamProfileLevelInfo::input>& profile){
  v.set().value=profile.v.profile==C2Config::PROFILE_HEVC_MAIN_10?0x36:0x23;
  return C2R::Ok();
 }
 static C2R setPools(bool,C2P<C2PortBlockPoolsTuning::output>&){return C2R::Ok();}
 static C2R setSize(bool,const C2P<C2StreamPictureSizeInfo::output>& old,C2P<C2StreamPictureSizeInfo::output>& v){
  C2R result=C2R::Ok();bool failed=false;
  if(!v.F(v.v.width).supportsAtAll(v.v.width)){
   result=C2R(C2SettingResultBuilder::BadValue(v.F(v.v.width)));failed=true;
   v.set().width=old.v.width;
  }
  if(!v.F(v.v.height).supportsAtAll(v.v.height)){
   auto error=C2R(C2SettingResultBuilder::BadValue(v.F(v.v.height)));
   result=failed?result.plus(std::move(error)):std::move(error);
   v.set().height=old.v.height;
  }
  return result;
 }
public:
 Sq2DecodeInterface(const std::shared_ptr<C2ReflectorHelper>& reflector,unsigned minw,unsigned maxw,unsigned minh,unsigned maxh,Sq2Codec codec=Sq2Codec::Avc):C2InterfaceHelper(reflector){
  setDerivedInstance(this);
  addParameter(DefineParam(outputDelay,C2_PARAMKEY_OUTPUT_DELAY)
   .withConstValue(new C2PortActualDelayTuning::output(8u)).build());
  addParameter(DefineParam(inputMime,C2_PARAMKEY_INPUT_MEDIA_TYPE)
   .withConstValue(android::AllocSharedString<C2PortMediaTypeSetting::input>(sq2CodecInfo(codec).mime)).build());
  addParameter(DefineParam(outputMime,C2_PARAMKEY_OUTPUT_MEDIA_TYPE)
   .withConstValue(android::AllocSharedString<C2PortMediaTypeSetting::output>("video/raw")).build());
  // Limit advertisement to the profile exercised by the lab's decoded vectors.
  if(codec==Sq2Codec::Vp8){
   addParameter(DefineParam(profileLevel,C2_PARAMKEY_PROFILE_LEVEL)
    .withDefault(new C2StreamProfileLevelInfo::input(0u,C2Config::PROFILE_VP8_0,C2Config::LEVEL_UNUSED))
    .withFields({C2F(profileLevel,profile).oneOf({C2Config::PROFILE_VP8_0}),
     C2F(profileLevel,level).oneOf({C2Config::LEVEL_UNUSED})}).withSetter(setProfileLevel).build());
  }else if(codec==Sq2Codec::Vp9){
   addParameter(DefineParam(profileLevel,C2_PARAMKEY_PROFILE_LEVEL)
    .withDefault(new C2StreamProfileLevelInfo::input(0u,C2Config::PROFILE_VP9_0,C2Config::LEVEL_VP9_5_1))
    .withFields({C2F(profileLevel,profile).oneOf({C2Config::PROFILE_VP9_0}),
     C2F(profileLevel,level).oneOf({C2Config::LEVEL_VP9_1,C2Config::LEVEL_VP9_1_1,
      C2Config::LEVEL_VP9_2,C2Config::LEVEL_VP9_2_1,C2Config::LEVEL_VP9_3,C2Config::LEVEL_VP9_3_1,
      C2Config::LEVEL_VP9_4,C2Config::LEVEL_VP9_4_1,C2Config::LEVEL_VP9_5,C2Config::LEVEL_VP9_5_1})})
    .withSetter(setProfileLevel).build());
  }else if(codec==Sq2Codec::Hevc){
   addParameter(DefineParam(profileLevel,C2_PARAMKEY_PROFILE_LEVEL)
    .withDefault(new C2StreamProfileLevelInfo::input(0u,C2Config::PROFILE_HEVC_MAIN,C2Config::LEVEL_HEVC_MAIN_5_1))
    .withFields({C2F(profileLevel,profile).oneOf({C2Config::PROFILE_HEVC_MAIN,C2Config::PROFILE_HEVC_MAIN_10}),
     C2F(profileLevel,level).oneOf({C2Config::LEVEL_HEVC_MAIN_1,C2Config::LEVEL_HEVC_MAIN_2,
      C2Config::LEVEL_HEVC_MAIN_2_1,C2Config::LEVEL_HEVC_MAIN_3,C2Config::LEVEL_HEVC_MAIN_3_1,
      C2Config::LEVEL_HEVC_MAIN_4,C2Config::LEVEL_HEVC_MAIN_4_1,C2Config::LEVEL_HEVC_MAIN_5,
      C2Config::LEVEL_HEVC_MAIN_5_1})}).withSetter(setProfileLevel).build());
  }else addParameter(DefineParam(profileLevel,C2_PARAMKEY_PROFILE_LEVEL)
   .withDefault(new C2StreamProfileLevelInfo::input(0u,C2Config::PROFILE_AVC_CONSTRAINED_BASELINE,C2Config::LEVEL_AVC_5_2))
   .withFields({C2F(profileLevel,profile).oneOf({C2Config::PROFILE_AVC_CONSTRAINED_BASELINE,
    C2Config::PROFILE_AVC_MAIN,C2Config::PROFILE_AVC_HIGH}),
    C2F(profileLevel,level).oneOf({C2Config::LEVEL_AVC_1,C2Config::LEVEL_AVC_1B,
     C2Config::LEVEL_AVC_1_1,C2Config::LEVEL_AVC_1_2,C2Config::LEVEL_AVC_1_3,
     C2Config::LEVEL_AVC_2,C2Config::LEVEL_AVC_2_1,C2Config::LEVEL_AVC_2_2,
     C2Config::LEVEL_AVC_3,C2Config::LEVEL_AVC_3_1,C2Config::LEVEL_AVC_3_2,
     C2Config::LEVEL_AVC_4,C2Config::LEVEL_AVC_4_1,C2Config::LEVEL_AVC_4_2,
     C2Config::LEVEL_AVC_5,C2Config::LEVEL_AVC_5_1,C2Config::LEVEL_AVC_5_2})})
   .withSetter(setProfileLevel).build());
  if(codec==Sq2Codec::Hevc){
   addParameter(DefineParam(pixelFormat,C2_PARAMKEY_PIXEL_FORMAT)
    .withDefault(new C2StreamPixelFormatInfo::output(0u,0x23))
    .withFields({C2F(pixelFormat,value).oneOf({0x23u,0x36u})})
    .calculatedAs(calculatePixel,profileLevel).build());
  }else addParameter(DefineParam(pixelFormat,C2_PARAMKEY_PIXEL_FORMAT)
   .withConstValue(new C2StreamPixelFormatInfo::output(0u,0x23)).build());
  addParameter(DefineParam(kind,C2_PARAMKEY_COMPONENT_KIND).withConstValue(new C2ComponentKindSetting(C2Component::KIND_DECODER)).build());
  addParameter(DefineParam(domain,C2_PARAMKEY_COMPONENT_DOMAIN).withConstValue(new C2ComponentDomainSetting(C2Component::DOMAIN_VIDEO)).build());
  addParameter(DefineParam(input,C2_PARAMKEY_INPUT_STREAM_BUFFER_TYPE).withConstValue(new C2StreamBufferTypeSetting::input(0u,C2BufferData::LINEAR)).build());
  addParameter(DefineParam(output,C2_PARAMKEY_OUTPUT_STREAM_BUFFER_TYPE).withConstValue(new C2StreamBufferTypeSetting::output(0u,C2BufferData::GRAPHIC)).build());
  addParameter(DefineParam(size,C2_PARAMKEY_PICTURE_SIZE).withDefault(new C2StreamPictureSizeInfo::output(0u,minw,minh)).withFields({C2F(size,width).inRange(minw,maxw,2),C2F(size,height).inRange(minh,maxh,2)}).withSetter(setSize).build());
  C2BlockPool::local_id_t defaults[1]={C2BlockPool::BASIC_GRAPHIC};
  addParameter(DefineParam(pools,C2_PARAMKEY_OUTPUT_BLOCK_POOLS)
   .withDefault(C2PortBlockPoolsTuning::output::AllocShared(defaults))
   .withFields({C2F(pools,m.values[0]).any(),C2F(pools,m.values).inRange(0,1)})
   .withSetter(setPools).build());
 }
};
