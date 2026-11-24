"""Run patched QTI SDM RGB + NV12 DPU overlay composition under ASan and UBSan.

Applies patches/sm8350-dpu-rgb-overlay.patch cleanly (--fuzz=0) to a staged QTI
display source tree (from QTI_DISPLAY_BASE or sdm-work/staged), extracts the real
patched Strategy, ResourceDefault, CompManager, DisplayBase, and DpuColorSupported
implementations, and exercises Tier 1 (full-stack <= 4 layers), Tier 2 (NV12
underlay), Tier 3 (single highest-z large opaque RGB underlay + cached GPU_TARGET),
pipe-allocation order (VIG-first for YUV/scaled, DMA-first for 1:1 RGB), and
hardware TEST_ONLY retry/cleanup guardrails under ASan + UBSan.
"""
from pathlib import Path
import hashlib
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "patches/sm8350-dpu-rgb-overlay.patch"
DEFAULT_BASE = ROOT.parent / "sdm-work/staged"

HARNESS = r"""// Only platform and DRM boundaries are stubs. Algorithm bodies are extracted from real sources.
#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <vector>
using namespace std;
#define DLOGE(...) ((void)0)
#define DLOGW(...) ((void)0)
#define DLOGV_IF(...) ((void)0)
#define DLOGI_IF(...) ((void)0)
#define DLOGE_IF(...) ((void)0)
#define DTRACE_SCOPED() ((void)0)
#define SCOPE_LOCK(...) ((void)0)
#define FLOAT(x) float(x)
#define UINT32(x) uint32_t(x)
#define UINT8(x) uint8_t(x)
#define ROUND_UP_ALIGN_DOWN(x,a) floorf(ceilf(x) / (a)) * (a)
#define Log(...) ((void)0)
using Handle = void*;
enum DisplayError { kErrorNone, kErrorParameters, kErrorNotSupported, kErrorResources,
 kErrorUndefined, kErrorPermission, kErrorHardware, kErrorShutDown };
enum DisplayType { kBuiltIn, kPluggable };
enum LayerComposition { kCompositionGPU, kCompositionGPUTarget, kCompositionSDE };
enum LayerBlending { kBlendingOpaque, kBlendingPremultiplied, kBlendingCoverage };
enum LayerBufferFormat { kFormatInvalid, kFormatRGBA8888, kFormatRGBA8888Ubwc,
 kFormatRGBX8888, kFormatRGBX8888Ubwc, kFormatRGB565,
 kFormatYCbCr420SemiPlanarVenus, kFormatYCbCr420SemiPlanarVenusUbwc };
enum HWSubBlockType { kHWVIGPipe, kHWDMAPipe };
enum PipeType { kPipeTypeVIG, kPipeTypeRGB, kPipeTypeDMA, kPipeTypeUnused };
enum HWBlockType { kHWBuiltIn, kHWPluggable, kHWBlockMax };
enum BufferLayout { kLinear, kUBWC };
BufferLayout GetBufferLayout(LayerBufferFormat f) {
 return (f == kFormatRGBA8888Ubwc || f == kFormatRGBX8888Ubwc) ? kUBWC : kLinear;
}
enum { ColorPrimaries_BT709_5 = 1, ColorPrimaries_BT601_6_525 = 4, ColorPrimaries_BT601_6_625 = 5,
 ColorPrimaries_BT2020 = 9, Transfer_sRGB = 2, Transfer_SMPTE_170M = 3, Transfer_ST2084 = 16,
 Range_Full = 1, Range_Limited = 0, MatrixCoEff_Identity = 0, MatrixCoEff_BT601_6_525 = 6,
 MatrixCoEff_BT601_6_625 = 5, MatrixCoEff_BT709_5 = 1 };
struct ColorMetaData {
 int colorPrimaries=ColorPrimaries_BT709_5, transfer=Transfer_sRGB, range=Range_Full,
 matrixCoefficients=MatrixCoEff_Identity;
 struct { bool colorVolumeSEIEnabled=false; } masteringDisplayInfo;
 struct { bool lightLevelSEIEnabled=false; } contentLightLevel;
 bool dynamicMetaDataValid=false;
 struct { bool criEnabled=false; } cRI;
};
namespace sde_drm { @COLOR@ }
struct LayerRect {
 float left=0,top=0,right=0,bottom=0;
 LayerRect()=default;
 LayerRect(float l,float t,float r,float b):left(l),top(t),right(r),bottom(b){}
};
bool IsValid(const LayerRect&r) { return r.left<r.right && r.top<r.bottom; }
void Normalize(int,int,LayerRect*) {}
struct LayerTransform { float rotation=0; bool flip_horizontal=false,flip_vertical=false; };
void TransformHV(const LayerRect&,const LayerRect&r,const LayerTransform&,LayerRect*out) { *out=r; }
void MapRect(const LayerRect&,const LayerRect&,const LayerRect&r,LayerRect*out) { *out=r; }
struct LayerBuffer {
 LayerBufferFormat format=kFormatRGBA8888;
 uint32_t width=2880,height=1920;
 uint64_t buffer_id=1,handle_id=2;
 struct { int fd=3; } planes[3];
 struct { bool secure=false,secure_camera=false,secure_display=false,interlace=false,hdr=false,mask_layer=false; } flags;
 ColorMetaData color_metadata;
 int acquire_fence=11,release_fence=-1;
};
struct Layer {
 LayerBuffer input_buffer;
 LayerComposition composition=kCompositionGPU;
 LayerBlending blending=kBlendingOpaque;
 uint32_t plane_alpha=255;
 LayerRect src_rect{0,0,2880,1920},dst_rect{0,0,2880,1920};
 LayerTransform transform;
 struct { bool skip=false,solid_fill=false,cursor=false,color_transform=false; } flags;
 struct { struct { unsigned request_flags=0; } flags; } request;
};
struct LayerStack {
 vector<Layer*> layers;
 struct { bool secure_present=false,hdr_present=false,mask_present=false,s3d_mode_present=false,fast_path=false; } flags;
};
struct HWResourceInfo {
 bool kernel_managed_dpu=true,is_src_split=true,has_non_scalar_rgb=false,has_decimation=false;
 uint32_t num_vig_pipe=4,num_rgb_pipe=0,num_dma_pipe=4,max_pipe_width=4096,max_scaler_pipe_width=4096;
 uint32_t max_scale_down=4,max_scale_up=4,num_blending_stages=11;
 map<HWSubBlockType,vector<LayerBufferFormat>> supported_formats_map{
  {kHWVIGPipe,{kFormatYCbCr420SemiPlanarVenus,kFormatRGBA8888,kFormatRGBX8888,kFormatRGBA8888Ubwc,kFormatRGBX8888Ubwc}},
  {kHWDMAPipe,{kFormatRGBA8888,kFormatRGBX8888,kFormatRGBA8888Ubwc,kFormatRGBX8888Ubwc}}
 };
};
struct HWPanelInfo { LayerTransform panel_orientation; };
struct HWMixerAttributes { uint32_t width=2880,height=1920,split_left=1440; };
struct HWDisplayAttributes {};
struct DisplayConfigVariableInfo { uint32_t x_pixels=2880,y_pixels=1920; };
struct HWLayersInfo {
 LayerStack *stack=nullptr;
 uint32_t app_layer_count=0,gpu_target_index=0;
 vector<Layer> hw_layers;
 vector<uint32_t> index,roi_index,layer_exts;
 bool fast_path_composition=false,rc_pu_flag_status=false;
};
struct HWPipeInfo {
 bool valid=false;
 uint32_t pipe_id=0,z_order=0;
 uint8_t horizontal_decimation=0,vertical_decimation=0;
 LayerRect src_roi,dst_roi;
};
struct HWLayerConfig { HWPipeInfo left_pipe,right_pipe; };
struct HWLayers { HWLayersInfo info; HWLayerConfig config[16]{}; bitset<8> updates_mask; };
struct StrategyConstraints { bool safe_mode=false; uint32_t max_layers=8; };
struct StrategyExt {
 DisplayError Start(HWLayersInfo*,uint32_t*) { return kErrorNotSupported; }
 DisplayError GetNextStrategy(StrategyConstraints*) { return kErrorNotSupported; }
 bool CanSkipValidate(bool*) { return true; }
 DisplayError Reconfigure(const HWPanelInfo&,const HWResourceInfo&,const HWMixerAttributes&,const DisplayConfigVariableInfo&) {return kErrorNone;}
};
struct PartialUpdate {};
struct Extension {
 void DestroyPartialUpdate(PartialUpdate*) {}
 template<class... T> void CreatePartialUpdate(T&&...) {}
};
class Strategy {
 public:
 bool disable_gpu_comp_=false,extn_start_success_=false,dpu_overlay_attempted_=false;
 Extension *extension_intf_=nullptr;
 PartialUpdate *partial_update_intf_=nullptr;
 int32_t display_id_=0;
 HWDisplayAttributes display_attributes_;
 StrategyExt *strategy_intf_=nullptr;
 HWResourceInfo hw_resource_info_;
 HWPanelInfo hw_panel_info_;
 HWMixerAttributes mixer_attributes_;
 DisplayConfigVariableInfo fb_config_;
 DisplayType display_type_=kBuiltIn;
 HWLayersInfo *hw_layers_info_=nullptr;
 @STRATEGY_DECLS@
};
class ResourceDefault {
 public:
 enum PipeOwner { kPipeOwnerUserMode,kPipeOwnerKernelMode };
 enum { kMaxDecimationDownScaleRatio=16 };
 struct SourcePipe {
  PipeType type=kPipeTypeUnused; PipeOwner owner=kPipeOwnerUserMode;
  uint32_t mdss_pipe_id=0,index=0; HWBlockType hw_block_type=kHWBlockMax; int priority=0;
  void ResetState() { hw_block_type=kHWBlockMax; }
 };
 struct DisplayResourceContext {
  HWDisplayAttributes display_attributes; HWBlockType hw_block_type=kHWBuiltIn;
  uint64_t frame_count=0; HWMixerAttributes mixer_attributes;
 };
 HWResourceInfo hw_res_info_;
 uint32_t num_pipe_=8;
 vector<SourcePipe> src_pipes_;
 ResourceDefault() {
  // SC8180X layout: 4 VIG (101..104) + 4 DMA (201..204)
  src_pipes_={
   {kPipeTypeVIG,kPipeOwnerUserMode,101,0,kHWBlockMax,0},
   {kPipeTypeVIG,kPipeOwnerUserMode,102,1,kHWBlockMax,1},
   {kPipeTypeVIG,kPipeOwnerUserMode,103,2,kHWBlockMax,2},
   {kPipeTypeVIG,kPipeOwnerUserMode,104,3,kHWBlockMax,3},
   {kPipeTypeDMA,kPipeOwnerUserMode,201,4,kHWBlockMax,4},
   {kPipeTypeDMA,kPipeOwnerUserMode,202,5,kHWBlockMax,5},
   {kPipeTypeDMA,kPipeOwnerUserMode,203,6,kHWBlockMax,6},
   {kPipeTypeDMA,kPipeOwnerUserMode,204,7,kHWBlockMax,7}
  };
 }
 DisplayError Start(Handle) { return kErrorNone; }
 DisplayError Stop(Handle,HWLayers*) { return kErrorNone; }
 void ResourceStateLog() {}
 @RESOURCEDEFAULT_DECLS@
};
struct DisplayCompositionContext {
 Handle display_resource_ctx=nullptr;
 Strategy *strategy=nullptr;
 StrategyConstraints constraints;
 uint32_t remaining_strategies=0;
};
class CompManager {
 public:
 ResourceDefault *resource_intf_=nullptr;
 void PrepareStrategyConstraints(Handle,HWLayers*) {}
 void PrePrepare(Handle h,HWLayers*l) {
  auto*c=static_cast<DisplayCompositionContext*>(h);
  assert(c->strategy->Start(&l->info,&c->remaining_strategies)==kErrorNone);
 }
 void PostPrepare(Handle,HWLayers*) {}
 void GenerateROI(Handle,HWLayers*) {}
 void ControlPartialUpdate(Handle,bool) {}
 @COMPMANAGER_DECLS@
};
struct Hardware {
 bool reject_first=false,reject_all=false;
 vector<unsigned> attempts;
 DisplayError Validate(HWLayers*l) {
  attempts.push_back(l->info.hw_layers.size());
  if(reject_all) return kErrorHardware;
  if(reject_first && attempts.size()==1) return kErrorHardware;
  return kErrorNone;
 }
};
struct ColorManager {
 void PrePrepare(HWLayers*){} bool NeedsPartialUpdateDisable(){return false;} void Validate(HWLayers*){}
};
enum { kPowerStateNone,kUpdateResources };
class DisplayBase {
 public:
 recursive_mutex recursive_mutex_;
 bool needs_validate_=true,active_=true,first_cycle_=true,rc_enable_prop_=false,
 rc_panel_feature_init_=false,partial_update_control_=false,disable_pu_one_frame_=false,
 disable_pu_on_dest_scaler_=false,rc_pu_flag_status_=false;
 int pending_power_state_=kPowerStateNone;
 void *rc_core_=nullptr,*pf_factory_=nullptr,*prop_intf_=nullptr;
 ColorManager *color_mgr_=nullptr;
 CompManager *comp_manager_=nullptr;
 Handle display_comp_ctx_=nullptr;
 Hardware *hw_intf_=nullptr;
 HWLayers hw_layers_;
 DisplayError BuildLayerStackStats(LayerStack*s) {
  hw_layers_.info.stack=s;
  hw_layers_.info.app_layer_count=s->layers.size()-1;
  hw_layers_.info.gpu_target_index=s->layers.size()-1;
  return kErrorNone;
 }
 DisplayError SetupRC(){return kErrorNotSupported;}
 void SetRCData(LayerStack*){}
 void DisablePartialUpdateOneFrame(){}
 @DISPLAYBASE_DECLS@
};
@STRATEGY_METHODS@
@RESOURCEDEFAULT_METHODS@
@COMPMANAGER_METHODS@
@DISPLAYBASE_METHODS@

static Layer make_rgb(float l, float t, float r, float b, LayerBufferFormat fmt,
                      LayerBlending blend, uint32_t alpha=255) {
 Layer layer;
 layer.input_buffer.format = fmt;
 layer.input_buffer.width = uint32_t(r - l);
 layer.input_buffer.height = uint32_t(b - t);
 layer.src_rect = {0, 0, r - l, b - t};
 layer.dst_rect = {l, t, r, b};
 layer.blending = blend;
 layer.plane_alpha = alpha;
 return layer;
}

static Layer make_nv12(float l, float t, float r, float b, uint32_t w=320, uint32_t h=240) {
 Layer layer;
 layer.input_buffer.format = kFormatYCbCr420SemiPlanarVenus;
 layer.input_buffer.width = w;
 layer.input_buffer.height = h;
 layer.input_buffer.color_metadata.colorPrimaries = ColorPrimaries_BT601_6_525;
 layer.input_buffer.color_metadata.matrixCoefficients = MatrixCoEff_BT601_6_525;
 layer.input_buffer.color_metadata.transfer = Transfer_SMPTE_170M;
 layer.input_buffer.color_metadata.range = Range_Limited;
 layer.src_rect = {0, 0, float(w), float(h)};
 layer.dst_rect = {l, t, r, b};
 layer.blending = kBlendingOpaque;
 layer.plane_alpha = 255;
 return layer;
}

static Layer make_target(LayerBufferFormat fmt=kFormatRGBA8888Ubwc) {
 Layer t = make_rgb(0, 0, 2880, 1920, fmt, kBlendingPremultiplied, 255);
 t.composition = kCompositionGPUTarget;
 return t;
}

struct Harness {
 Strategy strategy;
 ResourceDefault resources;
 ResourceDefault::DisplayResourceContext ctx;
 StrategyConstraints constraints;
 HWLayers layers;
 LayerStack stack;
 uint32_t attempts = 0;

 void bind(vector<Layer*> app_layers, Layer *target) {
  stack.layers = app_layers;
  stack.layers.push_back(target);
  layers.info.stack = &stack;
  layers.info.app_layer_count = uint32_t(app_layers.size());
  layers.info.gpu_target_index = uint32_t(app_layers.size());
 }
 uint32_t start() {
  assert(strategy.Start(&layers.info, &attempts) == kErrorNone);
  return attempts;
 }
 void next() { assert(strategy.GetNextStrategy(&constraints) == kErrorNone); }
 unsigned claims() const {
  unsigned n = 0;
  for (const auto &p : resources.src_pipes_) n += (p.hw_block_type == kHWBuiltIn);
  return n;
 }
 unsigned claims_of(PipeType type) const {
  unsigned n = 0;
  for (const auto &p : resources.src_pipes_)
   n += (p.hw_block_type == kHWBuiltIn && p.type == type);
  return n;
 }
};

int main() {
 unsigned cases = 0;

 // 1. BaiduMap 8-layer desktop stack -> Tier 3 promotes BaiduMap SurfaceView (z=3), NOT Wallpaper (z=0)!
 {
  Harness h;
  Layer wallpaper = make_rgb(0, 0, 2880, 1920, kFormatRGBA8888, kBlendingOpaque);
  Layer launcher  = make_rgb(0, 0, 2880, 1920, kFormatRGBA8888, kBlendingPremultiplied);
  Layer task_bg   = make_rgb(0, 0, 2880, 1920, kFormatRGBA8888, kBlendingPremultiplied);
  task_bg.flags.skip = true; // window shadow forced client
  Layer baidu_sv  = make_rgb(0, 80, 2073, 1387, kFormatRGBA8888Ubwc, kBlendingOpaque);
  Layer baidu_vri = make_rgb(0, 80, 2073, 1387, kFormatRGBA8888Ubwc, kBlendingPremultiplied);
  Layer caption   = make_rgb(0, 68, 2073, 143, kFormatRGBA8888, kBlendingPremultiplied);
  Layer statusbar = make_rgb(0, 0, 2880, 68, kFormatRGBA8888, kBlendingPremultiplied);
  Layer taskbar   = make_rgb(0, 1701, 2880, 1920, kFormatRGBA8888, kBlendingPremultiplied);
  Layer target    = make_target();
  h.bind({&wallpaper, &launcher, &task_bg, &baidu_sv, &baidu_vri, &caption, &statusbar, &taskbar}, &target);
  assert(h.start() == 2);
  h.next();
  assert(h.layers.info.index == vector<uint32_t>({3, 8}));
  assert(baidu_sv.composition == kCompositionSDE);
  assert(wallpaper.composition == kCompositionGPU);
  assert(h.resources.Prepare(&h.ctx, &h.layers) == kErrorNone);
  // Both 1:1 RGB layers claim DMA pipes (leaving all 4 VIG + 2 DMA free!)
  assert(h.claims() == 2 && h.claims_of(kPipeTypeDMA) == 2 && h.claims_of(kPipeTypeVIG) == 0);
  assert(h.layers.config[0].left_pipe.z_order == 0 && h.layers.config[1].left_pipe.z_order == 1);
  ++cases;
 }

 // 2. Antigravity / Planner 8-layer desktop stack -> promotes active opaque VRI (z=1) over Wallpaper (z=0)
 {
  Harness h;
  Layer cs1  = make_rgb(0, 0, 2880, 1920, kFormatRGBA8888, kBlendingOpaque);
  Layer app  = make_rgb(0, 68, 2880, 1815, kFormatRGBX8888Ubwc, kBlendingOpaque);
  Layer l2   = make_rgb(0, 0, 2880, 1920, kFormatRGBA8888, kBlendingOpaque, 0);
  Layer l3   = make_rgb(0, 0, 2880, 1920, kFormatRGBA8888, kBlendingPremultiplied, 0); l3.flags.skip = true;
  Layer cs2  = make_rgb(0, 0, 2880, 1920, kFormatRGBA8888, kBlendingPremultiplied);
  Layer l5   = make_rgb(0, 68, 2880, 1920, kFormatRGBA8888, kBlendingPremultiplied, 0);
  Layer l6   = make_rgb(0, 0, 2880, 68, kFormatRGBA8888, kBlendingPremultiplied, 0);
  Layer l7   = make_rgb(0, 1701, 2880, 1920, kFormatRGBA8888, kBlendingPremultiplied, 0);
  Layer target = make_target();
  h.bind({&cs1, &app, &l2, &l3, &cs2, &l5, &l6, &l7}, &target);
  assert(h.start() == 2);
  h.next();
  assert(h.layers.info.index == vector<uint32_t>({1, 8}));
  assert(app.composition == kCompositionSDE);
  assert(h.resources.Prepare(&h.ctx, &h.layers) == kErrorNone);
  assert(h.claims() == 2 && h.claims_of(kPipeTypeDMA) == 2);
  ++cases;
 }

 // 3. Tier 1 Full-stack 4-layer fullscreen BaiduMap -> ALL 4 SDE, ZERO GPU_TARGET!
 {
  Harness h;
  Layer sv = make_rgb(0, 68, 2880, 1740, kFormatRGBA8888Ubwc, kBlendingOpaque);
  Layer vri = make_rgb(0, 68, 2880, 1740, kFormatRGBA8888Ubwc, kBlendingPremultiplied);
  Layer sb = make_rgb(0, 0, 2880, 68, kFormatRGBA8888, kBlendingPremultiplied, 220);
  Layer tb = make_rgb(0, 1740, 2880, 1920, kFormatRGBA8888, kBlendingCoverage, 255);
  Layer target = make_target();
  h.bind({&sv, &vri, &sb, &tb}, &target);
  assert(h.start() == 2);
  h.next();
  assert(h.layers.info.index == vector<uint32_t>({0, 1, 2, 3}));
  assert(h.layers.info.hw_layers.size() == 4);
  for (uint32_t i = 0; i < 4; ++i)
   assert(h.layers.info.hw_layers[i].composition == kCompositionSDE);
  assert(h.resources.Prepare(&h.ctx, &h.layers) == kErrorNone);
  assert(h.claims() == 4 && h.claims_of(kPipeTypeDMA) == 4 && h.claims_of(kPipeTypeVIG) == 0);
  for (uint32_t i = 0; i < 4; ++i) assert(h.layers.config[i].left_pipe.z_order == i);
  ++cases;
 }

 // 4. Tier 1 Full-stack mixed NV12 video + premul UI + StatusBar -> VIG for NV12, DMA for RGB, ZERO GPU_TARGET
 {
  Harness h;
  Layer vid = make_nv12(0, 0, 2880, 1920, 1920, 1080); // scaled NV12
  Layer hud = make_rgb(0, 0, 2880, 1920, kFormatRGBA8888Ubwc, kBlendingPremultiplied);
  Layer sb  = make_rgb(0, 0, 2880, 68, kFormatRGBA8888, kBlendingPremultiplied);
  Layer target = make_target();
  h.bind({&vid, &hud, &sb}, &target);
  assert(h.start() == 2);
  h.next();
  assert(h.layers.info.index == vector<uint32_t>({0, 1, 2}));
  assert(h.resources.Prepare(&h.ctx, &h.layers) == kErrorNone);
  assert(h.claims() == 3 && h.claims_of(kPipeTypeVIG) == 1 && h.claims_of(kPipeTypeDMA) == 2);
  assert(h.layers.config[0].left_pipe.pipe_id == 101 && h.layers.config[0].left_pipe.z_order == 0);
  assert(h.layers.config[1].left_pipe.pipe_id == 201 && h.layers.config[1].left_pipe.z_order == 1);
  assert(h.layers.config[2].left_pipe.pipe_id == 202 && h.layers.config[2].left_pipe.z_order == 2);
  ++cases;
 }

 // 5. Pipe-fragmentation stress: 2 DMA + 1 VIG, 2 unscaled RGB (z=0,1) + 1 scaled RGB (z=2)
 // Pass 0 must claim VIG for scaled z=2 BEFORE Pass 1 claims DMA for z=0,1!
 {
  Harness h;
  h.strategy.hw_resource_info_.num_vig_pipe = h.resources.hw_res_info_.num_vig_pipe = 1;
  h.strategy.hw_resource_info_.num_dma_pipe = h.resources.hw_res_info_.num_dma_pipe = 2;
  h.resources.num_pipe_ = 3;
  h.resources.src_pipes_ = {
   {kPipeTypeVIG, ResourceDefault::kPipeOwnerUserMode, 101, 0, kHWBlockMax, 0},
   {kPipeTypeDMA, ResourceDefault::kPipeOwnerUserMode, 201, 1, kHWBlockMax, 1},
   {kPipeTypeDMA, ResourceDefault::kPipeOwnerUserMode, 202, 2, kHWBlockMax, 2},
  };
  Layer a = make_rgb(0, 0, 1000, 1000, kFormatRGBA8888, kBlendingOpaque);
  Layer b = make_rgb(0, 0, 1000, 1000, kFormatRGBA8888, kBlendingPremultiplied);
  Layer c = make_rgb(0, 0, 2000, 1000, kFormatRGBA8888, kBlendingPremultiplied);
  c.input_buffer.width = 1000; c.input_buffer.height = 500; c.src_rect = {0, 0, 1000, 500}; // 2x scaled
  Layer target = make_target();
  h.bind({&a, &b, &c}, &target);
  assert(h.start() == 2);
  h.next();
  assert(h.layers.info.index == vector<uint32_t>({0, 1, 2}));
  assert(h.resources.Prepare(&h.ctx, &h.layers) == kErrorNone);
  assert(h.layers.config[2].left_pipe.pipe_id == 101 && h.layers.config[2].left_pipe.z_order == 2);
  assert(h.layers.config[0].left_pipe.pipe_id == 201 && h.layers.config[0].left_pipe.z_order == 0);
  assert(h.layers.config[1].left_pipe.pipe_id == 202 && h.layers.config[1].left_pipe.z_order == 1);
  ++cases;
 }

 // 6. Tier 1 4-pipe cap: 3 wide layers needing 2 pipes each (6 > 4) must NOT take >4 pipes; falls to Tier 3
 {
  Harness h;
  h.strategy.hw_resource_info_.max_pipe_width = h.resources.hw_res_info_.max_pipe_width = 2048;
  h.strategy.hw_resource_info_.max_scaler_pipe_width = h.resources.hw_res_info_.max_scaler_pipe_width = 2048;
  Layer l0 = make_rgb(0, 0, 2880, 1920, kFormatRGBA8888, kBlendingOpaque);
  Layer l1 = make_rgb(0, 68, 2880, 1815, kFormatRGBA8888Ubwc, kBlendingOpaque);
  Layer l2 = make_rgb(0, 0, 2880, 68, kFormatRGBA8888, kBlendingPremultiplied);
  Layer target = make_target();
  h.bind({&l0, &l1, &l2}, &target);
  assert(h.start() == 2);
  h.next();
  // Tier 1 rejected (6 pipes > 4 cap); Tier 3 selects highest-z opaque l1 + GPU_TARGET (4 pipes total)
  assert(h.layers.info.index == vector<uint32_t>({1, 3}));
  assert(h.resources.Prepare(&h.ctx, &h.layers) == kErrorNone);
  assert(h.claims() == 4);
  ++cases;
 }

 // 7. Tier 2 NV12 priority over Tier 3 RGB in mixed stack (5 layers: NV12 + large opaque RGB + UI)
 {
  Harness h;
  Layer wp = make_rgb(0, 0, 2880, 1920, kFormatRGBA8888, kBlendingOpaque);
  Layer nv = make_nv12(100, 100, 1700, 1000, 1600, 900);
  Layer ui1 = make_rgb(0, 0, 2880, 1920, kFormatRGBA8888, kBlendingPremultiplied);
  Layer ui2 = make_rgb(0, 0, 2880, 68, kFormatRGBA8888, kBlendingPremultiplied);
  Layer ui3 = make_rgb(0, 1701, 2880, 1920, kFormatRGBA8888, kBlendingPremultiplied);
  Layer target = make_target();
  h.bind({&wp, &nv, &ui1, &ui2, &ui3}, &target);
  assert(h.start() == 2);
  h.next();
  assert(h.layers.info.index == vector<uint32_t>({1, 5}));
  assert(h.resources.Prepare(&h.ctx, &h.layers) == kErrorNone);
  assert(h.claims_of(kPipeTypeVIG) == 1 && h.claims_of(kPipeTypeDMA) == 1);
  ++cases;
 }

 // 8. Tiny opaque layer (<12.5% display) is skipped by Tier 3 in favor of large opaque app underneath
 {
  Harness h;
  Layer app = make_rgb(0, 80, 2073, 1387, kFormatRGBA8888Ubwc, kBlendingOpaque);
  Layer tiny_badge = make_rgb(0, 68, 2880, 143, kFormatRGBA8888, kBlendingOpaque); // 3.9% area
  Layer l2 = make_rgb(0, 0, 2880, 1920, kFormatRGBA8888, kBlendingPremultiplied); l2.flags.skip = true;
  Layer l3 = make_rgb(0, 0, 2880, 68, kFormatRGBA8888, kBlendingPremultiplied);
  Layer l4 = make_rgb(0, 1701, 2880, 1920, kFormatRGBA8888, kBlendingPremultiplied);
  Layer target = make_target();
  h.bind({&app, &tiny_badge, &l2, &l3, &l4}, &target);
  assert(h.start() == 2);
  h.next();
  assert(h.layers.info.index == vector<uint32_t>({0, 5}));
  ++cases;
 }

 // 9. Guardrails: every unsupported condition falls back cleanly to GPU_TARGET
 {
  const vector<function<void(Harness&, Layer&, Layer&)>> guardrails = {
   [](auto&h, auto&, auto&){ h.strategy.display_type_ = kPluggable; },
   [](auto&h, auto&, auto&){ h.strategy.hw_resource_info_.kernel_managed_dpu = false; },
   [](auto&h, auto&, auto&){ static Extension ext; h.strategy.extension_intf_ = &ext; },
   [](auto&h, auto&, auto&){ h.stack.flags.secure_present = true; },
   [](auto&h, auto&, auto&){ h.stack.flags.hdr_present = true; },
   [](auto&h, auto&, auto&){ h.stack.flags.mask_present = true; },
   [](auto&h, auto&, auto&){ h.stack.flags.s3d_mode_present = true; },
   [](auto&, auto&l, auto&){ l.flags.skip = true; },
   [](auto&, auto&l, auto&){ l.flags.solid_fill = true; },
   [](auto&, auto&l, auto&){ l.flags.cursor = true; },
   [](auto&, auto&l, auto&){ l.flags.color_transform = true; },
   [](auto&, auto&l, auto&){ l.transform.rotation = 90; },
   [](auto&, auto&l, auto&){ l.transform.flip_horizontal = true; },
   [](auto&, auto&l, auto&){ l.input_buffer.format = kFormatRGB565; },
   [](auto&, auto&l, auto&){ l.input_buffer.color_metadata.colorPrimaries = 0; }, // UNKNOWN dataspace
   [](auto&, auto&l, auto&){ l.input_buffer.color_metadata.range = Range_Limited; },
   [](auto&, auto&l, auto&){ l.input_buffer.color_metadata.transfer = Transfer_ST2084; },
   [](auto&, auto&l, auto&){ l.dst_rect.right = 2072; }, // 1-pixel downscale
   [](auto&, auto&l, auto&){ l.dst_rect = {0, 0, 200, 200}; l.src_rect = {0, 0, 200, 200}; }, // tiny < 12.5%
   [](auto&, auto&, auto&t){ t.input_buffer.format = kFormatRGB565; },
   [](auto&, auto&, auto&t){ t.blending = kBlendingOpaque; },
  };
  for (auto &mutate : guardrails) {
   Harness h;
   Layer app = make_rgb(0, 80, 2073, 1387, kFormatRGBA8888Ubwc, kBlendingOpaque);
   Layer skip_top = make_rgb(0, 0, 2880, 1920, kFormatRGBA8888, kBlendingPremultiplied);
   skip_top.flags.skip = true;
   Layer target = make_target();
   h.bind({&app, &skip_top}, &target);
   mutate(h, app, target);
   assert(h.start() == 1);
   h.next();
   assert(h.layers.info.index == vector<uint32_t>{2});
   assert(app.composition == kCompositionGPU);
   ++cases;
  }
 }

 // 10. DisplayBase::Prepare real TEST_ONLY hardware rejection -> safe GPU_TARGET fallback & 0 leaked pipes
 for (int mode = 0; mode < 3; ++mode) {
  Harness h;
  Layer sv = make_rgb(0, 80, 2073, 1387, kFormatRGBA8888Ubwc, kBlendingOpaque);
  Layer vri = make_rgb(0, 80, 2073, 1387, kFormatRGBA8888Ubwc, kBlendingPremultiplied);
  if (mode == 2) vri.flags.skip = true; // test both Tier 1 (mode 0,1) and Tier 3 (mode 2) rejection
  Layer target = make_target();
  h.bind({&sv, &vri}, &target);
  DisplayCompositionContext dctx{&h.ctx, &h.strategy, {}, 0};
  dctx.constraints.max_layers = 8;
  CompManager cm; cm.resource_intf_ = &h.resources;
  Hardware hw; hw.reject_first = (mode != 0);
  DisplayBase db; db.comp_manager_ = &cm; db.display_comp_ctx_ = &dctx; db.hw_intf_ = &hw;
  DisplayError got = db.Prepare(&h.stack);
  assert(got == kErrorNone);
  if (mode == 0) {
   assert(hw.attempts == vector<unsigned>{2});
   assert(h.claims() == 2 && sv.composition == kCompositionSDE && vri.composition == kCompositionSDE);
  } else {
   assert(hw.attempts == vector<unsigned>({2, 1}));
   assert(h.claims() == 1 && sv.composition == kCompositionGPU && vri.composition == kCompositionGPU);
  }
  ++cases;
 }

 printf("%u RGB+NV12 DPU overlay scenarios passed (ASan/UBSan).\n", cases);
 return 0;
}
"""


