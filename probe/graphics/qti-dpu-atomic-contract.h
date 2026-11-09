// The Linux DPU owns scale programming and clock/bandwidth votes.
#ifndef QTI_DPU_ATOMIC_CONTRACT_H
#define QTI_DPU_ATOMIC_CONTRACT_H
#include <algorithm>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <drm_interface.h>

namespace sde_drm {
class DpuAtomicContract {
 public:
  bool Enabled() const { return enabled_; }
  int Init(int fd, const DRMDisplayToken &token, const DRMCrtcInfo &info,
           const DRMPlanesInfo &planes) {
    enabled_ = info.kernel_managed_dpu;
    if (!enabled_) return 0;
    token_ = token;
    planes_ = planes;
    auto props = drmModeObjectGetProperties(fd, token.crtc_id, DRM_MODE_OBJECT_CRTC);
    if (!props) return -ENODEV;
    for (uint32_t i = 0; i < props->count_props; ++i) {
      auto p = drmModeGetProperty(fd, props->props[i]);
      if (!p) { drmModeFreeObjectProperties(props); return -ENODEV; }
      if (!strcmp(p->name, "ACTIVE") && (p->flags & DRM_MODE_PROP_RANGE) &&
          !(p->flags & DRM_MODE_PROP_IMMUTABLE)) active_ = p->prop_id;
      drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);
    return active_ ? 0 : -EOPNOTSUPP;
  }

