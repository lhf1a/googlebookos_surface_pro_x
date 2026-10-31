"""Exercise real Java fitting/inversion and native validator, without a touchscreen."""
import pathlib,subprocess,tempfile,unittest,shutil
R=pathlib.Path(__file__).resolve().parents[1]

class Wizard(unittest.TestCase):
    def test_java_fit_inverse_and_native_boundary(self):
        with tempfile.TemporaryDirectory() as td:
            w=pathlib.Path(td)
            (w/'Test.java').write_text(r'''
import org.surface.touchcalibration.Calibration;
import org.surface.touchcalibration.ThumbCheck;
public class Test {
 static void check(boolean b){if(!b)throw new AssertionError();}
 public static void main(String[] args){
  Calibration original=new Calibration("CalibrationX=0:0 .02:.03 .12:.12 .96:.96 .995:.98 1:1\nCalibrationY=0:0 .03:.04 .12:.12 .94:.94 .995:.98 1:1\nCalibrationXTop=0:0 1:1");
  check(!original.thumbOptimized);
  Calibration optimized=original.withThumbOptimization(true);
  check(new Calibration(optimized.encode("a")).thumbOptimized);
  check(!new Calibration(optimized.withThumbOptimization(false).encode("a")).thumbOptimized);
  boolean rejected=false;try{new Calibration("ContactSizeMax=2.5");}catch(IllegalArgumentException e){rejected=true;}check(rejected);
  ThumbCheck thumb=new ThumbCheck();check(!thumb.begin(0,.5,.5));
  check(thumb.begin(0,.02,.2));check(thumb.end(4999,.02,.3).equals("early"));check(thumb.completed==0);
  thumb.begin(0,.02,.2);thumb.move(.5,.5);check(thumb.end(5000,.02,.3).equals("retry"));
  thumb.begin(0,.02,.2);check(thumb.end(5000,.02,.21).equals("retry"));
  thumb.begin(0,.02,.2);thumb.cancel();check(thumb.end(5000,.02,.3).equals("ignore"));
  for(int i=0;i<3;i++){check(thumb.begin(100,.02,.2));thumb.move(.02,.3);check(thumb.remaining(5099)==1);check(thumb.end(5100,.02,.3).equals(i==2?"complete":"next"));}
  for(int y=0;y<=100;y++)for(int x=0;x<=100;x++){
   double[] mapped=original.map(x/100.,y/100.),raw=original.inverse(mapped[0],mapped[1]);
   check(Math.abs(raw[0]-x/100.)<1e-10&&Math.abs(raw[1]-y/100.)<1e-10);
   double[] same=optimized.map(x/100.,y/100.);check(same[0]==mapped[0]&&same[1]==mapped[1]);
  }
  Calibration noTop=new Calibration("CalibrationX=0:0 .02:.03 .12:.12 1:1\nCalibrationXTop=");
  check(Math.abs(noTop.map(.02,.01)[0]-.03)<1e-9);
  double[][] samples=new double[42][2];
  for(int i=0;i<42;i++){
   int group=i/14;double p=Calibration.POS[(i%14)/2];
   double x=group==1?.5:p,y=group==1?p:group==0?.5:.04;
   samples[i]=new double[]{x,y};
  }
  Calibration identity=Calibration.fit(samples);
  for(int i=0;i<=100;i++){double v=i/100.;double[] q=identity.map(v,v);check(Math.abs(q[0]-v)<1e-10&&Math.abs(q[1]-v)<1e-10);}
  // Known spatial distortion, sampled through an already calibrated display.
  for(int i=0;i<42;i++){
   double[] raw=original.inverse(samples[i][0],samples[i][1]);
   samples[i]=original.inverse(original.map(raw[0],raw[1])[0],original.map(raw[0],raw[1])[1]);
  }
  Calibration fitted=Calibration.fit(samples);
  check(Math.abs(fitted.map(samples[0][0],samples[0][1])[0]-.015)<.004);
  System.out.print(fitted.encode("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  samples[1][0]+=.04;boolean failed=false;
  try{Calibration.fit(samples);}catch(IllegalArgumentException e){failed=true;}check(failed);
 }
}
''')
            subprocess.run(['javac','-d',str(w),str(R/'probe/touch-calibration/Calibration.java'),str(R/'probe/touch-calibration/ThumbCheck.java'),str(w/'Test.java')],check=True)
            profile=subprocess.check_output(['java','-cp',str(w),'Test'],text=True)
            subprocess.run(['g++','-std=c++17','-O2',str(R/'probe/touch-calibration/profile-tool.cpp'),'-o',str(w/'tool')],check=True)
            def run(body,device='a'*64):return subprocess.run([str(w/'tool'),'validate',device],input=body,text=True,capture_output=True)
            ok=run(profile);self.assertEqual(ok.returncode,0,ok.stderr)
            self.assertIn('[Touchscreen]',ok.stdout)
            self.assertIn('SizeMax = 2.0',ok.stdout)
            self.assertIn('SizeMax = 2.4',run(profile.replace('ContactSizeMax=2.0','ContactSizeMax=2.4')).stdout)
            self.assertIn('SizeMax = 2.0',run(profile.replace('ContactSizeMax=2.0\n','')).stdout)
            self.assertNotEqual(run(profile.replace('ContactSizeMax=2.0','ContactSizeMax=2.5')).returncode,0)
            self.assertNotEqual(run(profile+'ContactSizeMax=2.4\n').returncode,0)
            self.assertNotEqual(run(profile,'b'*64).returncode,0)
            for bad in [profile+'Extra=1\n',profile+'Device='+'a'*64+'\n',profile.replace('Version=1','Version=2'),profile.replace('CalibrationTopEnd=0.3','CalibrationTopEnd=nan'),profile.replace('CalibrationY=','CalibrationY=0:0 .5:.9 1:1 #'),profile+'x'*300]:
                self.assertNotEqual(run(bad).returncode,0)
            # Atomic publication must replace a symlink, never follow it.
            (w/'private').write_text('unchanged');(w/'reply').symlink_to(w/'private')
            subprocess.run([str(w/'tool'),'publish',str(w),'reply'],input='nonce\nOK\n',text=True,check=True)
            self.assertEqual((w/'private').read_text(),'unchanged');self.assertFalse((w/'reply').is_symlink())

    def test_default_profile_is_device_neutral(self):
        import configparser
        p=configparser.ConfigParser();p.read(R/'configs/iptsd-sq2-calibration.conf')
        for key in ('CalibrationX','CalibrationY','CalibrationXTop'):self.assertEqual(p['Touchscreen'][key],'')
        for name in ('touch-calibration-boot','touch-services','init-googlebook-native'):
            subprocess.run(['sh','-n',str(R/'probe/test2'/name)],check=True)

if __name__=='__main__':unittest.main()
