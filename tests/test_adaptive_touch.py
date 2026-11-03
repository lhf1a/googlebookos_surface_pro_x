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

    def test_partial_fit_retains_prior_and_regularizes_disagreement(self):
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
  s[3][0]+=.1;Calibration.fitPartial(s,old);
  System.out.println("all_skipped_keeps_prior=1 partial_fit=1 disagreement_regularized=1");
 }
}
'''
        with tempfile.TemporaryDirectory() as td:
            d=pathlib.Path(td);(d/'Partial.java').write_text(code)
            subprocess.run(['javac','-d',str(d),str(R/'probe/touch-calibration/Calibration.java'),str(d/'Partial.java')],check=True)
            subprocess.run(['java','-cp',str(d),'Partial'],check=True)
    def test_bounded_band_preview_and_guards(self):
        code=r'''
#include "adaptive-noise.hpp"
#include <cassert>
#include <iostream>
using namespace surface_touch;
struct Grid {std::vector<double> v=std::vector<double>(46*68,.01);int rows()const{return 46;}int cols()const{return 68;}
double operator()(int r,int c)const{return v[r*68+c];}double &operator()(int r,int c){return v[r*68+c];}};
int main(){
 const std::string id(64,'a');AdaptiveNoise a;a.begin(id,0x45e,0x996,46,68,0);Grid g;
 for(int c=0;c<68;c++)for(int r=2;r<=4;r++)g(r,c)=.95;
 for(int i=0;i<=720;i++)a.observe(g,i*17);
 assert(a.state=="line_noise"&&a.result.bandRows==std::vector<int>({2,3,4}));
 const auto p=Profile::parse(a.result.encode(),id);g(20,30)=.8;g(1,30)=.7;g(5,30)=.9;
 assert(AdaptiveNoise::apply(p,id,0x45e,0x996,g)==3*68);
 assert(g(20,30)==.8&&std::abs(g(3,30)-.8)<1e-10&&g(3,10)==.01);
 assert(AdaptiveNoise::apply(p,std::string(64,'b'),0x45e,0x996,g)==0);
 assert(AdaptiveNoise::apply(p,id,0x45e,0xc21,g)==0);
 for(std::string tail:{"R 1 0","R 1 45","R 5 2 3 4 5 6","R 2 3 3","R 3 3 10 20","C 1 68","R -1","R 1 3 junk"}){
  bool bad=false;try{Profile::parse("SPXNOISE2 "+id+" 1118 2454 46 68 "+tail,id);}catch(...){bad=true;}assert(bad);
 }
 auto column=Profile::parse("SPXNOISE2 "+id+" 1118 2454 46 68 C 1 4",id);
 Grid c;c(10,4)=.95;assert(AdaptiveNoise::apply(column,id,0x45e,0x996,c)==46&&c(10,4)==.01);
 AdaptiveNoise wide;wide.begin(id,0x45e,0x996,46,68,0);Grid w;
 for(int r=0;r<20;r++)for(int c=0;c<68;c++)w(r,c)=.95;
 for(int i=0;i<=720;i++)wide.observe(w,i*17);
 assert(!wide.result.band());
 std::cout<<"band_repair=1 outside_touch_preserved=1 crossing_support_interpolated=1 unsafe_band_refused=1 identity_guard=1\n";
}
'''
        with tempfile.TemporaryDirectory() as td:
            d=pathlib.Path(td);(d/'main.cpp').write_text(code)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-O2','-I'+str(R/'probe/touch-calibration'),str(d/'main.cpp'),'-o',str(d/'test')],check=True)
            subprocess.run([str(d/'test')],check=True)


if __name__=='__main__':unittest.main()
