// QTI plane values translated using the actual DRM property's range and enum table.
#ifndef QTI_DPU_PLANE_PROPERTIES_H
#define QTI_DPU_PLANE_PROPERTIES_H
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <xf86drmMode.h>

namespace sde_drm {
class DpuPlaneProperties {
 public:
  void Observe(const drmModePropertyRes &p) {
    if (!strcmp(p.name, "alpha")) {
      alpha_id_ = p.prop_id;
      if ((p.flags & DRM_MODE_PROP_RANGE) && !(p.flags & DRM_MODE_PROP_IMMUTABLE) &&
          p.count_values == 2 && p.values && p.values[0] == 0 &&
          (p.values[1] == 255 || p.values[1] == 65535)) {
        alpha_max_ = p.values[1];
      }
    } else if (!strcmp(p.name, "blend_op")) {
      legacy_blend_ = p.prop_id;
    } else if (!strcmp(p.name, "pixel blend mode")) {
      blend_id_ = p.prop_id;
      if (!(p.flags & DRM_MODE_PROP_ENUM) || (p.flags & DRM_MODE_PROP_IMMUTABLE) ||
          p.count_enums < 0 || (p.count_enums && !p.enums)) return;
      const char *names[] = {"None", "Pre-multiplied", "Coverage"};
      for (int i = 0; i < p.count_enums; ++i) {
        for (unsigned j = 0; j < 3; ++j) {
          if (!strcmp(p.enums[i].name, names[j])) {
            blend_values_[j] = p.enums[i].value;
            blend_present_[j] = true;
          }
        }
      }
    } else if (!strcmp(p.name, "COLOR_ENCODING") || !strcmp(p.name, "COLOR_RANGE")) {
      if (!(p.flags & DRM_MODE_PROP_ENUM) || (p.flags & DRM_MODE_PROP_IMMUTABLE) ||
          !p.prop_id || p.count_enums <= 0 || p.count_enums > 16 || !p.enums) return;
      const bool encoding = !strcmp(p.name, "COLOR_ENCODING");
      const char *names[] = {"ITU-R BT.601 YCbCr", "ITU-R BT.709 YCbCr", "YCbCr limited range"};
      unsigned counts[3] = {};
      uint64_t values[3] = {};
      for (int i = 0; i < p.count_enums; ++i)
        for (unsigned j = encoding ? 0 : 2; j < (encoding ? 2u : 3u); ++j)
          if (!strcmp(p.enums[i].name, names[j])) {
            ++counts[j]; values[j] = p.enums[i].value;
          }
      if (encoding) {
        encoding_id_ = p.prop_id;
        for (unsigned j = 0; j < 2; ++j) {
          encoding_present_[j] = counts[j] == 1;
          encoding_values_[j] = values[j];
        }
      } else {
        range_id_ = p.prop_id; limited_present_ = counts[2] == 1; limited_value_ = values[2];
      }
    } else if (!strcmp(p.name, "input_fence")) {
      legacy_fence_ = p.prop_id;
    } else if (!strcmp(p.name, "IN_FENCE_FD")) {
      if ((p.flags & DRM_MODE_PROP_SIGNED_RANGE) &&
          !(p.flags & DRM_MODE_PROP_IMMUTABLE)) fence_id_ = p.prop_id;
    }
  }

  int Alpha(drmModeAtomicReq *req, uint32_t plane, uint32_t alpha) const {
    if (alpha > 255) return -EINVAL;
    if (!alpha_id_ || !alpha_max_) return -EOPNOTSUPP;
    return Add(req, plane, alpha_id_, uint64_t(alpha) * (alpha_max_ / 255));
  }
  int Blend(drmModeAtomicReq *req, uint32_t plane, uint32_t blend) const {
    // QTI DRMBlendType: OPAQUE=1, PREMULTIPLIED=2, COVERAGE=3.
    if (blend < 1 || blend > 3) return -EINVAL;
    if (legacy_blend_) return Add(req, plane, legacy_blend_, blend);
    if (!blend_id_ || !blend_present_[blend - 1]) return -EOPNOTSUPP;
    return Add(req, plane, blend_id_, blend_values_[blend - 1]);
  }
  int Color(drmModeAtomicReq *req, uint32_t plane, bool bt709) const {
    if (!encoding_id_ || !range_id_ || !encoding_present_[bt709] || !limited_present_)
      return -EOPNOTSUPP;
    int ret = Add(req, plane, encoding_id_, encoding_values_[bt709]);
    return ret ? ret : Add(req, plane, range_id_, limited_value_);
  }
  int InputFence(drmModeAtomicReq *req, uint32_t plane, int fence) const {
    if (fence < -1) return -EINVAL;
    const uint32_t id = legacy_fence_ ? legacy_fence_ : fence_id_;
    if (!id) return -EOPNOTSUPP;
    // DRM consumes a borrowed fd; -1 must be sign-extended to the property width.
    return Add(req, plane, id, static_cast<uint64_t>(static_cast<int64_t>(fence)));
  }

 private:
  static int Add(drmModeAtomicReq *req, uint32_t plane, uint32_t id, uint64_t value) {
    const int ret = drmModeAtomicAddProperty(req, plane, id, value);
    return ret < 0 ? ret : 0;
  }
  uint32_t alpha_id_ = 0, blend_id_ = 0, legacy_blend_ = 0;
  uint32_t fence_id_ = 0, legacy_fence_ = 0;
  uint64_t alpha_max_ = 0, blend_values_[3] = {};
  bool blend_present_[3] = {};
  uint32_t encoding_id_ = 0, range_id_ = 0;
  uint64_t encoding_values_[2] = {}, limited_value_ = 0;
  bool encoding_present_[2] = {}, limited_present_ = false;
};
}  // namespace sde_drm
#endif
