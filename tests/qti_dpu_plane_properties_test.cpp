#include "qti-dpu-plane-properties.h"
#include <assert.h>
#include <limits.h>
#include <initializer_list>
#include <unistd.h>
using sde_drm::DpuPlaneProperties;
static uint32_t last_id, last_plane;
static uint64_t last_value;
static int add_result, additions, fail_at;
static uint64_t encoding_value;
extern "C" int drmModeAtomicAddProperty(drmModeAtomicReq *, uint32_t plane,
                                        uint32_t property, uint64_t value) {
  last_id = property; last_plane = plane; last_value = value; ++additions;
  if(property==31)encoding_value=value;
  return fail_at==additions ? -EIO : add_result;
}
static drmModePropertyRes property(const char *name, uint32_t id, uint32_t flags) {
  drmModePropertyRes p = {};
  strcpy(p.name, name); p.prop_id = id; p.flags = flags;
  return p;
}
int main() {
  // Standard enum values are discovered, not assumed to be 0/1.
  drm_mode_property_enum encoding_enums[2] = {};
  strcpy(encoding_enums[0].name, "ITU-R BT.709 YCbCr");encoding_enums[0].value=73;
  strcpy(encoding_enums[1].name, "ITU-R BT.601 YCbCr");encoding_enums[1].value=29;
  auto encoding=property("COLOR_ENCODING",31,DRM_MODE_PROP_ENUM);
  encoding.count_enums=2;encoding.enums=encoding_enums;
  drm_mode_property_enum range_enum={};strcpy(range_enum.name,"YCbCr limited range");range_enum.value=47;
  auto range=property("COLOR_RANGE",32,DRM_MODE_PROP_ENUM);range.count_enums=1;range.enums=&range_enum;
  DpuPlaneProperties color;
  assert(color.Color(nullptr,7,true)==-EOPNOTSUPP);
  color.Observe(encoding);assert(color.Color(nullptr,7,true)==-EOPNOTSUPP);
  color.Observe(range);
  assert(color.Color(nullptr,7,true)==0 && last_id==32 && last_value==47 && encoding_value==73);
  assert(color.Color(nullptr,7,false)==0 && last_id==32 && last_value==47 && encoding_value==29);
  fail_at=additions+2;
  assert(color.Color(nullptr,7,true)==-EIO && last_id==32);
  fail_at=0;
  // A failed first addition must not attempt the range property.
  add_result=-ENOMEM;int color_before=additions;
  assert(color.Color(nullptr,7,false)==-ENOMEM && additions==color_before+1 && last_id==31 && last_value==29);
  add_result=0;
  for(unsigned bad=0;bad<5;++bad) {
    DpuPlaneProperties missing;auto bad_encoding=encoding;
    if(bad==0)bad_encoding.flags|=DRM_MODE_PROP_IMMUTABLE;
    if(bad==1)bad_encoding.enums=nullptr;
    if(bad==2)bad_encoding.count_enums=0;
    if(bad==3)bad_encoding.count_enums=17;
    if(bad==4)bad_encoding.flags=DRM_MODE_PROP_RANGE;
    missing.Observe(bad_encoding);missing.Observe(range);
    assert(missing.Color(nullptr,7,true)==-EOPNOTSUPP);
  }
  DpuPlaneProperties partial;encoding.count_enums=1;partial.Observe(encoding);partial.Observe(range);
  assert(partial.Color(nullptr,7,true)==0 && partial.Color(nullptr,7,false)==-EOPNOTSUPP);
  encoding.count_enums=2;
  DpuPlaneProperties duplicate;
  strcpy(encoding_enums[1].name,"ITU-R BT.709 YCbCr");duplicate.Observe(encoding);duplicate.Observe(range);
  assert(duplicate.Color(nullptr,7,true)==-EOPNOTSUPP);
  strcpy(encoding_enums[1].name,"ITU-R BT.601 YCbCr");
  additions=0;
  DpuPlaneProperties empty;
  assert(empty.Alpha(nullptr, 7, 255) == -EOPNOTSUPP);
  assert(empty.Blend(nullptr, 7, 1) == -EOPNOTSUPP);
  assert(empty.InputFence(nullptr, 7, -1) == -EOPNOTSUPP);
  assert(additions == 0);
  for (uint64_t maximum : {uint64_t(255), uint64_t(65535)}) {
    DpuPlaneProperties props;
    uint64_t range[] = {0, maximum};
    auto alpha = property("alpha", 11, DRM_MODE_PROP_RANGE);
    alpha.count_values = 2; alpha.values = range;
    props.Observe(alpha);
    for (uint32_t i = 0; i <= 255; ++i) {
      assert(props.Alpha(nullptr, 7, i) == 0);
      assert(last_id == 11 && last_plane == 7);
      assert(last_value == (uint64_t(i) * maximum) / 255);
    }
    int before = additions;
    assert(props.Alpha(nullptr, 7, 256) == -EINVAL);
    assert(props.Alpha(nullptr, 7, UINT_MAX) == -EINVAL);
    assert(additions == before);
    add_result = -ENOMEM;
    assert(props.Alpha(nullptr, 7, 12) == -ENOMEM);
    add_result = 4;
    assert(props.Alpha(nullptr, 7, 12) == 0);
    add_result = 0;
  }
  for (unsigned bad = 0; bad < 5; ++bad) {
    DpuPlaneProperties props;
    uint64_t range[] = {0, 65535};
    auto alpha = property("alpha", 11, DRM_MODE_PROP_RANGE);
    alpha.count_values = 2; alpha.values = range;
    if (bad == 0) range[0] = 1;
    if (bad == 1) range[1] = 1023;
    if (bad == 2) alpha.flags |= DRM_MODE_PROP_IMMUTABLE;
    if (bad == 3) alpha.values = nullptr;
    if (bad == 4) alpha.count_values = 1;
    props.Observe(alpha);
    assert(props.Alpha(nullptr, 7, 255) == -EOPNOTSUPP);
  }
  DpuPlaneProperties props;
  drm_mode_property_enum enums[3] = {};
  strcpy(enums[0].name, "Coverage"); enums[0].value = 42;
  strcpy(enums[1].name, "None"); enums[1].value = 8;
  strcpy(enums[2].name, "Pre-multiplied"); enums[2].value = 17;
  auto blend = property("pixel blend mode", 12, DRM_MODE_PROP_ENUM);
  blend.count_enums = 3; blend.enums = enums;
  props.Observe(blend);
  uint64_t expected[] = {8, 17, 42};
  for (uint32_t i = 1; i <= 3; ++i) {
    assert(props.Blend(nullptr, 7, i) == 0);
    assert(last_id == 12 && last_value == expected[i - 1]);
  }
  int before = additions;
  assert(props.Blend(nullptr, 7, 0) == -EINVAL);
  assert(props.Blend(nullptr, 7, 4) == -EINVAL);
  assert(additions == before);
  DpuPlaneProperties missing;
  blend.count_enums = 1;
  missing.Observe(blend);
  assert(missing.Blend(nullptr, 7, 1) == -EOPNOTSUPP);
  assert(missing.Blend(nullptr, 7, 3) == 0);
  auto legacy = property("blend_op", 14, DRM_MODE_PROP_ENUM);
  props.Observe(legacy);
  assert(props.Blend(nullptr, 7, 2) == 0 && last_id == 14 && last_value == 2);
  auto fence = property("IN_FENCE_FD", 15, DRM_MODE_PROP_SIGNED_RANGE);
  props.Observe(fence);
  assert(props.InputFence(nullptr, 7, -1) == 0 && last_value == UINT64_MAX);
  before = additions;
  assert(props.InputFence(nullptr, 7, -2) == -EINVAL && additions == before);
  int fds[2];
  assert(pipe(fds) == 0);
  assert(props.InputFence(nullptr, 7, fds[0]) == 0);
  assert(last_id == 15 && last_value == uint64_t(fds[0]));
  assert(write(fds[1], "x", 1) == 1);
  char c;
  assert(read(fds[0], &c, 1) == 1 && c == 'x');
  close(fds[0]); close(fds[1]);
  assert(props.InputFence(nullptr, 7, INT_MAX) == 0 && last_value == INT_MAX);
  props.Observe(property("input_fence", 16, DRM_MODE_PROP_RANGE));
  assert(props.InputFence(nullptr, 7, -1) == 0 && last_id == 16);
  // Property enumeration order must not change preference for the legacy contract.
  props.Observe(fence); props.Observe(blend);
  assert(props.InputFence(nullptr, 7, -1) == 0 && last_id == 16);
  assert(props.Blend(nullptr, 7, 2) == 0 && last_id == 14);
}
