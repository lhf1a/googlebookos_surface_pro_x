"""Deterministic replay/validation, no hardware or input injection."""
import pathlib, subprocess, tempfile, unittest
R=pathlib.Path(__file__).resolve().parents[1]
class AdaptiveTouch(unittest.TestCase):
    def test_sparse_impulses_and_real_contact_protection(self):
        code=r'''
#include "adaptive-noise.hpp"
#include <cassert>
#include <iostream>
using namespace surface_touch;
struct Grid { std::vector<double> v=std::vector<double>(46*68,0.01);int rows()const{return 46;}int cols()const{return 68;}
double operator()(int r,int c)const{return v[r*68+c];}double &operator()(int r,int c){return v[r*68+c];}};
int main(){
 const std::string id(64,'a');Grid g;AdaptiveNoise a;a.begin(id,0x45e,0x996,46,68,0);
 for(int i=0;i<=720;i++){g(20,30)=i%3?0.95:0.01;a.observe(g,i*17);}
 assert(a.state=="ready"&&a.result.cells.size()==1);
 auto p=Profile::parse(a.result.encode(),id);g(20,30)=.95;
 assert(AdaptiveNoise::apply(p,id,0x45e,0x996,g)==1&&g(20,30)==.01);
 for(int r=19;r<=21;r++)for(int c=29;c<=31;c++)g(r,c)=.95;
 assert(AdaptiveNoise::apply(p,id,0x45e,0x996,g)==0&&g(20,30)==.95);
 Grid isolated;isolated(20,30)=.95;
 assert(AdaptiveNoise::apply(p,std::string(64,'b'),0x45e,0x996,isolated)==0);
 assert(AdaptiveNoise::apply(p,id,0x45e,0xc21,isolated)==0);
 AdaptiveNoise quiet;quiet.begin(id,0x45e,0x996,46,68,0);Grid q;
 for(int i=0;i<=720;i++)quiet.observe(q,i*17);
 assert(quiet.state=="quiet"&&quiet.result.cells.empty());
 AdaptiveNoise touch;touch.begin(id,0x45e,0x996,46,68,0);touch.observe(g,17);
 touch.finish(12000);assert(touch.state=="transient"&&touch.result.cells.empty());
 AdaptiveNoise line;line.begin(id,0x45e,0x996,46,68,0);Grid stripe;
 for(int c=0;c<68;c++)stripe(3,c)=.95;
 for(int i=0;i<=720;i++)line.observe(stripe,i*17);
 assert(line.state=="line_noise"&&line.lineRows.size()==1&&line.lineRows[0]==3&&line.result.cells.empty());
 AdaptiveNoise nodata;nodata.begin(id,0x45e,0x996,46,68,0);nodata.finish(12000);
 assert(nodata.state=="no_data"&&nodata.frames==0);
 bool bad=false;try{Profile::parse(p.encode(),std::string(64,'b'));}catch(...){bad=true;}assert(bad);
 bad=false;try{Profile::parse(p.encode()+"junk",id);}catch(...){bad=true;}assert(bad);
 std::cout<<"sparse_learn=1 isolated_repaired=1 real_blob_preserved=1 quiet_no_change=1 contamination_refused=1 identity_guard=1\n";
}
'''
        with tempfile.TemporaryDirectory() as td:
            d=pathlib.Path(td);(d/'main.cpp').write_text(code)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-O2','-I'+str(R/'probe/touch-calibration'),str(d/'main.cpp'),'-o',str(d/'replay')],check=True)
            subprocess.run([str(d/'replay')],check=True)

    def test_partial_fit_retains_prior_and_rejects_bad_points(self):
        code=r'''
import org.surface.touchcalibration.Calibration;
public class Partial {
 public static void main(String[] args){
  Calibration old=new Calibration("CalibrationX=0:0 .04:.05 .96:.95 1:1\nCalibrationY=\n");
  double[][] s=new double[42][];
  Calibration same=Calibration.fitPartial(s,old);
  for(int i=0;i<=100;i++)if(Math.abs(same.map(i/100.,.5)[0]-old.map(i/100.,.5)[0])>1e-12)throw new AssertionError();
  for(int i=0;i<42;i++){
   int group=i/14;double p=Calibration.POS[(i%14)/2];
   s[i]=old.inverse(group==1?.5:p,group==1?p:group==0?.5:.04);
  }
  s[0]=s[1]=s[12]=s[13]=null;
  Calibration partial=Calibration.fitPartial(s,old);
  for(int i=0;i<=100;i++)if(Math.abs(partial.map(i/100.,.5)[0]-old.map(i/100.,.5)[0])>.01)throw new AssertionError();
  s[3][0]+=.1;boolean bad=false;try{Calibration.fitPartial(s,old);}catch(IllegalArgumentException e){bad=true;}
  if(!bad)throw new AssertionError();
  System.out.println("all_skipped_keeps_prior=1 partial_fit=1 bad_sample_refused=1");
 }
}
'''
        with tempfile.TemporaryDirectory() as td:
            d=pathlib.Path(td);(d/'Partial.java').write_text(code)
            subprocess.run(['javac','-d',str(d),str(R/'probe/touch-calibration/Calibration.java'),str(d/'Partial.java')],check=True)
            subprocess.run(['java','-cp',str(d),'Partial'],check=True)
if __name__=='__main__':unittest.main()
