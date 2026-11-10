// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "adaptive-noise.hpp"
#include "tap-runtime.hpp"
#include <chrono>
#include <fstream>
#include <cstdio>
#include <thread>
#include <mutex>
#include <condition_variable>
namespace surface_touch {
class NoiseRuntime {
    TapRuntime tap;AdaptiveNoise learner;Profile active;std::string device,nonce,last,lastStatus;
    long probeUntil=0;long published=0;const std::string root="/run/iptsd-noise/";
    std::thread worker;std::mutex mutex;std::condition_variable wake;bool stopping=false;
    bool noiseBypass=false,tapBypass=false,baselineProbe=false;
    int vendor=0,product=0,rows=0,cols=0;
    static long now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    static std::string read(const std::string &p){std::ifstream f(p);std::string s((std::istreambuf_iterator<char>(f)),{});return s.size()<=Profile::MAX_TEXT?s:"";}
    static void write(const std::string &p,const std::string &s){std::ofstream f(p+".new");if(f){f<<s;f.close();std::rename((p+".new").c_str(),p.c_str());}}
    void tick(long time) {
        const std::string command=read(root+"command");
        if(!command.empty() && command!=last){
            last=command;std::istringstream in(command);std::string op,id,n,extra;
            if(in>>op>>id>>n && !(in>>extra) && identity(id) && n.size()<=64 && !n.empty() && n.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==std::string::npos){
                device=id;nonce=n;
                tap.command(op,id,n);
                if(op=="taplearn"){noiseBypass=tapBypass=baselineProbe=false;probeUntil=time+600000;learner.learning=false;}
                if(op=="tapreload")tapBypass=false;
                try {
                    if(op=="learn" || op=="slide" || op=="learn-test" || op=="slide-test") {
                        probeUntil=time+600000;baselineProbe=noiseBypass=tapBypass=false; // Learn residuals with all accepted rules active.
                        std::remove((root+"candidate").c_str());learner.begin(id,vendor,product,rows,cols,time,op=="slide"||op=="slide-test");
                    }
                    else if(op=="reload"){
                        probeUntil=0;noiseBypass=tapBypass=baselineProbe=false;learner.learning=false;std::string s=read(root+"active");
                        active=s.empty()?Profile{}:Profile::parse(s,id);learner.state="applied";
                    }
                }catch(...){learner.learning=false;learner.state="error";}
            }
        }
        if(probeUntil && time>=probeUntil){
            probeUntil=0;noiseBypass=tapBypass=baselineProbe=false;learner.learning=false;learner.state="expired";
            tap.command("tapreload",device,nonce);std::remove((root+"candidate").c_str());
        }
        learner.finish(time);
        const std::string status=nonce+" "+learner.state;
        // Control-file/timer worker only, never HID IO. No idle status rewrite loop.
        if(!nonce.empty() && (status!=lastStatus || (learner.learning && time-published>=1000))){
            if(learner.state=="ready" || (learner.state=="line_noise"&&learner.result.band())){
                try{auto candidate=AdaptiveNoise::refine(active,learner.result);
                    write(root+"candidate",candidate.encode());
                }catch(...){learner.state="unresolved";std::remove((root+"candidate").c_str());}
            }
            write(root+"status",nonce+" "+learner.state+" "+std::to_string(learner.frames)+" "+std::to_string(learner.result.cells.size())+"\n"+learner.diagnostics()+"probe_mode="+(baselineProbe?"baseline":"candidate")+"\n");
            lastStatus=status;published=time;
        }
    }
public:
    NoiseRuntime()=default;
    NoiseRuntime(const NoiseRuntime&)=delete;NoiseRuntime& operator=(const NoiseRuntime&)=delete;
    ~NoiseRuntime(){if(worker.joinable()){{std::lock_guard<std::mutex> l(mutex);stopping=true;}wake.notify_all();worker.join();}}
    template<class Info> void configure(const Info &info) {
        if(!info.is_touchscreen() || !info.meta.has_value() || worker.joinable())return;
        vendor=info.vendor;product=info.product;rows=info.meta->rows;cols=info.meta->columns;tap.configure(info);
        write(root+"version","7\n");
        worker=std::thread([this]{std::unique_lock<std::mutex> l(mutex);while(!stopping){tick(now());wake.wait_for(l,std::chrono::milliseconds(200),[this]{return stopping;});}});
    }
    template<class Info,class Image,class Contacts,class Config> void repair_contacts(const Info &info,const Image &im,Contacts &contacts,const Config &cfg) {std::lock_guard<std::mutex> l(mutex);tap.process(info,im,contacts,cfg,now(),tapBypass);}
    template<class Info,class Image> void process(const Info &info,Image &im) {
        if(!info.is_touchscreen())return;
        std::lock_guard<std::mutex> l(mutex);
        if(!noiseBypass)AdaptiveNoise::apply(active,device,info.vendor,info.product,im);
        // Retest the remaining signal, not the already repaired raw defect.
        learner.observe(im,now());
    }
};
}
