#!/usr/bin/env python3
"""Run the actual patched TouchDevice with a recording sink; no hardware access."""
from pathlib import Path
import argparse,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('--assert-coherent',action='store_true');p.add_argument('--dependencies',type=Path,default=Path('/root/.cache/sq2-probe-build/iptsd-3.1.1/subprojects'));a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='surface-state-test-') as td:
 d=Path(td);(d/'touch.hpp').write_bytes((a.source/'src/apps/daemon/touch.hpp').read_bytes())
 h=a.source/'src/apps/daemon/sq2-touch-calibration.hpp'
 if h.exists():(d/h.name).write_bytes(h.read_bytes())
 (d/'uinput-device.hpp').write_text(r"""
#pragma once
#include <vector>
#include <string>
namespace iptsd::apps::daemon {
struct Event{int type,code,value;};struct UinputDevice {
 inline static std::vector<Event> events;
 void set_name(const std::string&){} void set_vendor(int){} void set_product(int){}
 void set_evbit(int){} void set_keybit(int){} void set_propbit(int){}
 void set_absinfo(int,int,int,int){} void create(){}
 void emit(int t,int c,int v){events.push_back({t,c,v});}
};}
""")
 (d/'main.cpp').write_text(r"""
#include "touch.hpp"
#include <iostream>
#include <map>
#include <cassert>
using namespace iptsd;using namespace iptsd::apps::daemon;using C=contacts::Contact<f64>;
C finger(usize id,bool stable=true,bool valid=true){C c;c.index=id;c.stable=stable;c.valid=valid;c.mean={.4,.5};c.size={.02,.02};c.normalized=true;return c;}
struct Sink{int slot=0,btn=0,downs=0,inconsistent=0,peak=0;std::map<int,int> slots;
 void frame(){for(auto e:UinputDevice::events){if(e.type==EV_ABS&&e.code==ABS_MT_SLOT)slot=e.value;
 if(e.type==EV_ABS&&e.code==ABS_MT_TRACKING_ID){if(e.value<0)slots.erase(slot);else slots[slot]=e.value;}
 if(e.type==EV_KEY&&e.code==BTN_TOUCH){if(e.value&&!btn)downs++;btn=e.value;}}
 peak=std::max(peak,(int)slots.size());if(bool(btn)!=!slots.empty())inconsistent++;}
};
int main(int argc,char**){bool enforce=argc>1;
 for(int product:{0x0996,0x0c21,0x1234}){
  core::Config cfg;cfg.width=27.4;cfg.height=18.5;core::DeviceInfo info;info.vendor=product==0x1234?0x1234:0x045e;info.product=product;info.type=ipts::Device::Type::Touchscreen;
  TouchDevice t(cfg,info);Sink s;
  auto run=[&](std::vector<C> c){UinputDevice::events.clear();t.update(c);s.frame();};
  run({finger(0)});run({finger(0,false)});run({finger(0)});run({});
  std::cout<<"product="<<std::hex<<product<<std::dec<<" single_physical_sequence_downs="<<s.downs<<" inconsistent_frames="<<s.inconsistent;
  if(enforce)assert(s.downs==1&&s.inconsistent==0&&s.slots.empty()&&s.btn==0);
  run({finger(0),finger(7)});run({finger(7)});run({finger(7,false,false)});run({});
  int invalidSlots=s.slots.size(),invalidBtn=s.btn;
  run({finger(5,false)});int phantom=s.slots.size();run({finger(5)});run({});
  std::cout<<" genuine_multitouch_peak="<<s.peak<<" invalid_remaining="<<invalidSlots<<" invalid_btn="<<invalidBtn<<" phantom_new_unstable="<<phantom<<" immediate_release_slots="<<s.slots.size()<<"\n";
  if(enforce)assert(s.peak==2&&invalidSlots==0&&invalidBtn==0&&phantom==0&&s.slots.empty()&&s.btn==0);
 }
}
""")
 includes=[a.source/'src',a.dependencies/'eigen-5.0.1',a.dependencies/'GSL-4.2.2/include',a.dependencies/'fmt-12.0.0/include']
 subprocess.run(['g++','-std=c++20','-O1','-DFMT_HEADER_ONLY',*[f'-I{x}' for x in includes],str(d/'main.cpp'),'-o',str(d/'probe')],check=True)
 subprocess.run([str(d/'probe')]+(['check'] if a.assert_coherent else []),check=True)
