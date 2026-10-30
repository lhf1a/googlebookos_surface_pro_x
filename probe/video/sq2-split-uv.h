#pragma once
#include <stdint.h>
#include <stddef.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
static inline void sq2_copy_row(uint8_t* dst,const uint8_t* src,size_t bytes){
 size_t x=0;
#if defined(__aarch64__)
 for(;bytes-x>=64;x+=64){
  auto a=vld1q_u8(src+x),b=vld1q_u8(src+x+16),c=vld1q_u8(src+x+32),d=vld1q_u8(src+x+48);
  vst1q_u8(dst+x,a);vst1q_u8(dst+x+16,b);vst1q_u8(dst+x+32,c);vst1q_u8(dst+x+48,d);
 }
 for(;bytes-x>=16;x+=16)vst1q_u8(dst+x,vld1q_u8(src+x));
#endif
 for(;x<bytes;++x)dst[x]=src[x];
}
static inline void sq2_swap_uv(const uint8_t* src,uint8_t* dst,size_t count){
 size_t x=0;
#if defined(__aarch64__)
 for(;count-x>=8;x+=8)vst1q_u8(dst+2*x,vrev16q_u8(vld1q_u8(src+2*x)));
#endif
 for(;x<count;++x){dst[2*x]=src[2*x+1];dst[2*x+1]=src[2*x];}
}
// Exactly count interleaved UV pairs, including an unaligned short tail.
static inline void sq2_split_uv(const uint8_t* src,uint8_t* u,uint8_t* v,size_t count){
 size_t x=0;
#if defined(__aarch64__)
 for(;count-x>=16;x+=16){auto pair=vld2q_u8(src+2*x);vst1q_u8(u+x,pair.val[0]);vst1q_u8(v+x,pair.val[1]);}
#endif
 for(;x<count;++x){u[x]=src[2*x];v[x]=src[2*x+1];}
}
