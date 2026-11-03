// SPDX-License-Identifier: GPL-2.0-or-later
// Generic, evidence-gated spatial continuity. Never time-only debounce.
#pragma once
#include "adaptive-noise.hpp"
#include <set>
#include <limits>
namespace surface_touch {
struct TapPolicy {
    std::string device;int vendor=0,product=0,rows=0,cols=0,frames=0;
    std::string encode()const {return "SPXTAP1 "+device+" "+std::to_string(vendor)+" "+std::to_string(product)+" "+std::to_string(rows)+" "+std::to_string(cols)+" "+std::to_string(frames)+"\n";}
    static TapPolicy parse(const std::string &s,const std::string &id){TapPolicy p;std::string tag,extra;std::istringstream in(s);
      if(s.size()>512||!identity(id)||!(in>>tag>>p.device>>p.vendor>>p.product>>p.rows>>p.cols>>p.frames)||tag!="SPXTAP1"||p.device!=id||p.vendor<1||p.vendor>65535||p.product<1||p.product>65535||p.rows<3||p.rows>256||p.cols<3||p.cols>256||p.frames<0||p.frames>6||in>>extra)throw std::runtime_error("tap policy");
      return p;}
};
struct Core {std::set<int> cells;double x=0,y=0,peak=0;};
// A single compact high-contrast core is necessary, not sufficient, for repair.
template<class Image> std::vector<Core> cores(const Image &im){
    std::vector<double> values;values.reserve(im.size());
    for(int r=0;r<im.rows();r++)for(int c=0;c<im.cols();c++){double v=im(r,c);if(!std::isfinite(v))return {};values.push_back(v);}
    double bg=AdaptiveNoise::median(values);for(double &v:values)v=std::abs(v-bg);
    const double gate=std::max(60./255.,8*AdaptiveNoise::median(values));std::set<int> todo;
    for(int r=0;r<im.rows();r++)for(int c=0;c<im.cols();c++)if(im(r,c)-bg>=gate)todo.insert(r*im.cols()+c);
    std::vector<Core> out;
    while(!todo.empty()) {Core b;std::vector<int> q{*todo.begin()};todo.erase(q[0]);b.cells.insert(q[0]);
      while(!q.empty()){int i=q.back();q.pop_back();int r=i/im.cols(),c=i%im.cols();b.peak=std::max(b.peak,double(im(r,c)-bg));b.x+=c;b.y+=r;
        for(auto d:std::array<std::pair<int,int>,4>{{{-1,0},{1,0},{0,-1},{0,1}}}){int rr=r+d.first,cc=c+d.second,j=rr*im.cols()+cc;if(rr>=0&&rr<im.rows()&&cc>=0&&cc<im.cols()&&todo.erase(j)){b.cells.insert(j);q.push_back(j);}}}
      b.x/=b.cells.size();b.y/=b.cells.size();out.push_back(b);
    }return out;
}
template<class Contact> class CoreContinuity {
    bool tracked=false;Contact last{};Core previous{};int age=0,burst=0;long previousTime=0,burstStart=0;
public:
    int observations=0,repairable=0,maxBurst=0,applied=0;
    void reset(){tracked=false;age=burst=0;previousTime=burstStart=0;}
    void begin(){reset();observations=repairable=maxBurst=applied=0;}
    template<class Image> bool step(const Image &im,std::vector<Contact>&contacts,long time,int allowed,bool apply,double sizeLimit,double diagonal){
      auto blobs=cores(im);
      // Any second component/contact is ambiguous: leave genuine multitouch alone.
      if(blobs.size()!=1||contacts.size()>1){reset();return false;}
      Core b=blobs[0];if(b.cells.size()<2||b.cells.size()>24){reset();return false;}
      int minx=im.cols(),maxx=0,miny=im.rows(),maxy=0;
      for(int j:b.cells){minx=std::min(minx,j%int(im.cols()));maxx=std::max(maxx,j%int(im.cols()));miny=std::min(miny,j/int(im.cols()));maxy=std::max(maxy,j/int(im.cols()));}
      // Keep the existing physical size envelope; never turn a broad palm into a finger.
      double span=std::max(double(maxx-minx+1)/im.cols(),double(maxy-miny+1)/im.rows());
      if(span*diagonal>sizeLimit){reset();return false;}
      bool fresh=!tracked||(time>=previousTime&&time-previousTime<=25);
      int overlap=0;for(int i:b.cells)overlap+=previous.cells.count(i);
      bool linked=tracked&&fresh&&overlap>=std::max(1,int(previous.cells.size()+1)/2)&&
          b.cells.size()*2>=previous.cells.size()&&b.cells.size()<=previous.cells.size()*2&&
          std::hypot(b.x-previous.x,b.y-previous.y)<=1.0&&b.peak>=previous.peak*.60;
      bool normal=contacts.size()==1&&contacts[0].index.has_value()&&contacts[0].valid.value_or(true);
      if(normal){
        if(!linked){age=0;burst=0;}
        if(contacts[0].stable.value_or(true)){last=contacts[0];tracked=true;}
        if(tracked){age++;observations++;}previous=b;previousTime=time;burst=0;return false;
      }
      // No old coordinates are retained unless this exact frame supports the same core.
      if(!linked||age<2||!last.index.has_value()||allowed<1||
         (!contacts.empty()&&contacts[0].index!=last.index)){reset();return false;}
      if(burst==0)burstStart=time;
      if(++burst>allowed||time-burstStart>60){reset();return false;}
      Contact restored=last;restored.mean.x()+=(b.x-previous.x)/double(im.cols()-1);restored.mean.y()+=(b.y-previous.y)/double(im.rows()-1);restored.valid=true;restored.stable=true;
      if(!std::isfinite(restored.mean.x())||!std::isfinite(restored.mean.y())||restored.mean.x()<0||restored.mean.x()>1||restored.mean.y()<0||restored.mean.y()>1){reset();return false;}
      repairable++;maxBurst=std::max(maxBurst,burst);if(apply){contacts.assign(1,restored);applied++;}
      last=restored;previous=b;previousTime=time;return true;
    }
};
}
