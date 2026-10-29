#pragma once
// Internal interface bring-up; not registered and not an advertised capability list.
#include <SimpleC2Interface.h>
#include <cstdio>
#include <cassert>
class Sq2DecodeInterface final : public C2InterfaceHelper {
 std::shared_ptr<C2ComponentKindSetting> kind;
 std::shared_ptr<C2ComponentDomainSetting> domain;
 std::shared_ptr<C2StreamBufferTypeSetting::input> input;
 std::shared_ptr<C2StreamBufferTypeSetting::output> output;
 std::shared_ptr<C2StreamPictureSizeInfo::output> size;
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
 Sq2DecodeInterface(const std::shared_ptr<C2ReflectorHelper>& reflector,unsigned minw,unsigned maxw,unsigned minh,unsigned maxh):C2InterfaceHelper(reflector){
  setDerivedInstance(this);
  addParameter(DefineParam(kind,C2_PARAMKEY_COMPONENT_KIND).withConstValue(new C2ComponentKindSetting(C2Component::KIND_DECODER)).build());
  addParameter(DefineParam(domain,C2_PARAMKEY_COMPONENT_DOMAIN).withConstValue(new C2ComponentDomainSetting(C2Component::DOMAIN_VIDEO)).build());
  addParameter(DefineParam(input,C2_PARAMKEY_INPUT_STREAM_BUFFER_TYPE).withConstValue(new C2StreamBufferTypeSetting::input(0u,C2BufferData::LINEAR)).build());
  addParameter(DefineParam(output,C2_PARAMKEY_OUTPUT_STREAM_BUFFER_TYPE).withConstValue(new C2StreamBufferTypeSetting::output(0u,C2BufferData::GRAPHIC)).build());
  addParameter(DefineParam(size,C2_PARAMKEY_PICTURE_SIZE).withDefault(new C2StreamPictureSizeInfo::output(0u,minw,minh)).withFields({C2F(size,width).inRange(minw,maxw,2),C2F(size,height).inRange(minh,maxh,2)}).withSetter(setSize).build());
 }
};
