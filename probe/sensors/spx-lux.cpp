// SPDX-License-Identifier: GPL-2.0-only
#include "spx-lux.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numeric>
namespace spx {
int LuxCalibration::integer(size_t o) const {
 return int(uint32_t(data[o])|(uint32_t(data[o+1])<<8)|(uint32_t(data[o+2])<<16)|(uint32_t(data[o+3])<<24));
}
float LuxCalibration::real(size_t o) const {uint32_t n=integer(o);float v;std::memcpy(&v,&n,4);return v;}
void LuxCalibration::real(size_t o,float v){uint32_t n;std::memcpy(&n,&v,4);for(int k=0;k<4;k++)data[o+k]=n>>(8*k);}
double LuxCalibration::linear(int ch,int raw) const {
 if(!raw)return 1.;
 int row=15;
 for(int j=1;j<16;j++)if(real(1124+20*j)>raw){row=j-1;break;}
 double a=real(1128+20*row+4*ch);
 if(row==15)return raw*a;
 double lo=real(1124+20*row),hi=real(1124+20*(row+1));
 double b=real(1128+20*(row+1)+4*ch);
 return raw*(a*(hi-raw)+b*(raw-lo))/(hi-lo);
}
float LuxCalibration::actualGain(int gain) const {
 for(int j=0;j<5;j++)if(gain<=integer(852+8*j))return real(856+8*j);
 return real(888);
}
bool LuxCalibration::load(const std::string& path) {
 ready=false;
 std::ifstream in(path,std::ios::binary);
 if(!in.read(reinterpret_cast<char*>(data.data()),data.size())||in.peek()!=EOF)return false;
 uint32_t crc=~0u;
 for(size_t j=0;j<1476;j++){crc^=data[j];for(int k=0;k<8;k++)crc=(crc>>1)^((0u-(crc&1))&0xedb88320);}
 if(crc!=uint32_t(integer(1476))||integer(0)!=1||integer(4)!=0||integer(892)!=1||integer(896)!=0)return false;
 const int dims[]={3,5,4,4,5,16};
 for(int j=0;j<6;j++)if(integer(28+j*4)!=dims[j])return false;
 for(int j=0;j<16;j++){
  if(!std::isfinite(real(1124+j*20))||(j&&real(1124+j*20)<=real(1124+(j-1)*20)))return false;
  for(int c=0;c<4;c++)if(!(real(1128+j*20+c*4)>0))return false;
 }
 for(int j=0;j<5;j++)if(integer(852+8*j)<=0||!(real(856+8*j)>0))return false;
 for(int j=1;j<5;j++)for(int off:{756,760})if(integer(off+20*j)<1||integer(off+20*j)>4)return false;
 // The file is the OEM model's golden default, never another unit's calibration.
 for(int bank=0;bank<2;bank++)for(int c=0;c<4;c++){
  int src=900+bank*72,dst=1052+bank*24+4*c;
  float sum=real(dst);
  for(int j=0;j<3;j++){
   int raw=integer(src+24*j+8+4*c);if(raw<0||raw>65535)return false;
   sum+=float(linear(c,raw));
  }
  float denominator=real(1092+16*bank+4*c);
  if(!(denominator>0))return false;
  float value=(sum/3)*(real(160+16*bank+4*c)/denominator);
  if(!(value>0)||!std::isfinite(value))return false;
  real(dst,value);
 }
 if(!(real(1056)>real(1080))||!(real(16)>0)||!(real(24)>0))return false;
 previous={35,45,6,0};ready=true;return true;
}
bool LuxCalibration::convert(int atime,int gain,const std::array<int,4>& input,float& lux){
 if(!ready||atime<1||atime>255||(gain!=1&&gain!=4&&gain!=16&&gain!=64&&gain!=128))return false;
 for(int v:input)if(v<0||v>=65535)return false;
 auto raw=input;int sum=std::accumulate(raw.begin(),raw.end(),0);
 if(gain==1)previous={35,45,6,0};
 else if(sum<80){int total=std::accumulate(previous.begin(),previous.end(),0);for(int c=0;c<4;c++)raw[c]=previous[c]*sum/total;}
 else previous=raw;
 int high=int(real(1056)),low=int(real(1080));std::array<double,4> n{};
 for(int c=0;c<4;c++){
  double value=linear(c,raw[c]);
  double hi=value*linear(c,integer(120+4*c))/real(1052+4*c);
  double lo=value*linear(c,integer(144+4*c))/real(1076+4*c);
  double w=std::clamp(double(high-raw[1])/(high-low),0.,1.);
  n[c]=hi*(1-w)+lo*w;
 }
 if(n[2]<=0||n[3]/n[2]>real(20))return false;
 int ref=float(n[1])>int(real(1080))?112:136;
 double scale=double(integer(ref)+1)/(atime+1)*actualGain(integer(ref+4))/actualGain(gain)*real(16);
 double features[]={double(float(n[0]))/float(n[2]),double(float(n[1]))/float(n[2]),float(n[2])*scale,double(float(n[3]))/float(n[2])};
 int category=1;
 if(!(float(n[3])>50&&features[3]>8)){
  double best=INFINITY;
  for(int j=1;j<5;j++){
   double a=features[integer(756+20*j)-1]-real(764+20*j);
   double b=features[integer(760+20*j)-1]-real(768+20*j);
   double distance=a*a+b*b;
   if(distance<best){best=distance;category=j+1;}
  }
 }
 double result=0;
 for(int c=0;c<4;c++)result+=real(4*(116+12*category+4+c))*n[c]*scale;
 if(!std::isfinite(result)||result<0||result>real(24))return false;
 lux=result;return true;
}
}
