#include "qti-dpu-display-topology.h"
#include <cassert>
#include <cstdio>
using qti_dpu::CountDisplayRoutes;
static int fail_at, allocated;
static uint32_t encoder_ids[]={11,12}, connector_ids[]={21,22};
extern "C" drmModeResPtr drmModeGetResources(int fd){
  assert(fd==42);if(fail_at==1){errno=EIO;return nullptr;}
  ++allocated;auto p=new drmModeRes{};p->count_crtcs=2;p->count_encoders=2;p->count_connectors=2;
  p->encoders=encoder_ids;p->connectors=connector_ids;return p;
}
extern "C" void drmModeFreeResources(drmModeResPtr p){--allocated;delete p;}
extern "C" drmModeEncoderPtr drmModeGetEncoder(int fd,uint32_t id){
  assert(fd==42);if(fail_at==2){errno=ENOMEM;return nullptr;}
  ++allocated;auto p=new drmModeEncoder{};p->encoder_id=id;p->encoder_type=DRM_MODE_ENCODER_TMDS;
  p->possible_crtcs=1U<<(id-11);return p;
}
extern "C" void drmModeFreeEncoder(drmModeEncoderPtr p){--allocated;delete p;}
extern "C" drmModeConnectorPtr drmModeGetConnector(int fd,uint32_t id){
  assert(fd==42);if(fail_at==3){errno=ENODEV;return nullptr;}
  ++allocated;auto p=new drmModeConnector{};p->connector_id=id;
  p->connector_type=id==21 ? DRM_MODE_CONNECTOR_eDP : DRM_MODE_CONNECTOR_DisplayPort;
  p->count_encoders=1;p->encoders=&encoder_ids[id-21];return p;
}
extern "C" void drmModeFreeConnector(drmModeConnectorPtr p){--allocated;delete p;}
int main(){
  std::array<int32_t,4> counts{};
  auto expect=[&](std::array<int32_t,4> expected){assert(counts==expected);};
  assert(qti_dpu::ConnectorKind(DRM_MODE_CONNECTOR_eDP)==0);
  assert(qti_dpu::ConnectorKind(DRM_MODE_CONNECTOR_DSI)==0);
  assert(qti_dpu::ConnectorKind(DRM_MODE_CONNECTOR_HDMIA)==1);
  assert(qti_dpu::ConnectorKind(DRM_MODE_CONNECTOR_WRITEBACK)==2);
  assert(qti_dpu::ConnectorKind(0)==-1);
  assert(CountDisplayRoutes({{0,{11,12}}},{{11,1},{12,2}},2,&counts)==0);expect({1,0,0,1});
  assert(CountDisplayRoutes({{0,{11}},{0,{11}}},{{11,3}},2,&counts)==0);expect({1,0,0,1});
  assert(CountDisplayRoutes({{0,{11}},{1,{12}}},{{11,1},{12,1}},2,&counts)==0);expect({1,1,0,1});
  assert(CountDisplayRoutes({{0,{11}},{1,{12}}},{{11,1},{12,2}},2,&counts)==0);expect({1,1,0,2});
  // An augmenting path must reassign the flexible connector to leave encoder 11 available.
  assert(CountDisplayRoutes({{0,{11,12}},{1,{11}}},{{11,1},{12,2}},2,&counts)==0);expect({1,1,0,2});
  assert(CountDisplayRoutes({{0,{11}},{1,{12}},{2,{13}}},{{11,1},{12,2},{13,4}},3,&counts)==0);expect({1,1,1,3});
  assert(CountDisplayRoutes({{0,{11}}},{{11,0}},0,&counts)==0);expect({0,0,0,0});
  assert(CountDisplayRoutes({{0,{11}}},{{11,0x80000000U}},32,&counts)==0);expect({1,0,0,1});
  assert(CountDisplayRoutes({}, {}, 33, &counts)==-EINVAL);
  assert(CountDisplayRoutes({}, {}, 0, nullptr)==-EINVAL);
  assert(CountDisplayRoutes({}, {{0,1}}, 1, &counts)==-EINVAL);
  assert(CountDisplayRoutes({}, {{11,1},{11,1}}, 1, &counts)==-EINVAL);
  assert(CountDisplayRoutes({}, {{11,2}}, 1, &counts)==-EINVAL);
  assert(CountDisplayRoutes({{3,{11}}}, {{11,1}}, 1, &counts)==-EINVAL);
  assert(CountDisplayRoutes({{0,{12}}}, {{11,1}}, 1, &counts)==-ENOENT);
  assert(qti_dpu::ReadDisplayCounts(42,&counts)==0);expect({1,1,0,2});assert(!allocated);
  for(int at=1;at<=3;++at){
    fail_at=at;assert(qti_dpu::ReadDisplayCounts(42,&counts)==-(at==1 ? EIO : at==2 ? ENOMEM : ENODEV));
    assert(!allocated);
  }
  puts("DPU_CONNECTOR_ENCODER_CRTC_ROUTES_ALL_PASS");
}