  // 1 means pass to the original standard-property handler; 0 means handled.
  int Perform(drmModeAtomicReq *req, DRMOps op, uint32_t object, va_list args) const {
    if (!enabled_) return 1;
    const DRMPlaneTypeInfo *plane = nullptr;
    if (op >= DRMOps::PLANE_SET_SRC_RECT && op <= DRMOps::PLANE_SET_POST_PROC) {
      auto found = std::find_if(planes_.begin(), planes_.end(),
                                [object](const auto &item) { return item.first == object; });
      if (found == planes_.end()) return -ENOENT;
      plane = &found->second;
    } else if (op >= DRMOps::PLANES_RESET_CACHE && op <= DRMOps::CRTC_RESET_CACHE) {
      if (object != token_.crtc_id) return -EINVAL;
    } else if ((op >= DRMOps::CONNECTOR_GET_RETIRE_FENCE &&
                op <= DRMOps::CONNECTOR_SET_HDR_METADATA) ||
               (op >= DRMOps::CONNECTOR_SET_QSYNC_MODE && op <= DRMOps::CONNECTOR_SET_COLORSPACE)) {
      if (object != token_.conn_id) return -EINVAL;
    }

    switch (op) {
      case DRMOps::PLANE_SET_SRC_RECT:
      case DRMOps::PLANE_SET_DST_RECT:
      case DRMOps::PLANE_SET_ZORDER:
      case DRMOps::PLANE_SET_ROTATION:
      case DRMOps::PLANE_SET_ALPHA:
      case DRMOps::PLANE_SET_BLEND_TYPE:
      case DRMOps::PLANE_SET_FB_ID:
      case DRMOps::PLANE_SET_CRTC:
      case DRMOps::PLANE_SET_INPUT_FENCE:
      case DRMOps::PLANES_RESET_CACHE:
      case DRMOps::CRTC_SET_ACTIVE:
      case DRMOps::CRTC_SET_MODE:
      case DRMOps::CRTC_RESET_CACHE:
      case DRMOps::CRTC_SET_OUTPUT_FENCE_OFFSET:
      case DRMOps::CRTC_GET_RELEASE_FENCE:
      case DRMOps::CONNECTOR_GET_RETIRE_FENCE:
      case DRMOps::CONNECTOR_SET_CRTC:
        return 1;

      case DRMOps::PLANE_SET_EXCL_RECT: {
        auto rect = va_arg(args, DRMRect);
        return (rect.left || rect.top || rect.right || rect.bottom) ? -EOPNOTSUPP : 0;
      }
      case DRMOps::PLANE_SET_H_DECIMATION:
      case DRMOps::PLANE_SET_V_DECIMATION:
      case DRMOps::PLANE_SET_SRC_CONFIG:
      case DRMOps::PLANE_SET_MULTIRECT_MODE:
      case DRMOps::PLANE_SET_ROT_FB_ID:
      case DRMOps::PLANE_SET_INVERSE_PMA:
      case DRMOps::CONNECTOR_SET_AUTOREFRESH:
      case DRMOps::CONNECTOR_SET_QSYNC_MODE:
      case DRMOps::CONNECTOR_SET_TOPOLOGY_CONTROL:
      case DRMOps::CONNECTOR_SET_FRAME_TRIGGER:
      case DRMOps::CONNECTOR_SET_COLORSPACE:
      case DRMOps::CRTC_SET_IDLE_TIMEOUT:
      case DRMOps::CRTC_SET_IDLE_PC_STATE:
      case DRMOps::CRTC_SET_CACHE_STATE:
      case DRMOps::CRTC_SET_VM_REQ_STATE:
        return va_arg(args, uint32_t) ? -EOPNOTSUPP : 0;

      case DRMOps::PLANE_SET_FB_SECURE_MODE:
      case DRMOps::CONNECTOR_SET_FB_SECURE_MODE:
        return va_arg(args, uint32_t) == uint32_t(DRMSecureMode::NON_SECURE) ? 0 : -EOPNOTSUPP;
      case DRMOps::CRTC_SET_SECURITY_LEVEL:
        return va_arg(args, uint32_t) == uint32_t(DRMSecurityLevel::SECURE_NON_SECURE) ? 0 : -EOPNOTSUPP;
      case DRMOps::PLANE_SET_CSC_CONFIG: {
        auto csc = va_arg(args, DRMCscType *);
        if (!csc) return -EINVAL;
        if (*csc == kCscTypeMax) return 0;  // RGB: no YUV conversion.
        // The plane handler writes the actual standard COLOR_ENCODING/COLOR_RANGE enums.
        return ((*csc == kCscYuv2Rgb601L || *csc == kCscYuv2Rgb709L) &&
                plane->type == DRMPlaneType::VIG) ? 1 : -EOPNOTSUPP;
      }
      case DRMOps::PLANE_SET_SCALER_CONFIG:
      case DRMOps::PLANE_SET_DGM_CSC_CONFIG:
      case DRMOps::CRTC_SET_DEST_SCALER_CONFIG:
        // Standard SRC/CRTC rectangles are the only Linux scaler interface.
        return va_arg(args, uint64_t) ? -EOPNOTSUPP : 0;
      case DRMOps::CRTC_SET_CORE_CLK:
      case DRMOps::CRTC_SET_CORE_AB:
      case DRMOps::CRTC_SET_CORE_IB:
      case DRMOps::CRTC_SET_LLCC_AB:
      case DRMOps::CRTC_SET_LLCC_IB:
      case DRMOps::CRTC_SET_DRAM_AB:
      case DRMOps::CRTC_SET_DRAM_IB:
        return 0;  // The kernel computes and applies votes from the atomic state.
      case DRMOps::CRTC_SET_ROT_PREFILL_BW:
        return va_arg(args, uint64_t) ? -EOPNOTSUPP : 0;
      case DRMOps::CRTC_SET_ROT_CLK:
        return va_arg(args, uint32_t) ? -EOPNOTSUPP : 0;
      case DRMOps::CRTC_SET_ROI:
      case DRMOps::CONNECTOR_SET_ROI:
        return va_arg(args, uint32_t) ? -EOPNOTSUPP : 0;  // Zero restores full-frame updates.
      case DRMOps::PLANE_SET_POST_PROC:
      case DRMOps::CRTC_SET_POST_PROC:
      case DRMOps::CONNECTOR_SET_POST_PROC: {
        auto pp = va_arg(args, DRMPPFeatureInfo *);
        return pp && pp->payload ? -EOPNOTSUPP : 0;
      }
      case DRMOps::CRTC_SET_SOLIDFILL_STAGES: {
        auto stages = reinterpret_cast<const std::vector<DRMSolidfillStage> *>(va_arg(args, uint64_t));
        return stages && !stages->empty() ? -EOPNOTSUPP : 0;
      }
      case DRMOps::CONNECTOR_SET_POWER_MODE: {
        auto mode = DRMPowerMode(va_arg(args, uint32_t));
        if (mode != DRMPowerMode::ON && mode != DRMPowerMode::OFF) return -EOPNOTSUPP;
        int ret = drmModeAtomicAddProperty(req, token_.crtc_id, active_, mode == DRMPowerMode::ON);
        return ret < 0 ? ret : 0;
      }
      case DRMOps::PLANES_RESET_LUT:
      case DRMOps::DPPS_COMMIT_FEATURE:
      case DRMOps::COMMIT_PANEL_FEATURES:
        return 0;  // No LUT/DPPS/panel feature is advertised or accepted by this backend.
      default:
        return -EOPNOTSUPP;
    }
  }
 private:
  bool enabled_ = false;
  uint32_t active_ = 0;
  DRMDisplayToken token_ = {};
  DRMPlanesInfo planes_;
};
}  // namespace sde_drm
#endif
