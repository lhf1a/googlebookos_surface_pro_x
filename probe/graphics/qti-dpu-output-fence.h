// Standard present completion and the DPU's true next-update release completion.
#ifndef QTI_DPU_OUTPUT_FENCE_H
#define QTI_DPU_OUTPUT_FENCE_H
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <xf86drmMode.h>

namespace sde_drm {
class DpuOutputFences {
 public:
  ~DpuOutputFences() { Reset(); }
  DpuOutputFences() = default;
  DpuOutputFences(const DpuOutputFences &) = delete;
  DpuOutputFences &operator=(const DpuOutputFences &) = delete;

  int Init(int fd, uint32_t crtc, uint32_t connector) {
    if (crtc_ || !crtc || !connector) return -EINVAL;
    auto props = drmModeObjectGetProperties(fd, crtc, DRM_MODE_OBJECT_CRTC);
    if (!props) return errno ? -errno : -ENODEV;
    uint32_t legacy = 0;
    for (uint32_t i = 0; i < props->count_props; ++i) {
      auto p = drmModeGetProperty(fd, props->props[i]);
      if (!p) {
        drmModeFreeObjectProperties(props);
        return errno ? -errno : -ENODEV;
      }
      if (!strcmp(p->name, "output_fence")) legacy = p->prop_id;
      if ((p->flags & DRM_MODE_PROP_RANGE) && !(p->flags & DRM_MODE_PROP_IMMUTABLE)) {
        if (!strcmp(p->name, "OUT_FENCE_PTR")) retire_id_ = p->prop_id;
        if (!strcmp(p->name, "QCOM_DPU_RELEASE_FENCE_PTR")) release_id_ = p->prop_id;
      }
      drmModeFreeProperty(p);
    }
    drmModeFreeObjectProperties(props);
    enabled_ = retire_id_ && !legacy;
    if (enabled_) {
      auto conn = drmModeGetConnector(fd, connector);
      if (!conn) return errno ? -errno : -ENODEV;
      bool writeback = conn->connector_type == DRM_MODE_CONNECTOR_WRITEBACK;
      drmModeFreeConnector(conn);
      if (writeback) return -EOPNOTSUPP;
    }
    crtc_ = crtc;
    connector_ = connector;
    return 0;
  }
  bool Enabled() const { return enabled_; }

  int RequestRelease(uint32_t crtc, int64_t *output) {
    if (!output) return -EINVAL;
    *output = -1;
    if (crtc != crtc_ || output == retire_output_) return -EINVAL;
    if (!release_id_) return -EOPNOTSUPP;
    if (release_output_ && release_output_ != output) return -EALREADY;
    release_output_ = output;
    return 0;
  }
  int RequestRetire(uint32_t connector, int64_t *output) {
    if (!output) return -EINVAL;
    *output = -1;
    // A writeback connector's completion must never be replaced by the panel's flip.
    if (connector != connector_ || output == release_output_) return -EOPNOTSUPP;
    if (!retire_id_) return -EOPNOTSUPP;
    if (retire_output_ && retire_output_ != output) return -EALREADY;
    retire_output_ = output;
    return 0;
  }
  int SetOffset(uint32_t crtc, uint32_t offset) const {
    if (crtc != crtc_) return -EINVAL;
    // The DPU property implements non-speculative video-mode release.
    return offset == 0 ? 0 : -EOPNOTSUPP;
  }
  int Prepare(drmModeAtomicReq *request) {
    if (release_raw_ >= 0 || retire_raw_ >= 0) return -EBUSY;
    if (release_output_) {
      int ret = Add(request, release_id_, &release_raw_);
      if (ret) return ret;
    }
    if (retire_output_) {
      int ret = Add(request, retire_id_, &retire_raw_);
      if (ret) return ret;
    }
    return 0;
  }
  int Complete(bool success) {
    if (!success) {
      Reset();
      return 0;
    }
    if ((release_output_ && release_raw_ < 0) || (retire_output_ && retire_raw_ < 0)) {
      Reset();
      return -EIO;
    }
    if (release_output_) {
      *release_output_ = release_raw_;
      release_raw_ = -1;
    }
    if (retire_output_) {
      *retire_output_ = retire_raw_;
      retire_raw_ = -1;
    }
    Reset();
    return 0;
  }
  void Reset() {
    if (release_raw_ >= 0) close(release_raw_);
    if (retire_raw_ >= 0) close(retire_raw_);
    release_raw_ = retire_raw_ = -1;
    // The caller's stack pointers can expire if it abandons a request.
    release_output_ = retire_output_ = nullptr;
  }

 private:
  int Add(drmModeAtomicReq *request, uint32_t property, int32_t *output) {
    int ret = drmModeAtomicAddProperty(request, crtc_, property,
                                      reinterpret_cast<uintptr_t>(output));
    return ret < 0 ? ret : 0;
  }
  uint32_t crtc_ = 0, connector_ = 0, release_id_ = 0, retire_id_ = 0;
  bool enabled_ = false;
  int32_t release_raw_ = -1, retire_raw_ = -1;
  int64_t *release_output_ = nullptr, *retire_output_ = nullptr;
};
}  // namespace sde_drm
#endif
