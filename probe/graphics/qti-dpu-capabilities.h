// Read the versioned DPU facts and standard IN_FORMATS; never synthesize donor capabilities.
#ifndef QTI_DPU_CAPABILITIES_H
#define QTI_DPU_CAPABILITIES_H
#include <drm_interface.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace sde_drm {
namespace dpu_caps {
using Fields = std::map<std::string, std::string>;
inline bool ParseText(const void *bytes, size_t size, const char *object, Fields *out) {
  if (!bytes || !size || size > 8192) return false;
  const char *data = static_cast<const char *>(bytes);
  if (data[size - 1] == 0) --size;
  if (!size || memchr(data, 0, size)) return false;
  Fields fields;
  size_t begin = 0;
  while (begin < size) {
    const char *end = static_cast<const char *>(memchr(data + begin, '\n', size - begin));
    size_t length = end ? size_t(end - data) - begin : size - begin;
    std::string line(data + begin, length);
    auto equal = line.find('=');
    if (equal == 0 || equal == std::string::npos || equal == line.size() - 1 ||
        !fields.emplace(line.substr(0, equal), line.substr(equal + 1)).second) return false;
    begin += length + (end ? 1 : 0);
  }
  if (fields["version"] != "1" || fields["object"] != object) return false;
  *out = std::move(fields);
  return true;
}
template<class T> inline bool Number(const Fields &f, const char *key, T *out) {
  auto it = f.find(key);
  if (it == f.end() || it->second.empty()) return false;
  uint64_t value = 0, maximum = std::numeric_limits<T>::max();
  for (unsigned char c : it->second) {
    if (c < '0' || c > '9' || value > maximum / 10 ||
        (value == maximum / 10 && uint64_t(c - '0') > maximum % 10)) return false;
    value = value * 10 + c - '0';
  }
  *out = static_cast<T>(value);
  return true;
}
inline int Blob(int fd, uint32_t object, uint32_t type, const char *name,
                std::vector<uint8_t> *out) {
  auto props = drmModeObjectGetProperties(fd, object, type);
  if (!props) return errno ? -errno : -EIO;
  int result = -ENOENT;
  for (uint32_t i = 0; i < props->count_props; ++i) {
    auto prop = drmModeGetProperty(fd, props->props[i]);
    if (!prop) { result = errno ? -errno : -EIO; break; }
    bool match = !strcmp(prop->name, name);
    bool valid = (prop->flags & DRM_MODE_PROP_BLOB) &&
                 (prop->flags & DRM_MODE_PROP_IMMUTABLE);
    drmModeFreeProperty(prop);
    if (!match) continue;
    result = -EINVAL;
    if (valid && props->prop_values[i] && props->prop_values[i] <= UINT32_MAX) {
      auto blob = drmModeGetPropertyBlob(fd, static_cast<uint32_t>(props->prop_values[i]));
      if (blob && blob->data && blob->length && blob->length <= 1024 * 1024) {
        auto data = static_cast<const uint8_t *>(blob->data);
        out->assign(data, data + blob->length);
        result = 0;
      }
      drmModeFreePropertyBlob(blob);
    }
    break;
  }
  drmModeFreeObjectProperties(props);
  return result;
}
inline bool Formats(const void *data, size_t length,
                    std::vector<std::pair<uint32_t, uint64_t>> *out) {
  if (!data || length < sizeof(drm_format_modifier_blob)) return false;
  const auto *bytes = static_cast<const uint8_t *>(data);
  drm_format_modifier_blob h;
  memcpy(&h, data, sizeof(h));
  if (h.version != 1 || h.flags || !h.count_formats || !h.count_modifiers ||
      h.formats_offset < sizeof(h) || h.modifiers_offset < sizeof(h) ||
      uint64_t(h.formats_offset) + uint64_t(h.count_formats) * 4 > length ||
      uint64_t(h.modifiers_offset) + uint64_t(h.count_modifiers) * sizeof(drm_format_modifier) > length)
    return false;
  const uint64_t format_end = uint64_t(h.formats_offset) + uint64_t(h.count_formats) * 4;
  const uint64_t modifier_end = uint64_t(h.modifiers_offset) +
                                uint64_t(h.count_modifiers) * sizeof(drm_format_modifier);
  if (h.formats_offset < modifier_end && h.modifiers_offset < format_end) return false;
  std::vector<std::pair<uint32_t, uint64_t>> result;
  for (uint32_t i = 0; i < h.count_modifiers; ++i) {
    drm_format_modifier mod;
    memcpy(&mod, bytes + h.modifiers_offset + size_t(i) * sizeof(mod), sizeof(mod));
    if (mod.pad) return false;
    for (unsigned bit = 0; bit < 64; ++bit) {
      if (!(mod.formats & (uint64_t(1) << bit))) continue;
      uint64_t index = uint64_t(mod.offset) + bit;
      if (index >= h.count_formats) return false;
      uint32_t format;
      memcpy(&format, bytes + h.formats_offset + size_t(index) * 4, 4);
      result.emplace_back(format, mod.modifier);
    }
  }
  if (result.empty()) return false;
  *out = std::move(result);
  return true;
}
// Linux SSPP_SCALER_VER encodes major in bits 16..31, minor in bits 0..15.
inline bool Qseed(uint32_t version, QSEEDStepVersion *out) {
  switch (version) {
    case 0x10002: *out = QSEEDStepVersion::V2; break;
    case 0x10003: *out = QSEEDStepVersion::V3; break;
    case 0x10004: *out = QSEEDStepVersion::V4; break;
    case 0x20004: *out = QSEEDStepVersion::V3LITE_V4; break;
    case 0x30000: *out = QSEEDStepVersion::V3LITE_V5; break;
    default: return false;
  }
  return true;
}
inline int Plane(const Fields &f, DRMPlaneTypeInfo *info) {
  bool physical, scaler, csc, kernel_scaler, kernel_csc, fixed_csc;
  uint32_t version, min_scale, max_scale;
  uint64_t bw;
  DRMPlaneTypeInfo value = {};
  if (!Number(f, "physical_pipe", &physical) || !physical ||
      !Number(f, "scaler", &scaler) || !Number(f, "csc", &csc) ||
      !Number(f, "kernel_managed_scaler", &kernel_scaler) ||
      !Number(f, "kernel_managed_csc", &kernel_csc) ||
      !Number(f, "csc_bt601_limited_only", &fixed_csc) ||
      !Number(f, "scaler_hw_version", &version) ||
      !Number(f, "min_scale_16_16", &min_scale) ||
      !Number(f, "max_scale_16_16", &max_scale) ||
      !Number(f, "max_linewidth", &value.max_linewidth) || !value.max_linewidth ||
      !Number(f, "max_upscale", &value.max_upscale) || !value.max_upscale ||
      !Number(f, "max_downscale", &value.max_downscale) || !value.max_downscale ||
      !Number(f, "max_per_pipe_bw_kbps", &bw) || bw > UINT64_MAX / 1000 ||
      scaler != kernel_scaler || csc != kernel_csc || csc != fixed_csc) return -EINVAL;
  if (min_scale != 65536 / value.max_upscale ||
      max_scale != (uint64_t(value.max_downscale) << 16)) return -EINVAL;
  auto type = f.find("pipe_type");
  if (type == f.end()) return -EINVAL;
  if (type->second == "vig" && scaler && csc) value.type = DRMPlaneType::VIG;
  else if (type->second == "rgb" && scaler && !csc) value.type = DRMPlaneType::RGB;
  else if (type->second == "dma" && !scaler && !csc) value.type = DRMPlaneType::DMA;
  else if (type->second == "cursor" && !scaler && !csc) value.type = DRMPlaneType::CURSOR;
  else return -EOPNOTSUPP;
  if (scaler && !Qseed(version, &value.qseed3_version)) return -EOPNOTSUPP;
  if (!scaler && (value.max_upscale != 1 || value.max_downscale != 1)) return -EINVAL;
  value.max_scaler_linewidth = value.max_linewidth;
  value.max_pipe_bandwidth = value.max_pipe_bandwidth_high = bw * 1000;
  value.inrot_version = InlineRotationVersion::kInlineRotationNone;
  // Mainline's current CSC path is BT601 limited only; no extended CSC/tonemap advertised.
  *info = std::move(value);
  return 0;
}
inline int ReadPlane(int fd, uint32_t plane, DRMPlaneTypeInfo *info) {
  std::vector<uint8_t> text, formats;
  int ret = Blob(fd, plane, DRM_MODE_OBJECT_PLANE, "QCOM_DPU_CAPS", &text);
  if (ret) return ret;
  Fields fields;
  DRMPlaneTypeInfo value = {};
  if (!ParseText(text.data(), text.size(), "plane", &fields)) return -EINVAL;
  ret = Plane(fields, &value);
  if (ret) return ret;
  ret = Blob(fd, plane, DRM_MODE_OBJECT_PLANE, "IN_FORMATS", &formats);
  if (ret) return ret;
  if (!Formats(formats.data(), formats.size(), &value.formats_supported)) return -EINVAL;
  *info = std::move(value);
  return 0;
}
inline int Crtc(const Fields &f, DRMCrtcInfo *info) {
  DRMCrtcInfo value = {};
  bool votes;
  uint32_t ubwc, clock_pct, bw_pct;
  uint64_t low, high;
#define DPU_FIELD(key, field) if (!Number(f, key, &value.field)) return -EINVAL
  DPU_FIELD("hw_version", hw_version);
  DPU_FIELD("max_blendstages", max_blend_stages);
  DPU_FIELD("has_src_split", has_src_split);
  DPU_FIELD("max_core_clk_hz", max_sde_clk);
  DPU_FIELD("max_mixer_width", max_mixer_width);
  DPU_FIELD("undersized_prefill_lines", undersized_prefill_lines);
  DPU_FIELD("xtra_prefill_lines", extra_prefill_lines);
  DPU_FIELD("dest_scale_prefill_lines", dest_scale_prefill_lines);
  DPU_FIELD("macrotile_prefill_lines", macrotile_prefill_lines);
  DPU_FIELD("yuv_nv12_prefill_lines", nv12_prefill_lines);
  DPU_FIELD("linear_prefill_lines", linear_prefill_lines);
  DPU_FIELD("downscaling_prefill_lines", downscale_prefill_lines);
  DPU_FIELD("amortizable_threshold", amortized_threshold);
  DPU_FIELD("min_prefill_lines", min_prefill_lines);
#undef DPU_FIELD
  if (!Number(f, "kernel_bw_votes", &votes) || !votes ||
      !Number(f, "max_bw_low_kbps", &low) || low > UINT64_MAX / 1000 ||
      !Number(f, "max_bw_high_kbps", &high) || high > UINT64_MAX / 1000 ||
      !Number(f, "ubwc_version", &ubwc) ||
      !Number(f, "clk_inefficiency_pct", &clock_pct) ||
      !Number(f, "bw_inefficiency_pct", &bw_pct) ||
      !value.hw_version || !value.max_blend_stages || !value.max_sde_clk ||
      !value.max_mixer_width) return -EINVAL;
  value.max_bandwidth_low = low * 1000;
  value.max_bandwidth_high = high * 1000;
  value.clk_fudge_factor = float(clock_pct) / 100;
  value.ib_fudge_factor = float(bw_pct) / 100;
  value.ubwc_version = ubwc >> 28;
  value.kernel_managed_dpu = true;
  *info = std::move(value);
  return 0;
}
inline int ReadCrtc(int fd, uint32_t crtc, DRMCrtcInfo *info) {
  std::vector<uint8_t> text;
  int ret = Blob(fd, crtc, DRM_MODE_OBJECT_CRTC, "QCOM_DPU_CAPS", &text);
  if (ret) return ret;
  Fields fields;
  if (!ParseText(text.data(), text.size(), "crtc", &fields)) return -EINVAL;
  DRMCrtcInfo value = {};
  ret = Crtc(fields, &value);
  if (ret) return ret;
  auto planes = drmModeGetPlaneResources(fd);
  if (!planes) return -EIO;
  for (uint32_t i = 0; i < planes->count_planes; ++i) {
    std::vector<uint8_t> data;
    ret = Blob(fd, planes->planes[i], DRM_MODE_OBJECT_PLANE, "QCOM_DPU_CAPS", &data);
    Fields plane;
    bool scaler;
    uint32_t version;
    if (ret || !ParseText(data.data(), data.size(), "plane", &plane) ||
        !Number(plane, "scaler", &scaler) || !Number(plane, "scaler_hw_version", &version)) {
      ret = ret ? ret : -EINVAL; break;
    }
    if (scaler) {
      QSEEDStepVersion step;
      if (!Qseed(version, &step)) { ret = -EOPNOTSUPP; break; }
      value.qseed_version = version < 0x20000 ? QSEEDVersion::V3 : QSEEDVersion::V3LITE;
    }
  }
  drmModeFreePlaneResources(planes);
  if (ret) return ret;
  *info = std::move(value);
  return 0;
}
}  // namespace dpu_caps
}  // namespace sde_drm
#endif
