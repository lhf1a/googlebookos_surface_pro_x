// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <utility>
namespace spx {
using Size=std::pair<int,int>;
inline Size nativeSize(bool front){return front?Size{2592,1944}:Size{4224,3136};}
inline Size binnedSize(bool front){return front?Size{1296,972}:Size{2112,1568};}
inline std::array<Size,5> outputSizes(bool front){
    return {Size{320,240},Size{640,480},Size{1280,720},Size{1920,1080},nativeSize(front)};
}
inline bool outputSize(bool front,int w,int h){
    for(auto size:outputSizes(front))if(size==Size{w,h})return true;
    return false;
}
inline int jpegMaxSize(bool front){auto [w,h]=nativeSize(front);return w*h*3+65536;}
}
