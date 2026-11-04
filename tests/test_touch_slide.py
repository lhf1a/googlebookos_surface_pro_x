"""Slide-stage guards, actual native interpolation and Java motion evidence."""
from pathlib import Path
import subprocess,tempfile,unittest
R=Path(__file__).resolve().parents[1]
CPP='#include "adaptive-noise.hpp"\n#include <cassert>\n#include <iostream>\nusing namespace surface_touch;\nstruct Grid{std::vector<double> v=std::vector<double>(46*68,.01);int rows()const{return 46;}int cols()const{return 68;}double operator()(int r,int c)const{return v[r*68+c];}double&operator()(int r,int c){return v[r*68+c];}};\ntemplate<class T>auto begin(T&a,int)->decltype(a.begin(std::string(64,\'a\'),1118,2454,46,68,0,true),void()){a.begin(std::string(64,\'a\'),1118,2454,46,68,0,true);}\ntemplate<class T>void begin(T&a,long){a.begin(std::string(64,\'a\'),1118,2454,46,68,0);}\nGrid frame(int i,bool line){Grid g;if(line)for(int x=10;x<60;x++)g(3,x)=.9;int cx=15+(i%50)/2;for(int y=24;y<27;y++)for(int x=cx;x<cx+3;x++)g(y,x)=.7;return g;}\nint main(){AdaptiveNoise a;begin(a,0);for(int i=0;i<200;i++){auto g=frame(i,true);a.observe(g,i*100);}a.finish(20000);\nstd::cout<<"slide20_state="<<a.state<<" band="<<a.result.band()<<"\\n";\nif(a.learning)return 0;\nassert(a.state=="line_noise"&&a.result.bandRows==std::vector<int>{3});auto p=a.result;\nAdaptiveNoise b;begin(b,0);for(int i=0;i<200;i++){auto g=frame(i,true);AdaptiveNoise::apply(p,std::string(64,\'a\'),1118,2454,g);assert(g(3,40)==.01&&g(24,15+(i%50)/2)==.7);b.observe(g,i*100);}b.finish(20000);assert(b.state=="quiet");\nAdaptiveNoise real;begin(real,0);for(int i=0;i<200;i++){auto g=frame(i,false);real.observe(g,i*100);}real.finish(20000);assert(real.state=="quiet"&&!real.result.band());\nAdaptiveNoise empty;begin(empty,0);empty.finish(20000);assert(empty.state=="no_data");\nGrid crossing;for(int y=2;y<=4;y++)for(int x=20;x<23;x++)crossing(y,x)=.7;AdaptiveNoise::apply(p,std::string(64,\'a\'),1118,2454,crossing);assert(crossing(3,21)==.7);\nstd::cout<<"retest=quiet moving_finger_preserved=1 crossing_preserved=1 no_data_rejected=1\\n";\n}\n'
class Slide(unittest.TestCase):
 def test_native(self):
  with tempfile.TemporaryDirectory() as td:
   d=Path(td);(d/'check.cpp').write_text(CPP)
   subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(R/'probe/touch-calibration'),str(d/'check.cpp'),'-o',str(d/'check')],check=True)
   out=subprocess.check_output([str(d/'check')],text=True);print(out,end='');self.assertIn('retest=quiet',out)
 def test_motion(self):
  code=r'''import org.surface.touchcalibration.*;
class Check{static void ok(boolean b){if(!b)throw new AssertionError();}
public static void main(String[]args){
 SlideCapture a=new SlideCapture();ok(!a.enough());
 for(int i=0;i<200;i++)a.frame(i*100,new int[]{1},new double[]{.25+.4*Math.sin(i*.04)},new double[]{.6},false);
 ok(a.enough()&&a.clean());a.frame(20000,new int[]{1,2},new double[]{.5,.7},new double[]{.6,.06},false);ok(!a.clean()&&a.multiFrames==1);
 SlideCapture still=new SlideCapture();for(int i=0;i<200;i++)still.frame(i*100,new int[]{1},new double[]{.5},new double[]{.6},false);ok(!still.enough());
 a.frame(21000,new int[0],new double[0],new double[0],true);ok(!a.enough());
 WizardFlow f=new WizardFlow();ok(!f.confirmSlide());f.slideRetested=true;ok(f.confirmSlide());f.changedNoise();ok(!f.slideConfirmed);
 System.out.println("motion_required=1 multi_detected=1 cancellation_rejected=1 confirmation_gate=1");}}'''
  with tempfile.TemporaryDirectory() as td:
   d=Path(td);(d/'Check.java').write_text(code)
   subprocess.run(['javac','-d',str(d),str(R/'probe/touch-calibration/SlideCapture.java'),str(R/'probe/touch-calibration/WizardFlow.java'),str(d/'Check.java')],check=True)
   subprocess.run(['java','-cp',str(d),'Check'],check=True)
 def test_ui_and_isolation(self):
  s=(R/'probe/touch-calibration/MainActivity.java').read_text()
  self.assertIn('flow.confirmNoise();slideIntro()',s);self.assertIn('if(!flow.slideConfirmed)',s)
  self.assertIn('if(!slideEnough)',s);self.assertIn('noise-slide',s);self.assertIn('全程单指',s)
  n=(R/'probe/touch-calibration/noise-runtime.hpp').read_text()
  for t in ['if(!noiseBypass)AdaptiveNoise::apply','baselineProbe?Profile{}:active','tapBypass=true','time+600000','tapreload'] : self.assertIn(t,n)
  t=(R/'probe/touch-calibration/tap-runtime.hpp').read_text();self.assertIn('!learning&&!bypass',t);self.assertIn('candidate.frames=0',t)
  h=(R/'probe/test2/touch-calibration-boot').read_text();self.assertIn('noise-resume)',h);self.assertIn('op=$op-test',h)
if __name__=='__main__':unittest.main()
