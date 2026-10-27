#!/usr/bin/env python3
"""Exercise the actual TouchDevice class with a recording uinput sink."""
from pathlib import Path
import subprocess
import sys
import tempfile

src = Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='sq2-touch-test-') as td:
    d = Path(td)
    # Replace only the OS output boundary, preserving the production class.
    (d/'touch.hpp').write_text((src/'src/apps/daemon/touch.hpp').read_text())
    (d/'uinput-device.hpp').write_text(r'''
#pragma once
#include <vector>
#include <string>
namespace iptsd::apps::daemon {
struct Event { int type, code, value; };
struct UinputDevice {
    inline static std::vector<Event> events;
    void set_name(const std::string&) {}
    void set_vendor(int) {} void set_product(int) {}
    void set_evbit(int) {} void set_keybit(int) {} void set_propbit(int) {}
    void set_absinfo(int,int,int,int) {} void create() {}
    void emit(int t,int c,int v) { events.push_back({t,c,v}); }
};
}
''')
    (d/'test.cpp').write_text(r'''
#include "touch.hpp"
#include <cassert>
#include <iostream>
using namespace iptsd;
using namespace iptsd::apps::daemon;
using Contact = contacts::Contact<f64>;
Contact finger(usize id, bool stable=true, bool valid=true) {
    Contact c; c.index=id; c.stable=stable; c.valid=valid;
    c.mean={0.4,0.5}; c.size={0.02,0.02}; c.normalized=true; return c;
}
int button() {
    for(auto i=UinputDevice::events.rbegin(); i!=UinputDevice::events.rend(); ++i)
        if(i->type==EV_KEY && i->code==BTN_TOUCH) return i->value;
    return -1;
}
bool emitted(int code, int value) {
    for(auto e:UinputDevice::events) if(e.code==code && e.value==value) return true;
    return false;
}
void frame(TouchDevice& t, std::vector<Contact> c, int expected) {
    UinputDevice::events.clear(); t.update(c); assert(button()==expected);
}
int main() {
    core::Config cfg; cfg.width=27.4; cfg.height=18.5;
    core::DeviceInfo info; info.vendor=0x045e; info.product=0x0c21;
    info.type=ipts::Device::Type::Touchscreen;
    TouchDevice t(cfg,info);
    frame(t,{finger(3)},1); // Nonzero first ID must press in the first frame.
    frame(t,{finger(3,false)},1); // Unstable existing contact is still down.
    assert(!emitted(ABS_MT_TRACKING_ID,-1));
    frame(t,{finger(3)},1);
    frame(t,{finger(3),finger(7)},1);
    frame(t,{finger(7)},1); // Primary-finger handover must not click.
    frame(t,{finger(7,false,false)},0); // Invalid wins over unstable.
    assert(emitted(ABS_MT_TRACKING_ID,-1));
    frame(t,{},0);
    frame(t,{finger(5,false)},0); // Do not invent contacts from unstable noise.
    frame(t,{finger(5)},1);
    frame(t,{},0); // A real release has no added delay.
    frame(t,{finger(8)},1);
    UinputDevice::events.clear(); t.disable(); assert(button()==0);
    t.enable(); frame(t,{finger(8,false)},0);
    frame(t,{finger(8)},1);
    auto outside=finger(8,false); outside.mean.x()=2;
    frame(t,{outside},0);
    cfg.touchscreen_disable_on_palm=true;
    TouchDevice p(cfg,info);
    frame(p,{finger(0)},1);
    frame(p,{finger(0),finger(1,true,false)},0);
    frame(p,{finger(0,false)},0);
    frame(p,{finger(0)},1);
    std::cout << "PASS: first press, unstable hold, multi-finger handover, invalid/outside release, no phantom start, immediate release, disable and palm reset\n";
}
''')
    includes = [src/'src', src/'subprojects/eigen-5.0.1', src/'subprojects/GSL-4.2.2/include', src/'subprojects/fmt-12.0.0/include']
    subprocess.run(['aarch64-linux-gnu-g++','-static','-std=c++20','-O1','-DFMT_HEADER_ONLY', *[f'-I{x}' for x in includes],str(d/'test.cpp'),'-o',str(d/'test')],check=True)
    subprocess.run(['qemu-aarch64',str(d/'test')],check=True)