def extract_block(source: str, start: str) -> str:
    begin = source.index(start)
    left = source.index("{", begin)
    depth, end = 1, left + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[begin:end]


class QtiDpuRgbOverlayTest(unittest.TestCase):
    def test_patch_structure(self):
        self.assertTrue(PATCH.is_file(), f"missing {PATCH}")
        text = PATCH.read_text()
        self.assertIn("diff --git a/sdm/libs/core/strategy.cpp b/sdm/libs/core/strategy.cpp", text)
        self.assertIn("diff --git a/sdm/libs/core/resource_default.cpp b/sdm/libs/core/resource_default.cpp", text)
        self.assertIn("kFormatRGBA8888Ubwc", text)
        self.assertIn("kFormatRGBX8888Ubwc", text)

    def test_real_rgb_and_nv12_overlay_under_sanitizers(self):
        base_env = os.environ.get("QTI_DISPLAY_BASE")
        base = Path(base_env) if base_env else DEFAULT_BASE
        if not (base / "sdm/libs/core/strategy.cpp").is_file():
            self.skipTest(f"staged QTI display source not present at {base}")

        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            source = temp / "source"
            shutil.copytree(base / "sdm/libs/core", source / "sdm/libs/core")
            subprocess.run(
                ["patch", "--batch", "--fuzz=0", "-p1", "-i", str(PATCH)],
                cwd=source, check=True, capture_output=True, text=True,
            )

            text = HARNESS
            for cls, filename, names in [
                ("Strategy", "strategy.cpp", ["Start", "GetDpuOverlayIndices", "GetNextStrategy", "CanSkipValidate", "Reconfigure"]),
                ("ResourceDefault", "resource_default.cpp", [
                    "Prepare", "PrepareLayer", "Purge", "SearchPipe", "NextPipe", "GetPipe",
                    "IsScalingNeeded", "SrcSplitConfig", "DisplaySplitConfig", "Config",
                    "CalculateCropRects", "ValidateLayerParams", "ValidateDimensions",
                    "ValidatePipeParams", "ValidateScaling", "ValidateDownScaling",
                    "ValidateUpScaling", "GetScaleFactor", "SetDecimationFactor",
                    "SplitRect", "AlignPipeConfig", "CalculateDecimation",
                ]),
                ("CompManager", "comp_manager.cpp", ["Prepare"]),
                ("DisplayBase", "display_base.cpp", ["Prepare"]),
            ]:
                src = (source / "sdm/libs/core" / filename).read_text()
                methods = []
                for name in names:
                    match = re.search(
                        r"(?:DisplayError|uint32_t|int|bool|void|std::vector<uint32_t>) " + cls + "::" + name + r"\(",
                        src,
                    )
                    self.assertIsNotNone(match, f"{cls}::{name}")
                    methods.append(extract_block(src, match.group()))
                decls = "\n".join(x[:x.index("{")].replace(cls + "::", "") + ";" for x in methods)
                text = text.replace("@" + cls.upper() + "_DECLS@", decls)
                text = text.replace("@" + cls.upper() + "_METHODS@", "\n".join(methods))
                print(cls, hashlib.sha256("\n".join(methods).encode()).hexdigest()[:16])

            color = extract_block(
                (ROOT / "probe/graphics/qti-dpu-sdm-contract.h").read_text(),
                "inline bool DpuColorSupported(",
            )
            text = text.replace("@COLOR@", color)
            cpp, binary = temp / "test.cpp", temp / "test"
            cpp.write_text(text)
            subprocess.run(
                [
                    "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-Wno-misleading-indentation",
                    "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                    str(cpp), "-o", str(binary),
                ],
                check=True,
            )
            run = subprocess.run(
                [str(binary)], capture_output=True, text=True, timeout=20,
                env=dict(
                    os.environ,
                    ASAN_OPTIONS="detect_leaks=1:halt_on_error=1",
                    UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
                ),
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout.strip())


if __name__ == "__main__":
    unittest.main()
