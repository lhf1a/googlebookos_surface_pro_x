"""Offline idle duration, residual refinement and rollback guards."""
from pathlib import Path
import subprocess, tempfile, unittest
R=Path(__file__).resolve().parents[1]

class Idle45(unittest.TestCase):
 def test_duration_residual_refinement_and_bounds(self):
  code=r'''
#include "adaptive-noise.hpp"
#include <cassert>
#include <iostream>
using namespace surface_touch;
struct Grid{std::vector<double> v=std::vector<double>(46*68,.01);int rows()const{return 46;}int cols()const{return 68;}double operator()(int r,int c)const{return v[r*68+c];}double&operator()(int r,int c){return v[r*68+c];}};
int main(){
 std::string id(64,'a');AdaptiveNoise a;a.begin(id,1118,2454,46,68,0);
 for(int i=0;i<450;i++){Grid g;g(20,30)=.9;a.observe(g,i*100);}
 assert(a.learning);a.finish(44999);assert(a.learning);a.finish(45000);assert(a.state=="ready");
 Profile p=a.result;AdaptiveNoise b;b.begin(id,1118,2454,46,68,0);
 for(int i=0;i<=450;i++){Grid g;g(20,30)=.9;g(25,40)=.8;AdaptiveNoise::apply(p,id,1118,2454,g);b.observe(g,i*100);}
 assert(b.state=="ready"&&b.result.cells.size()==1);auto next=AdaptiveNoise::refine(p,b.result);assert(next.cells.size()==2);
 AdaptiveNoise retest;retest.begin(id,1118,2454,46,68,0);
 for(int i=0;i<=450;i++){Grid g;g(20,30)=.9;g(25,40)=.8;AdaptiveNoise::apply(next,id,1118,2454,g);retest.observe(g,i*100);}
 assert(retest.state=="quiet");Grid real;for(int y=19;y<=21;y++)for(int x=29;x<=31;x++)real(y,x)=.9;
 assert(AdaptiveNoise::apply(next,id,1118,2454,real)==0);
 auto band=Profile::parse("SPXNOISE2 "+id+" 1118 2454 46 68 R 1 3",id);auto mixed=AdaptiveNoise::refine(next,band);assert(mixed.model());
 auto other=Profile::parse("SPXNOISE2 "+id+" 1118 2454 46 68 R 1 4",id);
 assert(AdaptiveNoise::refine(band,other).bandRows.size()==2);
 AdaptiveNoise no;no.begin(id,1118,2454,46,68,0);Grid g;for(int i=0;i<400;i++)no.observe(g,i);no.finish(45000);assert(no.state=="no_data");
 std::cout<<"duration45000=1 residual_refinement=1 quiet_after_two_rounds=1 real_touch_preserved=1 mixed_candidate_generated=1 missing_windows_not_passed=1\n";
}
'''
  with tempfile.TemporaryDirectory() as td:
   d=Path(td);(d/'test.cpp').write_text(code)
   subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(R/'probe/touch-calibration'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
   subprocess.run([str(d/'test')],check=True)
 def test_ui_and_original_rollback_are_preserved(self):
  s=(R/'probe/touch-calibration/MainActivity.java').read_text()
  for text in ['NOISE_MS=45000','+60000','已检测到','已应用修复，重新检查','继续优化','我确认当前效果可以','noisePassed=f[1].equals("quiet")&&noiseContacts==0','handler.removeCallbacks(noiseStep)']:
   self.assertIn(text,s)
  h=(R/'probe/test2/touch-calibration-boot').read_text()
  self.assertIn('if [ "$noise_preview" != 1 ]; then\n                        rm -f "$noise/previous"',h)
  self.assertIn('+ 600 ))',h)
  native=(R/'probe/touch-calibration/noise-runtime.hpp').read_text()
  self.assertLess(native.index('AdaptiveNoise::apply(active'),native.index('learner.observe(im'))

if __name__=='__main__':unittest.main()
