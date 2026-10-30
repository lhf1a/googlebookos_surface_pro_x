#pragma once
#include <linux/videodev2.h>
#include <cstring>
enum class Sq2Codec { Avc, Hevc, Vp8, Vp9 };
struct Sq2CodecInfo { Sq2Codec codec; const char* name; const char* mime; unsigned fourcc; };
inline constexpr Sq2CodecInfo sq2Codecs[] = {
 {Sq2Codec::Avc,"c2.sq2.avc.experimental","video/avc",V4L2_PIX_FMT_H264},
 {Sq2Codec::Hevc,"c2.sq2.hevc.experimental","video/hevc",V4L2_PIX_FMT_HEVC},
 {Sq2Codec::Vp8,"c2.sq2.vp8.experimental","video/x-vnd.on2.vp8",V4L2_PIX_FMT_VP8},
 {Sq2Codec::Vp9,"c2.sq2.vp9.experimental","video/x-vnd.on2.vp9",V4L2_PIX_FMT_VP9},
};
inline const Sq2CodecInfo& sq2CodecInfo(Sq2Codec codec) {
 for (const auto& info : sq2Codecs) if (info.codec == codec) return info;
 __builtin_trap();
}
// Preserve the Codec2 canonical name; OEM OMX names are compatibility aliases,
// not an OMX implementation or a promise of secure/HDR capability.
inline const char* sq2CodecAlias(Sq2Codec codec) {
 switch(codec) {
 case Sq2Codec::Avc: return "OMX.qcom.video.decoder.avc";
 case Sq2Codec::Hevc: return "OMX.qcom.video.decoder.hevc";
 case Sq2Codec::Vp8: return "OMX.qcom.video.decoder.vp8";
 case Sq2Codec::Vp9: return "OMX.qcom.video.decoder.vp9";
 }
 __builtin_trap();
}
inline bool sq2CodecMatches(const Sq2CodecInfo& info, const char* name) {
 return name && (!std::strcmp(name,info.name) || !std::strcmp(name,sq2CodecAlias(info.codec)));
}
