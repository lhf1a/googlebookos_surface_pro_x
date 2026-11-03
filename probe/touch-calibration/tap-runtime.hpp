// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "tap-core.hpp"
#include <contacts/contact.hpp>
#include <fstream>
#include <cstdio>
namespace surface_touch {
class TapRuntime {
    CoreContinuity<iptsd::contacts::Contact<double>> tracker;TapPolicy active{},identityProfile{};bool learning=false;std::string id,nonce;
    const std::string root="/run/iptsd-noise/";
    static std::string read(const std::string &p){std::ifstream f(p);return std::string((std::istreambuf_iterator<char>(f)),{});}
    static void write(const std::string &p,const std::string &s){std::ofstream f(p+".new");if(f){f<<s;f.close();std::rename((p+".new").c_str(),p.c_str());}}
public:
    template<class Info> void configure(const Info&info){identityProfile.vendor=info.vendor;identityProfile.product=info.product;if(info.meta){identityProfile.rows=info.meta->rows;identityProfile.cols=info.meta->columns;}}
    void command(const std::string &op,const std::string &device,const std::string &n){
      if(!identity(device))return;
      id=device;nonce=n;identityProfile.device=id;
      try {
        if(op=="reload"||op=="tapreload") {auto text=read(root+"tap-active");active=text.empty()?TapPolicy{}:TapPolicy::parse(text,id);learning=false;tracker.reset();}
        if(op=="taplearn"){learning=true;tracker.begin();std::remove((root+"tap-candidate").c_str());}
        if(op=="tapfinish") {if(!learning)throw std::runtime_error("not collecting");learning=false;
          TapPolicy candidate=identityProfile;candidate.frames=active.frames;
          if(tracker.observations>=15&&tracker.repairable>0)candidate.frames=std::max(candidate.frames,std::min(6,tracker.maxBurst+1));
          write(root+"tap-candidate",candidate.encode());}
        write(root+"tap-status",n+" "+(op=="tapfinish"?"ready":op=="taplearn"?"learning":"applied")+" "+std::to_string(tracker.observations)+" "+std::to_string(tracker.repairable)+" "+std::to_string(tracker.maxBurst)+"\n");
      }catch(...){learning=false;write(root+"tap-status",n+" error 0 0 0\n");}
    }
    template<class Info,class Image,class Config,class Contacts> void process(const Info &info,const Image&im,Contacts &contacts,const Config &cfg,long now){
      if(!info.is_touchscreen())return;
      bool enabled=active.device==id&&active.vendor==info.vendor&&active.product==info.product&&active.rows==im.rows()&&active.cols==im.cols()&&active.frames>0;
      if(!learning&&!enabled)return;
      tracker.step(im,contacts,now,learning?6:active.frames,enabled,cfg.contacts_size_max,std::hypot(cfg.width,cfg.height));
    }
};
}
