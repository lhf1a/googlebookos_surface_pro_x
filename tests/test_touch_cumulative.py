"""Compile actual runtime headers; substitute only filesystem and Contact ABI fixture."""
from pathlib import Path
import subprocess,sys,tempfile

CPP=r'''
#include "noise-runtime.hpp"
#include <iostream>
#include <iomanip>
#include <optional>
using namespace surface_touch;
struct Meta {int rows=46,columns=68;};
struct Info {int vendor=1118,product=3105;std::optional<Meta> meta=Meta{};bool is_touchscreen()const{return true;}};
struct Grid {double v[46][68]{};int rows()const{return 46;}int cols()const{return 68;}int size()const{return 46*68;}double operator()(int r,int c)const{return v[r][c];}double&operator()(int r,int c){return v[r][c];}};
struct Config {double contacts_size_max=2.4,width=27,height=19;};
using Contact=iptsd::contacts::Contact<double>;
Grid blob(){Grid g;for(int r=20;r<22;r++)for(int c=30;c<32;c++)g(r,c)=.6;return g;}
int main(){
 std::string id(64,'a');Info info;Config cfg;NoiseRuntime n;n.tap.configure(info);
 n.vendor=info.vendor;n.product=info.product;n.rows=46;n.cols=68;
 Profile p;p.device=id;p.vendor=info.vendor;p.product=info.product;p.rows=46;p.cols=68;p.cells={{350,.2}};
 NoiseRuntime::write(n.root+"active",p.encode());
 TapPolicy old;old.device=id;old.vendor=info.vendor;old.product=info.product;old.rows=46;old.cols=68;old.frames=2;
 NoiseRuntime::write(n.root+"tap-active",old.encode());
 int nonce=0;long time=1000;bool pass=true;
 auto cmd=[&](std::string op){NoiseRuntime::write(n.root+"command",op+" "+id+" n"+std::to_string(++nonce));n.tick(time++);};
 auto noiseWorks=[&](){Grid g;g(5,10)=.9;n.process(info,g);return g(5,10)==0;};
 auto feed=[&](bool valid,long t){std::vector<Contact> c(1);c[0].valid=valid;n.tap.process(info,blob(),c,cfg,t,n.tapBypass);return c.size()==1&&c[0].valid.value_or(false);};
 for(auto mode:{"learn","slide","learn-test","slide-test","taplearn"}){
  cmd("reload");cmd(mode);bool noise=noiseWorks();
  for(int i=0;i<16;i++)feed(true,2000+i*10);
  bool tap=feed(false,2160);
  std::cout<<mode<<" noise="<<noise<<" old_tap="<<tap<<"\n";pass &= noise&&tap;
 }
 cmd("tapfinish");int kept=TapPolicy::parse(NoiseRuntime::read(n.root+"tap-candidate"),id).frames;
 std::cout<<"no_residual candidate_frames="<<kept<<"\n";pass &= kept==2;
 cmd("reload");cmd("taplearn");for(int i=0;i<16;i++)feed(true,3000+i*10);
 feed(false,3160);feed(false,3170);bool residualOutput=feed(false,3180);
 cmd("tapfinish");int grown=TapPolicy::parse(NoiseRuntime::read(n.root+"tap-candidate"),id).frames;
 std::cout<<"residual candidate_frames="<<grown<<" unapplied="<<!residualOutput<<"\n";pass &= grown==4&&!residualOutput;
 cmd("reload");cmd("taplearn");cmd("tapfinish");int empty=TapPolicy::parse(NoiseRuntime::read(n.root+"tap-candidate"),id).frames;
 std::cout<<"empty candidate_frames="<<empty<<"\n";pass &= empty==2;
 cmd("reload");cmd("slide");
 n.learner.result=p;n.learner.result.cells={{700,.2}};n.learner.state="ready";n.learner.learning=false;n.tick(time++);
 auto combined=Profile::parse(NoiseRuntime::read(n.root+"candidate"),id);
 bool unioned=combined.cells.size()==2&&combined.cells[0].index==350&&combined.cells[1].index==700;
 std::cout<<"noise_union="<<unioned<<"\n";pass &= unioned;
 cmd("reload");cmd("taplearn");for(int i=0;i<16;i++)feed(true,4000+i*10);
 std::vector<Contact> c;Grid quiet;n.tap.process(info,quiet,c,cfg,4160,n.tapBypass);
 bool released=c.empty();c.resize(2);c[1].index=1;n.tap.process(info,blob(),c,cfg,4170,n.tapBypass);
 bool multi=c.size()==2;std::cout<<"immediate_release="<<released<<" multitouch="<<multi<<"\n";pass &= released&&multi;
 cmd("reload");bool restored=noiseWorks();std::cout<<"reload_preserves_noise="<<restored<<"\n";pass &= restored;
 old.product++;NoiseRuntime::write(n.root+"tap-active",old.encode());cmd("reload");cmd("taplearn");cmd("tapfinish");
 int mismatch=TapPolicy::parse(NoiseRuntime::read(n.root+"tap-candidate"),id).frames;
 std::cout<<"wrong_hardware_inherited="<<(mismatch!=0)<<"\n";pass &= mismatch==0;
 std::cout<<"cumulative_contract="<<(pass?"PASS":"FAIL")<<"\n";return pass?0:1;
}
'''
CONTACT=r'''
#pragma once
#include <optional>
namespace iptsd::contacts {
template<class T> struct Contact {
 struct Vec {T a=.5,b=.5;T&x(){return a;}T&y(){return b;}} mean;
 std::optional<int> index=0;std::optional<bool> valid=true,stable=true;
};
}
'''
def run(source):
 with tempfile.TemporaryDirectory(prefix='touch-cumulative-') as td:
  d=Path(td);(d/'contacts').mkdir();(d/'contacts/contact.hpp').write_text(CONTACT)
  for name in ('adaptive-noise.hpp','tap-core.hpp','tap-runtime.hpp','noise-runtime.hpp'):
   text=(source/'probe/touch-calibration'/name).read_text()
   text=text.replace('class NoiseRuntime {','class NoiseRuntime {\npublic:')
   text=text.replace('/run/iptsd-noise/',str(d)+'/')
   (d/name).write_text(text)
  (d/'main.cpp').write_text(CPP)
  subprocess.run(['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie','-pthread','-I'+str(d),str(d/'main.cpp'),'-o',str(d/'check')],check=True)
  return subprocess.run([str(d/'check')]).returncode
import unittest
R=Path(__file__).resolve().parents[1]
class CumulativeRuntime(unittest.TestCase):
 def test_saved_rules_and_residual_learning(self):self.assertEqual(run(R),0)
if __name__=='__main__':
 if len(sys.argv)>1:sys.exit(run(Path(sys.argv[1]).resolve()))
 unittest.main()
