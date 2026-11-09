#include "qti-dpu-capabilities.h"
#include <cassert>
#include <cstdio>
#include <climits>
#include <cmath>
#include <cstdlib>
using namespace sde_drm;
using namespace sde_drm::dpu_caps;

// SSPP_SCALER_VER is extracted from the pinned kernel catalog by the runner.
static_assert(SSPP_SCALER_VER(1, 2) == 65538);
static_assert(SSPP_SCALER_VER(1, 3) == 65539);
static_assert(SSPP_SCALER_VER(1, 4) == 65540);
static_assert(SSPP_SCALER_VER(2, 4) == 131076);
static_assert(SSPP_SCALER_VER(3, 0) == 196608);

static uint32_t property_ids[2] = {10,11};
static uint64_t property_values[2] = {20,21};
static drmModeObjectProperties properties = {};
static drmModePropertyRes property = {};
static drmModePropertyBlobRes blob = {};
static int object_fail, property_fail, blob_fail;
static unsigned object_frees, property_frees, blob_frees;
static std::string blob_text;
static bool read_crtc_fixture;
static std::string crtc_text, plane_text;
static uint32_t plane_ids[] = {7};
static drmModePlaneRes plane_resources = {1, plane_ids};
static unsigned plane_resources_frees;
extern "C" drmModeObjectPropertiesPtr drmModeObjectGetProperties(int,uint32_t,uint32_t type) {
  if(read_crtc_fixture) {
    auto &text = type == DRM_MODE_OBJECT_CRTC ? crtc_text : plane_text;
    blob.data=text.data();blob.length=text.size()+1;
  }
  if(object_fail){errno=object_fail;return nullptr;} return &properties;
}
extern "C" drmModePropertyPtr drmModeGetProperty(int,uint32_t) {
  if(property_fail){errno=property_fail;return nullptr;}return &property;
}
extern "C" void drmModeFreeProperty(drmModePropertyPtr p){if(p)++property_frees;}
extern "C" void drmModeFreeObjectProperties(drmModeObjectPropertiesPtr p){if(p)++object_frees;}
extern "C" drmModePropertyBlobPtr drmModeGetPropertyBlob(int,uint32_t) {
  if(blob_fail)return nullptr;
  return &blob;
}
extern "C" void drmModeFreePropertyBlob(drmModePropertyBlobPtr p){if(p)++blob_frees;}
extern "C" drmModePlaneResPtr drmModeGetPlaneResources(int){return &plane_resources;}
extern "C" void drmModeFreePlaneResources(drmModePlaneResPtr p){
  assert(p==&plane_resources);++plane_resources_frees;
}

