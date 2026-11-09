#include "qti-dpu-output-fence.h"
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <vector>
using sde_drm::DpuOutputFences;
static int mode, fail_add;
static std::vector<std::pair<uint32_t, int32_t *>> requests;
extern "C" drmModeObjectPropertiesPtr drmModeObjectGetProperties(int, uint32_t, uint32_t) {
  auto p = new drmModeObjectProperties{};
  p->count_props = mode == 1 ? 3 : (mode == 2 ? 1 : 2);
  p->props = new uint32_t[p->count_props];
  for (unsigned i=0;i<p->count_props;++i) p->props[i]=i+10;
  return p;
}
extern "C" void drmModeFreeObjectProperties(drmModeObjectPropertiesPtr p) {
  delete[] p->props; delete p;
}
extern "C" drmModePropertyPtr drmModeGetProperty(int, uint32_t id) {
  auto p = new drmModePropertyRes{};
  p->prop_id=id; p->flags=DRM_MODE_PROP_RANGE;
  strcpy(p->name,id==10 ? "OUT_FENCE_PTR" :
                 id==11 ? "QCOM_DPU_RELEASE_FENCE_PTR" : "output_fence");
  return p;
}
extern "C" void drmModeFreeProperty(drmModePropertyPtr p) { delete p; }
extern "C" drmModeConnectorPtr drmModeGetConnector(int, uint32_t) {
  auto p = new drmModeConnector{};
  p->connector_type = mode==3 ? DRM_MODE_CONNECTOR_WRITEBACK : DRM_MODE_CONNECTOR_eDP;
  return p;
}
extern "C" void drmModeFreeConnector(drmModeConnectorPtr p) { delete p; }
extern "C" int drmModeAtomicAddProperty(drmModeAtomicReq *, uint32_t id, uint32_t prop,
                                        uint64_t ptr) {
  assert(id==7);
  if (fail_add) return -ENOMEM;
  requests.emplace_back(prop,reinterpret_cast<int32_t *>(ptr));
  return int(requests.size()-1);
}
static int newfd() { int fd=open("/dev/null",O_RDONLY|O_CLOEXEC); assert(fd>=0); return fd; }
static bool closed(int fd) { errno=0; return fcntl(fd,F_GETFD)==-1 && errno==EBADF; }
int main() {
  DpuOutputFences fences;
  assert(fences.Init(-1,7,8)==0 && fences.Enabled());
  assert(fences.Init(-1,7,8)==-EINVAL);
  int64_t release=-1,retire=-1;
  assert(fences.RequestRelease(7,&release)==0);
  assert(fences.RequestRetire(8,&retire)==0);
  assert(fences.SetOffset(7,0)==0 && fences.SetOffset(7,1)==-EOPNOTSUPP);
  assert(fences.SetOffset(9,0)==-EINVAL);
  assert(fences.Prepare(nullptr)==0 && requests.size()==2);
  assert(requests[0].first==11 && requests[1].first==10);
  int a=newfd(),b=newfd();
  *requests[0].second=a; *requests[1].second=b;
  assert(fences.Complete(true)==0);
  assert(release==a && retire==b && release>=0 && retire>=0);
  assert(!closed(a)&&!closed(b)); close(a);close(b);

  requests.clear();
  release=retire=-1;
  assert(fences.RequestRelease(7,&release)==0);
  assert(fences.RequestRetire(8,&retire)==0);
  assert(fences.Prepare(nullptr)==0);
  a=newfd();b=newfd();
  *requests[0].second=a;*requests[1].second=b;
  assert(fences.Complete(false)==0);
  assert(release==-1 && retire==-1 && closed(a)&&closed(b));

  requests.clear();
  assert(fences.RequestRelease(7,&release)==0);
  assert(fences.RequestRetire(8,&retire)==0);
  assert(fences.Prepare(nullptr)==0);
  a=newfd();*requests[0].second=a;
  assert(fences.Complete(true)==-EIO && closed(a));
  assert(release==-1&&retire==-1);

  assert(fences.RequestRelease(7,nullptr)==-EINVAL);
  assert(fences.RequestRelease(9,&release)==-EINVAL);
  assert(fences.RequestRetire(9,&retire)==-EOPNOTSUPP);
  assert(fences.RequestRelease(7,&release)==0);
  assert(fences.RequestRetire(8,&release)==-EOPNOTSUPP);
  int64_t other=-1;
  assert(fences.RequestRelease(7,&other)==-EALREADY);
  fail_add=1;
  assert(fences.Prepare(nullptr)==-ENOMEM);
  fences.Reset();fail_add=0;
  requests.clear();
  assert(fences.Prepare(nullptr)==0 && requests.empty());

  mode=1; DpuOutputFences legacy;
  assert(legacy.Init(-1,7,8)==0 && !legacy.Enabled());
  mode=2; DpuOutputFences norelease;
  assert(norelease.Init(-1,7,8)==0 && norelease.Enabled());
  assert(norelease.RequestRelease(7,&release)==-EOPNOTSUPP);
  assert(norelease.RequestRetire(8,&retire)==0);
  norelease.Reset();
  mode=3; DpuOutputFences writeback;
  assert(writeback.Init(-1,7,8)==-EOPNOTSUPP);
  mode=0;
  {
    DpuOutputFences abandoned;assert(abandoned.Init(-1,7,8)==0);
    { int64_t temporary=-1;assert(abandoned.RequestRelease(7,&temporary)==0); }
    // Abandoned callers need not keep their output pointers alive until destruction.
  }
}
