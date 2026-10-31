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
    calibration = src/'src/apps/daemon/sq2-touch-calibration.hpp'
    calibration_flags = []
    if calibration.exists():
        (d/calibration.name).write_text(calibration.read_text())
        calibration_flags = ['-DSQ2_CALIBRATION_TEST']
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
#ifdef SQ2_CALIBRATION_TEST
    core::Config plain=cfg;
    cfg.touchscreen_calibration_x="0:0 .9:.9 .995:.98 1:1";
    cfg.touchscreen_calibration_y=cfg.touchscreen_calibration_x;
    ipts::Metadata meta{}; meta.rows=46; meta.columns=68; info.meta=meta;
    TouchDevice original(plain,info), calibrated(cfg,info);
    auto edge=finger(3);edge.mean={.995,.995};
    auto unstable=edge;unstable.stable=false;unstable.mean={.999,.999};
    auto invalid=edge;invalid.valid=false;
    auto second=finger(7);auto out=edge;out.mean.x()=2;
    std::vector<std::vector<Contact>> sequence={{edge},{unstable},{edge,second},{second},{invalid},{},{unstable},{edge},{out},{}};
    for(const auto& contacts:sequence){
        UinputDevice::events.clear();original.update(contacts);auto before=UinputDevice::events;
        UinputDevice::events.clear();calibrated.update(contacts);auto after=UinputDevice::events;
        assert(before.size()==after.size());
        for(size_t i=0;i<before.size();++i){
            assert(before[i].type==after[i].type && before[i].code==after[i].code);
            const int code=before[i].code;
            const bool coordinate=before[i].type==EV_ABS &&
                (code==ABS_X || code==ABS_Y || code==ABS_MT_POSITION_X || code==ABS_MT_POSITION_Y);
            if(!coordinate)assert(before[i].value==after[i].value);
        }
    }
    frame(calibrated,{edge},1);
    assert(emitted(ABS_X,9408) && emitted(ABS_MT_POSITION_X,9408));
    assert(emitted(ABS_Y,7056) && emitted(ABS_MT_POSITION_Y,7056));
    core::Config spatial_cfg=cfg;
    spatial_cfg.touchscreen_calibration_x="0:0 .02:.03 .12:.12 1:1";
    spatial_cfg.touchscreen_calibration_y="0:0 .03:.04 .12:.12 1:1";
    spatial_cfg.touchscreen_calibration_x_top="0:0 1:1";
    TouchDevice spatial(spatial_cfg,info);
    auto left=finger(0);left.mean={.02,.03};frame(spatial,{left},1);
    assert(emitted(ABS_X,192) && emitted(ABS_MT_POSITION_X,192));
    assert(emitted(ABS_Y,288) && emitted(ABS_MT_POSITION_Y,288));
    left.mean={.02,.5};frame(spatial,{left},1);
    assert(emitted(ABS_X,288) && emitted(ABS_MT_POSITION_X,288));
    left.mean={.02,.2};frame(spatial,{left},1);
    assert(emitted(ABS_X,240) && emitted(ABS_MT_POSITION_X,240));
    info.meta->rows=45;TouchDevice wrong_grid(cfg,info);frame(wrong_grid,{edge},1);
    assert(emitted(ABS_X,9552) && emitted(ABS_Y,7164));
    info.meta->rows=46;info.product=0x0996;TouchDevice other_device(cfg,info);
    other_device.update({edge});UinputDevice::events.clear();other_device.update({edge});
    assert(emitted(ABS_X,9552) && emitted(ABS_Y,7164));
    std::cout << "PASS: calibration changes only coordinates, ST/MT agree, timing and device/grid scope preserved\n";
#endif
    std::cout << "PASS: first press, unstable hold, multi-finger handover, invalid/outside release, no phantom start, immediate release, disable and palm reset\n";
}
''')
    includes = [src/'src', src/'subprojects/eigen-5.0.1', src/'subprojects/GSL-4.2.2/include', src/'subprojects/fmt-12.0.0/include']
    subprocess.run(['aarch64-linux-gnu-g++','-static','-std=c++20','-O1','-DFMT_HEADER_ONLY', *calibration_flags, *[f'-I{x}' for x in includes],str(d/'test.cpp'),'-o',str(d/'test')],check=True)
    subprocess.run(['qemu-aarch64',str(d/'test')],check=True)
