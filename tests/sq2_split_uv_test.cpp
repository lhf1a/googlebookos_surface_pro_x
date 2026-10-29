#include "sq2-split-uv.h"
#include <vector>
#include <cassert>
#include <cstdio>
int main(){
 for(size_t n=0;n<=2050;++n)for(size_t offset=0;offset<16;++offset){
  std::vector<uint8_t> src(2*n+offset),u(n+32,0xa5),v(n+32,0xa5);
  for(size_t x=0;x<n;++x){src[offset+2*x]=(x*17+3)&255;src[offset+2*x+1]=(x*31+19)&255;}
  sq2_split_uv(src.data()+offset,u.data()+16,v.data()+16,n);
  for(size_t x=0;x<n;++x){assert(u[x+16]==((x*17+3)&255));assert(v[x+16]==((x*31+19)&255));}
  for(size_t x=0;x<16;++x){assert(u[x]==0xa5&&v[x]==0xa5);assert(u[n+16+x]==0xa5&&v[n+16+x]==0xa5);}
  std::vector<uint8_t> vu(2*n+32,0xa5);
  sq2_swap_uv(src.data()+offset,vu.data()+16,n);
  for(size_t x=0;x<n;++x){assert(vu[16+2*x]==((x*31+19)&255));assert(vu[17+2*x]==((x*17+3)&255));}
  for(size_t x=0;x<16;++x){assert(vu[x]==0xa5&&vu[2*n+16+x]==0xa5);}
  std::vector<uint8_t> copied(2*n+offset+32,0xa5);
  sq2_copy_row(copied.data()+16+offset,src.data()+offset,2*n);
  for(size_t x=0;x<2*n;++x)assert(copied[16+offset+x]==src[offset+x]);
  for(size_t x=0;x<16;++x){assert(copied[x+offset]==0xa5&&copied[16+offset+2*n+x]==0xa5);}
 }
 puts("split UV lengths/alignment/tails PASS");
}
