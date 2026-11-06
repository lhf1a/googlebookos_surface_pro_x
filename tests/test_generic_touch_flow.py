"""Generic core guards, user confirmation and default build routing."""
from pathlib import Path
import ast, importlib.util, json, subprocess, tempfile, unittest
R = Path(__file__).resolve().parents[1]

class GenericTouch(unittest.TestCase):
 def test_core_and_identity(self):
  cpp = r'''
#include "tap-core.hpp"
#include <optional>
#include <cassert>
#include <iostream>
struct V{double a=.1,b=.1;double&x(){return a;}double&y(){return b;}};
struct Contact{std::optional<int> index=0;std::optional<bool> valid=true,stable=true;V mean;};
struct Image{double v[46][68]{};int rows()const{return 46;}int cols()const{return 68;}int size()const{return 46*68;}double operator()(int r,int c)const{return v[r][c];}};
Image blob(){Image a;for(int y=3;y<=4;y++)for(int c=5;c<7;c++)a.v[y][c]=.6;return a;}
int main(){using namespace surface_touch;auto im=blob();std::vector<Contact> c(1);CoreContinuity<Contact> t;
 t.step(im,c,0,6,false,2.4,33);t.step(im,c,10,6,false,2.4,33);c[0].valid=false;
 assert(t.step(im,c,20,6,false,2.4,33)&&!c[0].valid.value());assert(t.repairable==1);
 t.begin();c.assign(1,Contact{});t.step(im,c,0,6,true,2.4,33);t.step(im,c,10,6,true,2.4,33);c[0].valid=false;
 assert(t.step(im,c,20,6,true,2.4,33)&&c[0].valid.value());c.clear();assert(t.step(im,c,30,6,true,2.4,33)&&c.size()==1);
 Image quiet;c.clear();assert(!t.step(quiet,c,40,6,true,2.4,33)&&c.empty());
 c.clear();assert(!t.step(im,c,50,6,true,2.4,33)&&c.empty());
 t.begin();c.assign(1,Contact{});t.step(im,c,0,6,true,2.4,33);t.step(im,c,10,6,true,2.4,33);
 Image two=im;two.v[20][20]=two.v[20][21]=two.v[21][20]=two.v[21][21]=.6;c.resize(2);c[1].index=1;assert(!t.step(two,c,20,6,true,2.4,33)&&c.size()==2);
 Image palm;for(int y=0;y<12;y++)for(int x=0;x<12;x++)palm.v[y][x]=.7;c.assign(1,Contact{});c[0].valid=false;assert(!t.step(palm,c,30,6,true,2.4,33)&&!c[0].valid.value());
 t.begin();c.assign(1,Contact{});t.step(im,c,0,6,true,2.4,33);t.step(im,c,10,6,true,2.4,33);c.clear();assert(!t.step(im,c,40,6,true,2.4,33)&&c.empty());
 t.begin();c.assign(1,Contact{});t.step(im,c,0,6,true,2.4,33);t.step(im,c,10,6,true,2.4,33);Image weak=im;for(auto &row:weak.v)for(double &v:row)v*=.3;c.clear();assert(!t.step(weak,c,20,6,true,2.4,33)&&c.empty());
 t.begin();c.assign(1,Contact{});t.step(im,c,0,2,true,2.4,33);t.step(im,c,10,2,true,2.4,33);c.clear();assert(t.step(im,c,20,2,true,2.4,33));c.clear();assert(t.step(im,c,30,2,true,2.4,33));c.clear();assert(!t.step(im,c,40,2,true,2.4,33)&&c.empty());
 auto id=std::string(64,'a');assert(TapPolicy::parse("SPXTAP1 "+id+" 1 2 46 68 4",id).frames==4);
 for(auto bad:{"SPXTAP1 "+id+" 1 2 46 68 7","SPXTAP1 "+id+" 1 2 46 68 -1","SPXTAP1 "+std::string(64,'b')+" 1 2 46 68 4"}){bool fail=false;try{TapPolicy::parse(bad,id);}catch(...){fail=true;}assert(fail);}
 std::cout<<"learn_without_apply=1 raw_supported_repair=1 immediate_release=1 doubletap_not_merged=1 multitouch_preserved=1 palm_rejected=1 stale_rejected=1 weak_rejected=1 bounded=1 identity=1\n";
}
'''
  with tempfile.TemporaryDirectory() as td:
   d=Path(td);(d/'test.cpp').write_text(cpp)
   subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(R/'probe/touch-calibration'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
   subprocess.run([str(d/'test')],check=True)
 def test_confirm_gates_and_ui_path(self):
  code='import org.surface.touchcalibration.*;class Check{public static void main(String[]x){WizardFlow f=new WizardFlow();if(f.confirmTap())throw new AssertionError();f.tapRetested=true;if(!f.confirmTap())throw new AssertionError();f.changedTap();if(f.tapConfirmed)throw new AssertionError();f.useOriginalTap();if(!f.tapConfirmed)throw new AssertionError();System.out.println("retest_before_confirm=1 original_choice=1");}}'
  with tempfile.TemporaryDirectory() as td:
   d=Path(td);(d/'Check.java').write_text(code);subprocess.run(['javac','-d',str(d),str(R/'probe/touch-calibration/WizardFlow.java'),str(d/'Check.java')],check=True);subprocess.run(['java','-cp',str(d),'Check'],check=True)
  s=(R/'probe/touch-calibration/MainActivity.java').read_text()
  for op in ['tap-begin','tap-finish','tap-apply','tap-retest','tap-save','tap-cancel']:self.assertIn('"'+op+'"',s)
  self.assertIn('flow.confirmTap();coordinateIntro()',s);self.assertIn('if(!flow.tapConfirmed)',s)
  self.assertNotIn('尚未自动生成、应用或验收多点修复',s)
  self.assertIn('AFTER_CAPTURE_MS=1000',(R/'probe/touch-calibration/SingleTapCapture.java').read_text())
 def test_default_generic_baseline(self):
     # Current boot is preserved as a checked 6.18 bundle, not rebuilt through old overlays.
     for name in ['build.py', 'build-surface-pro-x.py', 'build-source-boot.py', 'build-source-disk.py', 'build-sq2-default.py']:
         text = (R/"scripts"/name).read_text()
         self.assertIn("build-lts.py", text)
         self.assertNotIn("6.3.8", text)
 def test_bundle_manifest_guards(self):
  def load(n,f):
   sp=importlib.util.spec_from_file_location(n,f);m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);return m
  m=load('tap_payload',R/'scripts/surface-touch-payload.py');c=load('tap_cpio',R/'scripts/pack-sq2-vulkan.py')
  with tempfile.TemporaryDirectory() as td:
   b=Path(td);(b/'touch-bundle.json').write_text(json.dumps({'files':{n:'' for n in m.PATHS},'source_sha256':{}}))
   with self.assertRaisesRegex(ValueError,'Incomplete touch source manifest'):m.integrate(b'',R,b,c)
 def test_actual_helper_tap_preview_save_restore(self):
  source=(R/'tests/test_touch_calibration_boot.py').read_text()
  source=source.replace('class Boot(unittest.TestCase):','class TapBoot(unittest.TestCase):')
  anchor="            script=(R/'probe/test2/touch-calibration-boot').read_text()"
  source=source.replace(anchor,"            subprocess.run(['g++','-std=c++17','-O2',str(R/'probe/touch-calibration/tap-tool.cpp'),'-o',str(w/'bin/touch-tap-profile')],check=True)\n"+anchor)
  fake='noise_cmd() {\n case "$1" in\n taplearn) printf \'%s learning 0 0 0\\n\' "$2" > "$noise/tap-status";return;;\n tapfinish) printf \'SPXTAP1 %s 1118 3105 46 68 4\\n\' "$device" > "$noise/tap-candidate";printf \'%s ready 30 3 3\\n\' "$2" > "$noise/tap-status";return;;\n tapreload) printf \'%s applied 30 3 3\\n\' "$2" > "$noise/tap-status";return;;\n esac\n'
  # Simulate only the native file boundary; actual shell/validators/publishers execute.
  source=source.replace("runner=put('runner',script,0o755)","script=script.replace('noise_cmd() {', "+repr(fake)+").replace('noise_reload() {', 'noise_reload() { return 0;\\n')\n            runner=put('runner',script,0o755)")
  begin=source.index('                p=start();');end=source.index('            finally:',begin)
  source=source[:begin]+"""                p=start();noise=w/'run/iptsd-noise';(noise/'version').write_text('7\\n')
                self.assertEqual(request('tap-save'),'ERROR')
                self.assertEqual(request('tap-begin'),'OK')
                self.assertEqual(request('tap-finish'),'OK')
                self.assertFalse((store/(device+'.tap')).exists())
                self.assertEqual(request('tap-apply'),'OK')
                original=(noise/'tap-active').read_bytes()
                self.assertEqual(request('tap-retest'),'OK')
                self.assertEqual(request('tap-save'),'OK')
                self.assertEqual((store/(device+'.tap')).read_bytes(),original)
                self.assertEqual(request('tap-retest'),'ERROR')
                self.assertEqual(request('tap-begin'),'OK');self.assertEqual(request('tap-finish'),'OK')
                self.assertEqual(request('tap-apply'),'OK')
                self.assertEqual(request('tap-cancel'),'OK')
                self.assertEqual((noise/'tap-active').read_bytes(),original)
                self.assertEqual((store/(device+'.tap')).read_bytes(),original)
                stop(p)
"""+source[end:]
  scope={'__file__':str(R/'tests/test_touch_calibration_boot.py'),'__name__':'fixture'}
  exec(compile(source,'tap-helper-fixture','exec'),scope)
  result=unittest.TestResult();scope['TapBoot']('test_first_run_preview_save_reboot_timeout_skip_and_wrong_device').run(result)
  self.assertEqual(result.errors+result.failures,[],str(result.errors+result.failures))
if __name__=='__main__':unittest.main()
