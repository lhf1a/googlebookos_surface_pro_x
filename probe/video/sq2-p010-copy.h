#pragma once
#include "sq2-split-uv.h"
// Caller validates P010 depth, byte strides, dimensions and mapped capacities.
// Copy visible samples only; retain the scalar path for unusual valid layouts.
static inline void sq2_copy_p010(const uint8_t* src,size_t stride,size_t uv,
 unsigned width,unsigned height,uint8_t* const p[3],const size_t rows[3],const size_t cols[3]){
 for(unsigned y=0;y<height;++y){
  if(cols[0]==2)sq2_copy_row(p[0]+y*rows[0],src+y*stride,size_t(width)*2);
  else for(unsigned x=0;x<width;++x)for(unsigned b=0;b<2;++b)
   p[0][y*rows[0]+x*cols[0]+b]=src[y*stride+2*x+b];
 }
 if(cols[1]==4&&cols[2]==4&&rows[1]==rows[2]&&uintptr_t(p[2])==uintptr_t(p[1])+2){
  for(unsigned y=0;y<height/2;++y)sq2_copy_row(p[1]+y*rows[1],src+uv+y*stride,size_t(width)*2);
 }else for(unsigned plane=1;plane<3;++plane)for(unsigned y=0;y<height/2;++y)
  for(unsigned x=0;x<width/2;++x)for(unsigned b=0;b<2;++b)
   p[plane][y*rows[plane]+x*cols[plane]+b]=src[uv+y*stride+4*x+2*(plane-1)+b];
}
