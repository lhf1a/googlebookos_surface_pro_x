from pathlib import Path
import subprocess,tempfile,unittest
R=Path(__file__).resolve().parents[1]
CPP='#include "adaptive-noise.hpp"\n#include <iostream>\n#include <cassert>\nusing namespace surface_touch;\nstruct Grid{std::vector<double>v=std::vector<double>(46*68,.01);int rows()const{return 46;}int cols()const{return 68;}double operator()(int r,int c)const{return v[r*68+c];}double&operator()(int r,int c){return v[r*68+c];}};\nint main(){std::string id(64,\'a\');AdaptiveNoise a;a.begin(id,1118,2454,46,68,0);\nfor(int i=0;i<15;i++){Grid g;if(i<6)for(int r=2;r<5;r++)for(int c=10;c<53;c++)g(r,c)=.9;a.observe(g,i*100);}a.finish(45000);\nstd::cout<<"intermittent_15_frames_6_abnormal="<<a.state<<" candidate="<<(a.state=="ready"||a.result.band())<<"\\n";\nAdaptiveNoise w;w.begin(id,1118,2454,46,68,0);Grid g;for(int r=3;r<15;r++)for(int c=8;c<60;c++)g(r,c)=.8;w.observe(g,100);w.finish(45000);\nstd::cout<<"single_wide_frame="<<w.state<<" candidate="<<(w.state=="ready"||w.result.band())<<"\\n";\nif(w.state=="ready"){\n auto p=Profile::parse(w.result.encode(),id);g(30,30)=.9;int n=AdaptiveNoise::apply(p,id,1118,2454,g);assert(n>0&&g(8,20)<.05&&g(30,30)==.9);\n std::cout<<"wide_repaired=1 outside_touch_preserved=1\\n";\n}\n}\n'
class DetectedCandidate(unittest.TestCase):
 def test_sparse_time_and_wide_region(self):
  with tempfile.TemporaryDirectory() as td:
   d=Path(td);(d/'test.cpp').write_text(CPP)
   subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(R/'probe/touch-calibration'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
   p=subprocess.check_output([str(d/'test')],text=True);print(p)
   self.assertIn('intermittent_15_frames_6_abnormal=line_noise candidate=1',p)
   self.assertIn('single_wide_frame=ready candidate=1',p);self.assertIn('wide_repaired=1 outside_touch_preserved=1',p)
 def test_large_map_boundary_and_identity(self):
  with tempfile.TemporaryDirectory() as td:
   d=Path(td);subprocess.run(['g++','-std=c++17','-O2',str(R/'probe/touch-calibration/noise-tool.cpp'),'-o',str(d/'noise')],check=True)
   identity='a'*64;body=f'SPXNOISE3 {identity} 1118 2454 46 68 '+('cc'*(46*68))+'\n'
   p=subprocess.run([str(d/'noise'),identity],input=body,capture_output=True,text=True);self.assertEqual(p.returncode,0,p.stderr);self.assertEqual(p.stdout,body)
   for bad in [body[:-4]+'\n',body.replace('cc','zz',1),body+'junk',body.replace(identity,'b'*64)]:self.assertNotEqual(subprocess.run([str(d/'noise'),identity],input=bad,capture_output=True,text=True).returncode,0)
   subprocess.run(['g++','-std=c++17','-O2',str(R/'probe/touch-calibration/profile-tool.cpp'),'-o',str(d/'publish')],check=True)
   p=subprocess.run([str(d/'publish'),'publish',str(d),'noise-patch'],input=body,capture_output=True,text=True);self.assertEqual(p.returncode,0,p.stderr);self.assertEqual((d/'noise-patch').read_text(),body)
if __name__=='__main__':unittest.main()