static std::string Text(const Fields &fields) {
  std::string s;
  for(const auto &field:fields) s+=field.first+"="+field.second+"\n";
  return s;
}
static Fields PlaneFields() {
  return {{"version","1"},{"object","plane"},{"physical_pipe","1"},{"pipe_id","1"},
    {"pipe_type","vig"},{"scaler","1"},{"csc","1"},{"kernel_managed_scaler","1"},
    {"kernel_managed_csc","1"},{"csc_bt601_limited_only","1"},{"scaler_hw_version",std::to_string(SSPP_SCALER_VER(1,4))},
    {"max_linewidth","2560"},{"max_upscale","20"},{"max_downscale","4"},
    {"min_scale_16_16","3276"},{"max_scale_16_16","262144"},
    {"max_per_pipe_bw_kbps","4500000"}};
}
static Fields CrtcFields() {
  return {{"version","1"},{"object","crtc"},{"hw_major","5"},{"hw_minor","1"},
    {"hw_version","1342242818"},{"max_blendstages","11"},{"has_src_split","1"},
    {"max_core_clk_hz","600000000"},{"max_mixer_width","2560"},
    {"undersized_prefill_lines","2"},{"xtra_prefill_lines","3"},{"dest_scale_prefill_lines","4"},
    {"macrotile_prefill_lines","5"},{"yuv_nv12_prefill_lines","6"},{"linear_prefill_lines","7"},
    {"downscaling_prefill_lines","8"},{"amortizable_threshold","9"},{"min_prefill_lines","10"},
    {"kernel_bw_votes","1"},{"max_bw_low_kbps","9600000"},{"max_bw_high_kbps","12800000"},
    {"ubwc_version","536870912"},{"ubwc_dec_version","805306368"},
    {"clk_inefficiency_pct","105"},{"bw_inefficiency_pct","120"},
    {"min_core_ib","1"},{"min_llcc_ib","2"},{"min_dram_ib","3"}};
}
static void TestText() {
  Fields parsed={{"old","unchanged"}};
  auto s=Text(PlaneFields());
  assert(ParseText(s.data(),s.size(),"plane",&parsed));
  assert(parsed==PlaneFields());
  s.push_back('\0');
  assert(ParseText(s.data(),s.size(),"plane",&parsed));
  auto unchanged=parsed;
  for(const std::string &bad:std::vector<std::string>{
        "", "version=2\nobject=plane\n", "version=1\nobject=crtc\n",
        "version=1\nobject=plane\nversion=1\n","version=1\nobject=plane\n\n",
        "=value\nversion=1\nobject=plane\n","version=1\nobject=plane\nempty=\n",
        std::string("version=1\nobject=plane\n\0junk",27)}) {
    assert(!ParseText(bad.data(),bad.size(),"plane",&parsed));
    assert(parsed==unchanged);
  }
  assert(!ParseText(nullptr,1,"plane",&parsed));
  std::string oversized(8193,'x');
  assert(!ParseText(oversized.data(),oversized.size(),"plane",&parsed));
  assert(ParseText("version=1\nobject=plane",22,"plane",&parsed));
}
static void TestNumber() {
  Fields f;
  uint32_t u32=123; uint64_t u64=123; bool boolean=false;
  for(const char *s:{"","-1","+1"," 1","1 ","0x10","1.0","4294967296"}) {
    f["n"]=s; assert(!Number(f,"n",&u32)); assert(u32==123);
  }
  f["n"]="4294967295";assert(Number(f,"n",&u32)&&u32==UINT32_MAX);
  f["n"]="18446744073709551615";assert(Number(f,"n",&u64)&&u64==UINT64_MAX);
  f["n"]="18446744073709551616";assert(!Number(f,"n",&u64)&&u64==UINT64_MAX);
  f["n"]="1";assert(Number(f,"n",&boolean)&&boolean);
  f["n"]="0";assert(Number(f,"n",&boolean)&&!boolean);
  f["n"]="2";assert(!Number(f,"n",&boolean)&&!boolean);
  assert(!Number(f,"missing",&u32));
}
static std::vector<uint8_t> FormatBlob() {
  drm_format_modifier_blob h={};
  h.version=1;h.count_formats=3;h.formats_offset=sizeof(h);
  h.count_modifiers=2;h.modifiers_offset=sizeof(h)+3*sizeof(uint32_t)+4;
  std::vector<uint8_t> data(h.modifiers_offset+2*sizeof(drm_format_modifier));
  memcpy(data.data(),&h,sizeof(h));
  uint32_t formats[]={0x34325241,0x3231564e,0x30313050};
  memcpy(data.data()+h.formats_offset,formats,sizeof(formats));
  drm_format_modifier mods[2]={};
  mods[0].formats=7;mods[0].modifier=0;
  mods[1].formats=1;mods[1].offset=1;mods[1].modifier=0x0500000000000001ULL;
  memcpy(data.data()+h.modifiers_offset,mods,sizeof(mods));
  return data;
}
static void TestFormats() {
  auto bytes=FormatBlob();
  std::vector<std::pair<uint32_t,uint64_t>> out;
  assert(Formats(bytes.data(),bytes.size(),&out)&&out.size()==4);
  assert(out[0]==std::make_pair(uint32_t(0x34325241),uint64_t(0)));
  assert(out[3]==std::make_pair(uint32_t(0x3231564e),uint64_t(0x0500000000000001ULL)));
  auto good=out;
  assert(!Formats(nullptr,bytes.size(),&out)&&out==good);
  for(size_t n=0;n<bytes.size();++n) assert(!Formats(bytes.data(),n,&out)&&out==good);
  for(unsigned bad=0;bad<9;++bad) {
    auto copy=bytes;drm_format_modifier_blob h;memcpy(&h,copy.data(),sizeof(h));
    if(bad==0)h.version=2;
    if(bad==1)h.flags=1;
    if(bad==2)h.count_formats=0;
    if(bad==3)h.count_modifiers=0;
    if(bad==4)h.count_formats=UINT32_MAX;
    if(bad==5)h.modifiers_offset=UINT32_MAX;
    if(bad==6)h.formats_offset=sizeof(h)-1;
    if(bad==7)h.modifiers_offset=sizeof(h)-1;
    if(bad==8)h.count_modifiers=UINT32_MAX;
    memcpy(copy.data(),&h,sizeof(h));
    assert(!Formats(copy.data(),copy.size(),&out)&&out==good);
  }
  for(unsigned bad=0;bad<3;++bad) {
    auto copy=bytes;drm_format_modifier_blob h;memcpy(&h,copy.data(),sizeof(h));
    drm_format_modifier m;memcpy(&m,copy.data()+h.modifiers_offset,sizeof(m));
    if(bad==0)m.pad=1;
    if(bad==1)m.offset=UINT32_MAX;
    if(bad==2)m.formats=uint64_t(1)<<63;
    memcpy(copy.data()+h.modifiers_offset,&m,sizeof(m));
    assert(!Formats(copy.data(),copy.size(),&out)&&out==good);
  }
  // Distinct arrays must not alias despite both ranges being in bounds.
  drm_format_modifier_blob h={};
  h.version=1;h.count_formats=2;h.formats_offset=sizeof(h);
  h.count_modifiers=1;h.modifiers_offset=sizeof(h);
  std::vector<uint8_t> overlap(sizeof(h)+sizeof(drm_format_modifier));
  memcpy(overlap.data(),&h,sizeof(h));
  drm_format_modifier m={};m.formats=1;
  memcpy(overlap.data()+sizeof(h),&m,sizeof(m));
  assert(!Formats(overlap.data(),overlap.size(),&out)&&out==good);
}
static void TestPlane() {
  auto fields=PlaneFields();DRMPlaneTypeInfo info={};
  assert(Plane(fields,&info)==0&&info.type==DRMPlaneType::VIG);
  assert(info.max_pipe_bandwidth==4500000000ULL && info.max_pipe_bandwidth_high==4500000000ULL);
  assert(info.max_scaler_linewidth==2560 && info.max_upscale==20 && info.max_downscale==4);
  assert(!info.multirect_prop_present&&!info.inverse_pma&&!info.has_excl_rect);
  const std::pair<const char*,const char*> invalid[]={
    {"physical_pipe","0"},{"scaler","2"},{"kernel_managed_scaler","0"},{"csc_bt601_limited_only","0"},
    {"max_linewidth","0"},{"max_upscale","0"},{"max_downscale","0"},
    {"max_per_pipe_bw_kbps","18446744073709552"},{"scaler_hw_version","65535"},
    {"pipe_type","unknown"},{"min_scale_16_16","1"},{"max_scale_16_16","1"}};
  for(auto item:invalid) {
    auto bad=fields;bad[item.first]=item.second;
    info.max_linewidth=777;
    assert(Plane(bad,&info)<0&&info.max_linewidth==777);
  }
  auto missing=fields;missing.erase("max_linewidth");assert(Plane(missing,&info)<0);
  fields["pipe_type"]="dma";
  fields["scaler"]=fields["csc"]=fields["kernel_managed_scaler"]=fields["kernel_managed_csc"]=fields["csc_bt601_limited_only"]="0";
  fields["max_upscale"]=fields["max_downscale"]="1";
  fields["min_scale_16_16"]=fields["max_scale_16_16"]="65536";
  fields["scaler_hw_version"]="0";
  assert(Plane(fields,&info)==0&&info.type==DRMPlaneType::DMA);
  fields["pipe_type"]="cursor";assert(Plane(fields,&info)==0&&info.type==DRMPlaneType::CURSOR);
  fields["max_upscale"]="2";assert(Plane(fields,&info)<0);
}
static void TestScalerVersions() {
  const std::pair<uint32_t,QSEEDStepVersion> supported[] = {
    {SSPP_SCALER_VER(1,2),QSEEDStepVersion::V2},
    {SSPP_SCALER_VER(1,3),QSEEDStepVersion::V3},
    {SSPP_SCALER_VER(1,4),QSEEDStepVersion::V4},
    {SSPP_SCALER_VER(2,4),QSEEDStepVersion::V3LITE_V4},
    {SSPP_SCALER_VER(3,0),QSEEDStepVersion::V3LITE_V5},
  };
  properties.count_props=1;properties.props=property_ids;properties.prop_values=property_values;
  property.prop_id=10;strcpy(property.name,"QCOM_DPU_CAPS");
  property.flags=DRM_MODE_PROP_BLOB|DRM_MODE_PROP_IMMUTABLE;
  crtc_text=Text(CrtcFields());read_crtc_fixture=true;
  unsigned frees=plane_resources_frees;
  for(const auto &item:supported) {
    QSEEDStepVersion step=QSEEDStepVersion::V2;
    assert(Qseed(item.first,&step)&&step==item.second);
    auto fields=PlaneFields();fields["scaler_hw_version"]=std::to_string(item.first);
    DRMPlaneTypeInfo plane={};
    assert(Plane(fields,&plane)==0&&plane.qseed3_version==item.second);
    plane_text=Text(fields);
    DRMCrtcInfo crtc={};
    assert(ReadCrtc(3,4,&crtc)==0);
    const auto family=item.first==SSPP_SCALER_VER(1,2) ||
                      item.first==SSPP_SCALER_VER(1,3) ||
                      item.first==SSPP_SCALER_VER(1,4) ? QSEEDVersion::V3 : QSEEDVersion::V3LITE;
    assert(crtc.qseed_version==family);
    assert(plane_resources_frees==++frees);
  }
  // The old fixtures incorrectly shifted the major version by 12 rather than 16.
  for(uint32_t invalid:{0x1002U,0x1003U,0x1004U,0x2004U,0x3000U,0U,0xffffffffU,
                        SSPP_SCALER_VER(3U,1U)}) {
    QSEEDStepVersion step=QSEEDStepVersion::V4;
    assert(!Qseed(invalid,&step)&&step==QSEEDStepVersion::V4);
    auto fields=PlaneFields();fields["scaler_hw_version"]=std::to_string(invalid);
    DRMPlaneTypeInfo plane={};plane.max_linewidth=777;
    assert(Plane(fields,&plane)==-EOPNOTSUPP&&plane.max_linewidth==777);
    plane_text=Text(fields);
    DRMCrtcInfo crtc={};crtc.max_mixer_width=777;
    assert(ReadCrtc(3,4,&crtc)==-EOPNOTSUPP&&crtc.max_mixer_width==777);
    assert(plane_resources_frees==++frees);
  }
  read_crtc_fixture=false;
}
static void TestCrtc() {
  auto fields=CrtcFields();DRMCrtcInfo info={};
  assert(Crtc(fields,&info)==0&&info.kernel_managed_dpu);
  assert(info.max_bandwidth_low==9600000000ULL&&info.max_bandwidth_high==12800000000ULL);
  assert(info.hw_version==0x50010002&&info.max_mixer_width==2560&&info.max_blend_stages==11);
  assert(std::abs(info.clk_fudge_factor-1.05f)<0.0001f);
  assert(std::abs(info.ib_fudge_factor-1.20f)<0.0001f);
  assert(info.ubwc_version==2&&!info.has_hdr&&!info.has_spr&&info.max_solidfill_stages==0);
  assert(info.secure_disp_blend_stage==-1&&info.dest_scaler_count==0);
  const std::pair<const char*,const char*> invalid[]={
    {"kernel_bw_votes","0"},{"has_src_split","2"},{"hw_version","0"},{"max_blendstages","0"},
    {"max_mixer_width","0"},{"max_core_clk_hz","0"},{"max_core_clk_hz","4294967296"},
    {"max_bw_low_kbps","18446744073709552"},{"max_bw_high_kbps","18446744073709552"}};
  for(auto item:invalid) {
    auto bad=fields;bad[item.first]=item.second;
    info.max_mixer_width=777;assert(Crtc(bad,&info)<0&&info.max_mixer_width==777);
  }
  fields.erase("min_prefill_lines");assert(Crtc(fields,&info)<0);
}
static void TestBlob() {
  properties.count_props=1;properties.props=property_ids;properties.prop_values=property_values;
  property.prop_id=10;strcpy(property.name,"QCOM_DPU_CAPS");
  property.flags=DRM_MODE_PROP_BLOB|DRM_MODE_PROP_IMMUTABLE;
  blob_text=Text(PlaneFields());blob.data=blob_text.data();blob.length=blob_text.size()+1;
  std::vector<uint8_t> out;
  assert(Blob(3,4,DRM_MODE_OBJECT_PLANE,"QCOM_DPU_CAPS",&out)==0);
  assert(std::string(reinterpret_cast<const char*>(out.data()))==blob_text);
  auto original=out;
  assert(object_frees==1&&property_frees==1&&blob_frees==1);
  property.flags=DRM_MODE_PROP_BLOB;assert(Blob(3,4,0,"QCOM_DPU_CAPS",&out)==-EINVAL);
  property.flags=DRM_MODE_PROP_IMMUTABLE;assert(Blob(3,4,0,"QCOM_DPU_CAPS",&out)==-EINVAL);
  property.flags=DRM_MODE_PROP_BLOB|DRM_MODE_PROP_IMMUTABLE;
  property_values[0]=0;assert(Blob(3,4,0,"QCOM_DPU_CAPS",&out)==-EINVAL);
  property_values[0]=uint64_t(UINT32_MAX)+1;assert(Blob(3,4,0,"QCOM_DPU_CAPS",&out)==-EINVAL);
  property_values[0]=20;
  blob.length=1024*1024+1;assert(Blob(3,4,0,"QCOM_DPU_CAPS",&out)==-EINVAL);
  blob.length=0;assert(Blob(3,4,0,"QCOM_DPU_CAPS",&out)==-EINVAL);
  blob.length=1;blob.data=nullptr;assert(Blob(3,4,0,"QCOM_DPU_CAPS",&out)==-EINVAL);
  blob.data=blob_text.data();blob_fail=1;assert(Blob(3,4,0,"QCOM_DPU_CAPS",&out)==-EINVAL);blob_fail=0;
  property_fail=EIO;assert(Blob(3,4,0,"QCOM_DPU_CAPS",&out)==-EIO);property_fail=0;
  object_fail=ENOMEM;assert(Blob(3,4,0,"QCOM_DPU_CAPS",&out)==-ENOMEM);object_fail=0;
  properties.count_props=0;assert(Blob(3,4,0,"QCOM_DPU_CAPS",&out)==-ENOENT);
  assert(out==original);
}
static void TestActualEmitter() {
  const char *plane=std::getenv("DPU_CAPS_PLANE");
  const char *crtc=std::getenv("DPU_CAPS_CRTC");
  if(!plane&&!crtc)return;
  assert(plane&&crtc);
  Fields fields;DRMPlaneTypeInfo p={};DRMCrtcInfo c={};
  assert(ParseText(plane,strlen(plane),"plane",&fields));
  assert(Plane(fields,&p)==0&&p.qseed3_version==QSEEDStepVersion::V4);
  assert(p.max_linewidth==2671&&p.max_pipe_bandwidth==456789000ULL&&p.max_upscale==20);
  assert(ParseText(crtc,strlen(crtc),"crtc",&fields));
  assert(Crtc(fields,&c)==0);
  assert(c.max_mixer_width==2049&&c.max_sde_clk==600000001);
  assert(c.max_bandwidth_low==123456000ULL&&c.max_bandwidth_high==234567000ULL);
  assert(c.hw_version==0x50010002&&c.ubwc_version==2);
}
int main() {
  TestActualEmitter();
  TestText();TestNumber();TestFormats();TestPlane();TestCrtc();TestBlob();TestScalerVersions();
  puts("QTI_DPU_CAPS_PASS text/numbers/formats/plane/crtc/blob-failures/scaler-catalog-encoding");
}
