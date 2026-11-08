#pragma once
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
struct Quest2CodecCaps {
 enum Codec { Avc, Hevc, Vp8, Vp9, Mpeg2 };
 Codec codec=Avc;
 struct Limit {uint32_t min,max,step;};
 struct Profile {uint32_t profile,level;};
 std::map<uint32_t,Limit> limits;
 std::vector<Profile> profiles;
 uint32_t sessions=0;
 bool nv12=false,p010=false;
 const char* suffix() const {switch(codec){case Avc:return "avc";case Hevc:return "hevc";case Vp8:return "vp8";case Vp9:return "vp9";case Mpeg2:return "mpeg2";}throw std::runtime_error("unknown codec");}
 std::string name() const {return std::string("OMX.qcom.video.decoder.")+suffix();}
 std::string mime() const {return codec==Vp8||codec==Vp9?std::string("video/x-vnd.on2.")+suffix():std::string("video/")+suffix();}
 bool tenBit(uint32_t profile) const {return (codec==Hevc&&profile==2)||(codec==Vp9&&profile==4);}
 static uint32_t omxProfile(uint32_t hfi) {
  switch(hfi){case 1:return 1;case 2:return 2;case 4:return 8;
   case 32:return 0x10000;case 64:return 0x80000;default:return 0;}
 }
 static Quest2CodecCaps parse(std::istream& in,Codec codec=Avc) {
  Quest2CodecCaps c;c.codec=codec;std::string line;bool version=false,end=false;size_t bytes=0,records=0;
  while(std::getline(in,line)) {
   bytes+=line.size()+1;if(bytes>4096||end)throw std::runtime_error("invalid capability length/end");
   if(line.find('-')!=std::string::npos)throw std::runtime_error("negative capability value");
   uint32_t a,b,d,e;int used=0;
   if(line=="version=1"&&!version){version=true;continue;}
   if(!version)throw std::runtime_error("missing capability version");
   if(line=="end=1"){end=true;continue;}
   if(sscanf(line.c_str(),"sessions=%u%n",&a,&used)==1&&used==(int)line.size()&&!c.sessions&&a&&a<=64){c.sessions=a;continue;}
   used=0;
   if(sscanf(line.c_str(),"cap=%u,%u,%u,%u%n",&a,&b,&d,&e,&used)==4&&used==(int)line.size()){
    if(c.limits.size()>=32||c.limits.count(a)||b>d||!e)throw std::runtime_error("invalid capability range");
    c.limits.emplace(a,Limit{b,d,e});continue;
   }
   used=0;
   if(sscanf(line.c_str(),"pl=%u,%u%n",&a,&b,&used)==2&&used==(int)line.size()){
    if(++records>32)throw std::runtime_error("too many profiles");
    uint32_t p=a,l=b;
    auto power=[](uint32_t v,uint32_t max){return v&&!(v&(v-1))&&v<=max;};
    switch(codec) {
     case Avc:
      if(!power(b,0x80000))throw std::runtime_error("unsupported AVC level encoding");
      p=omxProfile(a);
      if(!p){if(a==8||a==16)continue;throw std::runtime_error("unknown AVC profile");}
      break;
     case Hevc: {
      uint32_t tier=b>>28,level=b&0x0fffffff;
      if((a!=1&&a!=2&&a!=4)||(tier!=1&&tier!=2)||!power(level,0x1000))
       throw std::runtime_error("unknown HEVC profile/tier/level");
      unsigned bit=0;while((1u<<bit)!=level)++bit;
      l=1u<<(2*bit+(tier==2));break;
     }
     case Vp8:
      if(a!=1||!power(b,8))throw std::runtime_error("unknown VP8 profile/version");
      break;
     case Vp9:
      if((a!=1&&a!=4)||!power(b,0x800))throw std::runtime_error("unknown VP9 profile/level");
      // HFI skips level 5.2; Android includes it.
      l=b>=0x400?b<<1:b;break;
     case Mpeg2:
      if((a!=1&&a!=2)||!power(b,8))throw std::runtime_error("unknown MPEG2 profile/level");
      p=a==1?0:1;
      // HFI LL,ML,HL,H14 are 1,2,4,8; OMX LL,ML,H14,HL are 0..3.
      l=b==1?0:b==2?1:b==4?3:2;break;
    }
    for(const auto& v:c.profiles)
     if(v.profile==p&&(codec!=Vp8||v.level==l))throw std::runtime_error("duplicate codec profile");
    c.profiles.push_back({p,l});continue;
   }
   used=0;
   if(sscanf(line.c_str(),"fmt=%u,%u%n",&a,&b,&used)==2&&used==(int)line.size()){
    if(a==3&&b==2)c.nv12=true;
    if(a==3&&b==0x4003)c.p010=true;
    continue;
   }
   throw std::runtime_error("malformed capability record");
  }
  if(!end||!c.sessions||c.profiles.empty()||!c.nv12)throw std::runtime_error("incomplete decoder capabilities");
  for(uint32_t id:{1u,2u,3u,4u,5u,8u})if(!c.limits.count(id)||!c.limits.at(id).max)throw std::runtime_error("missing required decoder limit");
  if(c.limits.at(1).max>65536||c.limits.at(2).max>65536||c.limits.at(1).step>65536||c.limits.at(2).step>65536)throw std::runtime_error("invalid raster bounds");
  return c;
 }
 static Quest2CodecCaps read(Codec codec=Avc) {
  Quest2CodecCaps key;key.codec=codec;
  std::ifstream in(std::string("/sys/module/venus_core/parameters/sq2_")+key.suffix()+"_caps");
  if(!in)throw std::runtime_error("firmware decoder capabilities unavailable");
  return parse(in,codec);
 }
 std::string xmlEntry() const {
  const auto& w=limits.at(1);const auto& h=limits.at(2);
  // Linear 4:2:0 requires even dimensions; firmware steps remain authoritative.
  uint32_t wa=w.step%2?w.step*2:w.step,ha=h.step%2?h.step*2:h.step;
  if(!wa||!ha||(wa&(wa-1))||(ha&(ha-1)))throw std::runtime_error("non-power-of-two alignment");
  std::ostringstream out;
  out<<" <MediaCodec name=\""<<name()<<"\" type=\""<<mime()<<"\" rank=\"256\">\n"
     <<"  <Quirk name=\"requires-allocate-on-input-ports\" />\n"
     <<"  <Quirk name=\"requires-allocate-on-output-ports\" />\n"
     <<"  <Limit name=\"size\" min=\""<<((w.min+wa-1)/wa*wa)<<"x"<<((h.min+ha-1)/ha*ha)
     <<"\" max=\""<<(w.max/wa*wa)<<"x"<<(h.max/ha*ha)<<"\" />\n"
     <<"  <Limit name=\"alignment\" value=\""<<wa<<"x"<<ha<<"\" />\n"
     <<"  <Limit name=\"block-size\" value=\"16x16\" />\n";
  for(const auto& item:std::map<uint32_t,const char*>{{3,"block-count"},{4,"blocks-per-second"},{5,"frame-rate"},{8,"bitrate"}}){
   const auto& v=limits.at(item.first);
   out<<"  <Limit name=\""<<item.second<<"\" range=\""<<v.min<<"-"<<v.max<<"\" />\n";
  }
  out<<"  <Limit name=\"concurrent-instances\" max=\""<<sessions<<"\" />\n"
     <<" </MediaCodec>\n";
  return out.str();
 }
 std::string xml() const {return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<MediaCodecs><Decoders>\n"+xmlEntry()+"</Decoders></MediaCodecs>\n";}
};
