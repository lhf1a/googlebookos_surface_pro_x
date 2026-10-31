#!/usr/bin/env python3
"""Exercise the actual SQ2 heatmap repair with recorded-shape and touch fixtures."""
from pathlib import Path
import subprocess
import sys
import tempfile

src = Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='sq2-heatmap-test-') as td:
    d = Path(td)
    (d / 'test.cpp').write_text(r'''
#include <core/generic/sq2-heatmap.hpp>
#include <cassert>
#include <iostream>
#include <random>
using namespace iptsd;
using namespace iptsd::core;
// Original full-copy implementation: independent behavioral oracle.
void reference(Image<f64> &image) {
    const Image<f64> original = image;
    for (const auto [row,col] : std::array<std::pair<int,int>,4>{{{0,0},{0,67},{1,0},{2,0}}}) {
        std::array<f64,8> neighbors {};
        std::size_t n=0;
        for(int y=std::max(0,row-1);y<=row+1;++y)
            for(int x=std::max(0,col-1);x<=std::min(67,col+1);++x)
                if(y!=row || x!=col) neighbors[n++]=original(y,x);
        std::nth_element(neighbors.begin(),neighbors.begin()+n/2,neighbors.begin()+n);
        const auto median=neighbors[n/2];
        if(original(row,col)>=239.0/255.0 && original(row,col)-median>40.0/255.0)
            image(row,col)=median;
    }
}
int main() {
    DeviceInfo info; info.vendor=0x045e; info.product=0x0c21;
    info.type=ipts::Device::Type::Touchscreen;
    Image<f64> idle=Image<f64>::Constant(46,68,75.0/255.0);
    idle(0,0)=1; idle(0,67)=1; idle(1,0)=1; idle(2,0)=249.0/255.0;
    auto repaired=idle;
    repair_sq2_heatmap(info,repaired);
    assert((repaired.array()-75.0/255.0).abs().maxCoeff()<1e-12);
    // A supported real contact at the corner survives; interior data is untouched.
    auto finger=idle;
    finger.block(0,0,4,4).setConstant(1.0);
    finger(20,30)=0.9;
    auto before=finger;
    repair_sq2_heatmap(info,finger);
    assert(finger.block(0,0,4,4).isApprox(before.block(0,0,4,4)));
    assert(finger(20,30)==0.9);
    // No changes on unrelated hardware, touchpads, or unexpected report shapes.
    auto other=info; other.product=0x1234;
    auto unchanged=idle; repair_sq2_heatmap(other,unchanged);
    assert(unchanged.isApprox(idle));
    other=info; other.type=ipts::Device::Type::Touchpad;
    repair_sq2_heatmap(other,unchanged); assert(unchanged.isApprox(idle));
    Image<f64> small=Image<f64>::Ones(2,2);
    repair_sq2_heatmap(info,small); assert(small.isOnes());
    // Unsaturated readings are not spatially filtered.
    auto weak=Image<f64>::Constant(46,68,75.0/255.0).eval();
    weak(0,0)=200.0/255.0; before=weak;
    repair_sq2_heatmap(info,weak); assert(weak.isApprox(before));
    // Compare all cells exactly, including adjacent spikes and threshold values.
    std::mt19937 random(0x0c21);
    for(int frame=0;frame<2000;++frame) {
        Image<f64> input(46,68);
        for(int y=0;y<46;++y) for(int x=0;x<68;++x)
            input(y,x)=(random()%256)/255.0;
        input(0,0)=(238+frame%3)/255.0;
        auto expected=input;
        reference(expected);
        repair_sq2_heatmap(info,input);
        assert((input.array()==expected.array()).all());
    }
    std::cout << "PASS: edge-spike repair, supported corner touch, unchanged interior, device/geometry guards\n";
}
''')
    includes = [src/'src', src/'subprojects/eigen-5.0.1', src/'subprojects/GSL-4.2.2/include', src/'subprojects/fmt-12.0.0/include']
    subprocess.run(['aarch64-linux-gnu-g++', '-static', '-std=c++17', '-O2', '-DFMT_HEADER_ONLY',
                    *[f'-I{x}' for x in includes], str(d/'test.cpp'), '-o', str(d/'test')], check=True)
    subprocess.run(['qemu-aarch64', str(d/'test')], check=True)
