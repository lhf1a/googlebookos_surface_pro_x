// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "adaptive-noise.hpp"
#include <chrono>
#include <fstream>
#include <cstdio>
#include <thread>
#include <mutex>
#include <condition_variable>
namespace surface_touch {
class NoiseRuntime {
    AdaptiveNoise learner;Profile active;std::string device,nonce,last,lastStatus;
    long published=0;const std::string root="/run/iptsd-noise/";
    std::thread worker;std::mutex mutex;std::condition_variable wake;bool stopping=false;
    int vendor=0,product=0,rows=0,cols=0;
    static long now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    static std::string read(const std::string &p){std::ifstream f(p);std::string s((std::istreambuf_iterator<char>(f)),{});return s.size()<=4096?s:"";}
    static void write(const std::string &p,const std::string &s){std::ofstream f(p+".new");if(f){f<<s;f.close();std::rename((p+".new").c_str(),p.c_str());}}
    void tick(long time) {
        const std::string command=read(root+"command");
        if(!command.empty() && command!=last){
            last=command;std::istringstream in(command);std::string op,id,n,extra;
            if(in>>op>>id>>n && !(in>>extra) && identity(id) && n.size()<=64 && !n.empty() && n.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==std::string::npos){
                device=id;nonce=n;
                try {
                    if(op=="learn")learner.begin(id,vendor,product,rows,cols,time);
                    else if(op=="reload"){
                        learner.learning=false;std::string s=read(root+"active");
                        active=s.empty()?Profile{}:Profile::parse(s,id);learner.state="applied";
                    }
                }catch(...){learner.learning=false;learner.state="error";}
            }
        }
        learner.finish(time);
        const std::string status=nonce+" "+learner.state;
        // Control-file/timer worker only, never HID IO. No idle status rewrite loop.
        if(!nonce.empty() && (status!=lastStatus || (learner.learning && time-published>=1000))){
            if(learner.state=="ready")write(root+"candidate",learner.result.encode());
            write(root+"status",status+" "+std::to_string(learner.frames)+" "+std::to_string(learner.result.cells.size())+"\n"+learner.diagnostics());
            lastStatus=status;published=time;
        }
    }
public:
    NoiseRuntime()=default;
    NoiseRuntime(const NoiseRuntime&)=delete;NoiseRuntime& operator=(const NoiseRuntime&)=delete;
    ~NoiseRuntime(){if(worker.joinable()){{std::lock_guard<std::mutex> l(mutex);stopping=true;}wake.notify_all();worker.join();}}
    template<class Info> void configure(const Info &info) {
        if(!info.is_touchscreen() || !info.meta.has_value() || worker.joinable())return;
        vendor=info.vendor;product=info.product;rows=info.meta->rows;cols=info.meta->columns;
        write(root+"version","2\n");
        worker=std::thread([this]{std::unique_lock<std::mutex> l(mutex);while(!stopping){tick(now());wake.wait_for(l,std::chrono::milliseconds(200),[this]{return stopping;});}});
    }
    template<class Info,class Image> void process(const Info &info,Image &im) {
        if(!info.is_touchscreen())return;
        std::lock_guard<std::mutex> l(mutex);
        learner.observe(im,now());
        AdaptiveNoise::apply(active,device,info.vendor,info.product,im);
    }
};
}
