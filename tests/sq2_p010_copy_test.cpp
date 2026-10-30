#include "sq2-p010-copy.h"
#include <vector>
#include <cassert>
#include <cstdio>
int main(){
 for(unsigned w: {2u,14u,32u,34u,128u,3840u})for(unsigned h:{2u,6u})
 for(unsigned off=0;off<16;++off)for(unsigned mode=0;mode<4;++mode){
  size_t stride=w*2+24,uv=stride*(h+2);
  std::vector<uint8_t> src(off+uv+stride*h/2+32);
  for(size_t i=0;i<src.size();++i)src[i]=(i*37+19)&255;
  size_t cols[3]={mode==3?4u:2u,mode==0?2u:4u,mode==0?2u:4u};
  size_t rows[3]={w*cols[0]+16,w/2*cols[1]+16,w/2*cols[2]+16};
  size_t y=32+off,u=y+rows[0]*h+32,v=u+rows[1]*h/2+32;
  if(mode==1||mode==3)v=u+2;
  if(mode==2){v=u;u=v+2;}
  std::vector<uint8_t> a(y+rows[0]*h+rows[1]*h+256,0xa5),b=a;
  uint8_t* p[3]={a.data()+y,a.data()+u,a.data()+v};
  uint8_t* q[3]={b.data()+y,b.data()+u,b.data()+v};
  for(unsigned plane=0;plane<3;++plane)for(unsigned yy=0;yy<(plane?h/2:h);++yy)
   for(unsigned x=0;x<(plane?w/2:w);++x)for(unsigned byte=0;byte<2;++byte)
    q[plane][yy*rows[plane]+x*cols[plane]+byte]=src[off+(plane?uv:0)+yy*stride+(plane?4*x+2*(plane-1):2*x)+byte];
  sq2_copy_p010(src.data()+off,stride,uv,w,h,p,rows,cols);
  assert(a==b); // Includes all gaps, row padding and leading/trailing guards.
 }
 puts("P010_ROW_COPY_PLANAR_UV_VU_STRIDED_UNALIGNED_PADDING_PASS");
}
