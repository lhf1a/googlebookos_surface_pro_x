/* QTI color metadata via the pinned exported C API, never private-handle offsets.
 * HDR conversions use the vendor's documented units; no pixel/tone-map operation. */
#pragma once
#include <color_metadata.h>
#include <aidl/android/hardware/graphics/common/Dataspace.h>
#include <aidl/android/hardware/graphics/common/Smpte2086.h>
#include <aidl/android/hardware/graphics/common/Cta861_3.h>
#include <cutils/native_handle.h>
#include <cmath>
#include <optional>
#include <cstddef>
struct private_handle_t;
extern "C" int getMetaData(private_handle_t *,int,void *);
extern "C" int setMetaData(private_handle_t *,int,void *);
static_assert(sizeof(ColorMetaData)==2804);
static_assert(offsetof(ColorMetaData,masteringDisplayInfo)==16);
static_assert(offsetof(ColorMetaData,contentLightLevel)==60);
namespace questcolor {
namespace gc=aidl::android::hardware::graphics::common;
inline private_handle_t *handle(buffer_handle_t h) {
    return reinterpret_cast<private_handle_t *>(const_cast<native_handle_t *>(h));
}
inline bool read(buffer_handle_t h,ColorMetaData &out) {
    out={};return getMetaData(handle(h),2,&out)==0;
}
inline bool write(buffer_handle_t h,ColorMetaData &data) {
    return setMetaData(handle(h),2,&data)==0;
}
inline bool setDataspace(gc::Dataspace value,ColorMetaData &c) {
    uint32_t d=static_cast<uint32_t>(value);
    if(!d){c.colorPrimaries=static_cast<ColorPrimaries>(0);c.transfer=static_cast<GammaTransfer>(0);
        c.range=Range_Limited;c.matrixCoefficients=MatrixCoEff_Identity;return true;}
    // Legacy named dataspaces have no independent bitfields. Do not silently reinterpret them.
    if(d&~0x3fff0000U)return false;
    switch(d&0x3f0000) {
        case 1<<16:c.colorPrimaries=ColorPrimaries_BT709_5;c.matrixCoefficients=MatrixCoEff_BT709_5;break;
        case 2<<16:c.colorPrimaries=ColorPrimaries_BT601_6_625;c.matrixCoefficients=MatrixCoEff_BT601_6_625;break;
        case 3<<16:c.colorPrimaries=ColorPrimaries_BT601_6_625;c.matrixCoefficients=MatrixCoEff_BT601_6_Unadjusted;break;
        case 4<<16:c.colorPrimaries=ColorPrimaries_BT601_6_525;c.matrixCoefficients=MatrixCoEff_BT601_6_525;break;
        case 5<<16:c.colorPrimaries=ColorPrimaries_BT601_6_525;c.matrixCoefficients=MatrixCoEff_SMPTE240M;break;
        case 6<<16:c.colorPrimaries=ColorPrimaries_BT2020;c.matrixCoefficients=MatrixCoEff_BT2020;break;
        case 7<<16:c.colorPrimaries=ColorPrimaries_BT2020;c.matrixCoefficients=MatrixCoEff_BT2020Constant;break;
        case 8<<16:c.colorPrimaries=ColorPrimaries_BT470_6M;c.matrixCoefficients=MatrixCoeff_FCC_73_682;break;
        case 9<<16:c.colorPrimaries=ColorPrimaries_GenericFilm;c.matrixCoefficients=MatrixCoEff_Identity;break;
        case 10<<16:c.colorPrimaries=ColorPrimaries_DCIP3;c.matrixCoefficients=MatrixCoEff_DCIP3;break;
        case 11<<16:c.colorPrimaries=ColorPrimaries_AdobeRGB;c.matrixCoefficients=MatrixCoEff_Identity;break;
        default:return false;
    }
    switch(d&0x7c00000) {
        case 1<<22:c.transfer=Transfer_Linear;break;
        case 2<<22:c.transfer=Transfer_sYCC;break;
        case 3<<22:c.transfer=Transfer_SMPTE_170M;break;
        case 4<<22:c.transfer=Transfer_Gamma2_2;break;
        // QTI's color ABI has no gamma 2.6 value.
        case 6<<22:c.transfer=Transfer_Gamma2_8;break;
        case 7<<22:c.transfer=Transfer_SMPTE_ST2084;break;
        case 8<<22:c.transfer=Transfer_HLG;break;
        default:return false;
    }
    switch(d&0x38000000) {
        case 1<<27:c.range=Range_Full;break;
        case 2<<27:c.range=Range_Limited;break;
        case 3<<27:c.range=Range_Extended;break;
        default:return false;
    }
    return true;
}
inline gc::Dataspace dataspace(const ColorMetaData &c) {
    uint32_t s=0,t=0,r=0;
    switch(c.colorPrimaries) {
        case ColorPrimaries_BT709_5:s=1;break;
        case ColorPrimaries_BT601_6_625:s=c.matrixCoefficients==MatrixCoEff_BT601_6_Unadjusted?3:2;break;
        case ColorPrimaries_BT601_6_525:s=c.matrixCoefficients==MatrixCoEff_SMPTE240M?5:4;break;
        case ColorPrimaries_BT2020:s=c.matrixCoefficients==MatrixCoEff_BT2020Constant?7:6;break;
        case ColorPrimaries_BT470_6M:s=8;break;
        case ColorPrimaries_GenericFilm:s=9;break;
        case ColorPrimaries_DCIP3:s=10;break;
        case ColorPrimaries_AdobeRGB:s=11;break;
        default:return gc::Dataspace::UNKNOWN;
    }
    switch(c.transfer) {
        case Transfer_Linear:t=1;break;
        case Transfer_sYCC:t=2;break;
        case Transfer_sRGB:case Transfer_SMPTE_170M:case Transfer_BT2020_2_1:case Transfer_BT2020_2_2:t=3;break;
        case Transfer_Gamma2_2:t=4;break;
        case Transfer_Gamma2_8:t=6;break;
        case Transfer_SMPTE_ST2084:t=7;break;
        case Transfer_HLG:t=8;break;
        default:return gc::Dataspace::UNKNOWN;
    }
    switch(c.range) {
        case Range_Full:r=1;break;case Range_Limited:r=2;break;case Range_Extended:r=3;break;
        default:return gc::Dataspace::UNKNOWN;
    }
    return static_cast<gc::Dataspace>((s<<16)|(t<<22)|(r<<27));
}
inline bool quantize(float value,double scale,uint32_t &out) {
    double scaled=double(value)*scale;
    if(!std::isfinite(scaled) || scaled<0 || scaled>UINT32_MAX)return false;
    double rounded=std::round(scaled);if(rounded>UINT32_MAX)return false;
    out=static_cast<uint32_t>(rounded);return true;
}
inline std::optional<gc::Smpte2086> mastering(const ColorMetaData &c) {
    const auto &m=c.masteringDisplayInfo;
    if(!m.colorVolumeSEIEnabled)return std::nullopt;
    gc::Smpte2086 v;
    v.primaryRed.x=m.primaries.rgbPrimaries[0][0]/50000.f;
    v.primaryRed.y=m.primaries.rgbPrimaries[0][1]/50000.f;
    v.primaryGreen.x=m.primaries.rgbPrimaries[1][0]/50000.f;
    v.primaryGreen.y=m.primaries.rgbPrimaries[1][1]/50000.f;
    v.primaryBlue.x=m.primaries.rgbPrimaries[2][0]/50000.f;
    v.primaryBlue.y=m.primaries.rgbPrimaries[2][1]/50000.f;
    v.whitePoint.x=m.primaries.whitePoint[0]/50000.f;v.whitePoint.y=m.primaries.whitePoint[1]/50000.f;
    v.maxLuminance=m.maxDisplayLuminance;v.minLuminance=m.minDisplayLuminance/10000.f;
    return v;
}
inline bool setMastering(const std::optional<gc::Smpte2086> &v,ColorMetaData &c) {
    if(!v){c.masteringDisplayInfo={};return true;}
    MasteringDisplay m{};
    const gc::XyColor xy[]={v->primaryRed,v->primaryGreen,v->primaryBlue,v->whitePoint};
    uint32_t *out[]={m.primaries.rgbPrimaries[0],m.primaries.rgbPrimaries[1],
                    m.primaries.rgbPrimaries[2],m.primaries.whitePoint};
    for(unsigned i=0;i<4;i++) {
        if(xy[i].x>1 || xy[i].y>1 || !quantize(xy[i].x,50000,out[i][0]) ||
           !quantize(xy[i].y,50000,out[i][1]))return false;
    }
    if(v->minLuminance>v->maxLuminance || !quantize(v->maxLuminance,1,m.maxDisplayLuminance) ||
       !quantize(v->minLuminance,10000,m.minDisplayLuminance))return false;
    m.colorVolumeSEIEnabled=true;c.masteringDisplayInfo=m;return true;
}
inline std::optional<gc::Cta861_3> light(const ColorMetaData &c) {
    if(!c.contentLightLevel.lightLevelSEIEnabled)return std::nullopt;
    gc::Cta861_3 v;v.maxContentLightLevel=c.contentLightLevel.maxContentLightLevel;
    v.maxFrameAverageLightLevel=c.contentLightLevel.maxPicAverageLightLevel;return v;
}
inline bool setLight(const std::optional<gc::Cta861_3> &v,ColorMetaData &c) {
    if(!v){c.contentLightLevel={};return true;}
    ContentLightLevel l{};
    if(!quantize(v->maxContentLightLevel,1,l.maxContentLightLevel) ||
       !quantize(v->maxFrameAverageLightLevel,1,l.maxPicAverageLightLevel))return false;
    l.minPicAverageLightLevel=l.maxPicAverageLightLevel;l.lightLevelSEIEnabled=true;
    c.contentLightLevel=l;return true;
}
} // namespace questcolor
