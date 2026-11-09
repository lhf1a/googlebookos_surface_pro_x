// Bounds for the current Linux DPU backend; unsupported layers must use client composition.
#ifndef QTI_DPU_SDM_CONTRACT_H
#define QTI_DPU_SDM_CONTRACT_H
#include <stdint.h>
#include <drm_interface.h>
#include <color_metadata.h>

namespace sde_drm {
inline bool DpuLogicalTopology(uint32_t width, const DRMCrtcInfo &caps, DRMTopology *topology) {
  if (!width || !caps.max_mixer_width) return false;
  if (width <= caps.max_mixer_width) { *topology = DRMTopology::SINGLE_LM; return true; }
  if (width > uint64_t(caps.max_mixer_width) * 2 || (width & 1)) return false;
  *topology = DRMTopology::DUAL_LM_MERGE;
  return true;
}
inline bool DpuColorSupported(bool yuv, const ColorMetaData &color) {
  if (color.masteringDisplayInfo.colorVolumeSEIEnabled ||
      color.contentLightLevel.lightLevelSEIEnabled || color.dynamicMetaDataValid || color.cRI.criEnabled)
    return false;
  if (!yuv)
    return color.colorPrimaries == ColorPrimaries_BT709_5 &&
           color.transfer == Transfer_sRGB && color.range == Range_Full &&
           color.matrixCoefficients == MatrixCoEff_Identity;
  bool primary = color.colorPrimaries == ColorPrimaries_BT601_6_525 ||
                 color.colorPrimaries == ColorPrimaries_BT601_6_625;
  bool matrix = color.matrixCoefficients == MatrixCoEff_BT601_6_525 ||
                color.matrixCoefficients == MatrixCoEff_BT601_6_625;
  bool transfer = color.transfer == Transfer_sRGB || color.transfer == Transfer_SMPTE_170M;
  bool bt709 = color.colorPrimaries == ColorPrimaries_BT709_5 &&
               color.matrixCoefficients == MatrixCoEff_BT709_5;
  return ((primary && matrix) || bt709) && transfer && color.range == Range_Limited;
}
}  // namespace sde_drm
#endif
