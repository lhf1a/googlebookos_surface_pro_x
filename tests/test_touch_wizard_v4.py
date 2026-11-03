"""Full-range fit, paced thumb evidence, explicit user gates and real helper IPC."""
import pathlib, subprocess, tempfile, unittest
R=pathlib.Path(__file__).resolve().parents[1]
class V4(unittest.TestCase):
 def test_full_range_fit_and_confirmation(self):
  code=r"""
import org.surface.touchcalibration.*;
public class Check {
 static void ok(boolean b){if(!b)throw new AssertionError();}
 public static void main(String[] args){
  CalibrationCapture cap=new CalibrationCapture();
  ok(cap.begin(4,0,.9,.8,.015,.04));cap.other(99,.02,.03);cap.move(99,.5,.5);
  ok(cap.end(99,100,.1,.1)==null&&cap.pointer==4);
  cap.move(4,.901,.801);ok(cap.green(100));double[] point=cap.end(4,120,.902,.802);
  ok(Math.abs(point[0]-.901)<1e-10);
  ok(cap.begin(4,200,-2,5,.5,.5));ok(cap.end(4,300,-2,5)!=null);
  ok(!cap.begin(4,400,Double.NaN,.2,.5,.5));
  double[][] s=new double[42][];
  for(int i=0;i<42;i++){int g=i/14;double p=Calibration.POS[(i%14)/2];s[i]=new double[]{.25+.5*(g==1?.5:p),.25+.5*(g==1?p:g==0?.5:.04)};}
  Calibration old=new Calibration("");Calibration fit=Calibration.fitPartial(s,old);
  for(int i=0;i<42;i++){int g=i/14;double p=Calibration.POS[(i%14)/2];double[] v=fit.map(s[i][0],s[i][1]);ok(Math.abs(v[g==1?1:0]-p)<.0001);}
  // Contradictory pairs and saturated positions regularize without invalidating.
  s[0][0]=s[1][0]=0;s[5][0]=.9;s[13][0]=.1;Calibration odd=Calibration.fitPartial(s,old);
  double last=-1;for(int i=0;i<=1000;i++){double x=odd.map(i/1000.,.5)[0];ok(x>last&&x>=0&&x<=1);last=x;}
  Calibration same=Calibration.fitPartial(new double[42][],fit);ok(same.encode("a").equals(fit.encode("a")));
  WizardFlow f=new WizardFlow();ok(!f.confirmNoise());f.finishRetest();ok(!f.noiseConfirmed);ok(f.confirmNoise());f.changedNoise();ok(!f.noiseConfirmed&&!f.retested);
  ok(!f.confirmThumb());f.thumbRetested=true;ok(f.confirmThumb());f.changedThumb();ok(!f.thumbConfirmed);
  ThumbTapCapture t=new ThumbTapCapture(3000);t.down(1,2000);ok(t.total()==0);
  for(int i=0;i<10;i++){long at=3000+i*2000;t.down(1,at);t.up(1,at+100);}
  ok(t.total()==10&&t.extras()==0&&t.missing()==0&&!t.proposeSizeCandidate());
  t.down(1,21100);t.up(1,21120);t.down(1,21200);t.up(1,21220);ok(t.total()==12&&t.extras()==2&&t.proposeSizeCandidate());ok(!t.finished(22999)&&t.finished(23000));
  System.out.println("full_range=1 large_offset_fit=1 inconsistent_samples_regularized=1 monotone=1 no_auto_confirmation=1 thumb_before_plan_after=1");
  System.out.print(fit.encode("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
 }
}
"""
  with tempfile.TemporaryDirectory() as td:
   d=pathlib.Path(td);(d/'Check.java').write_text(code)
   sources=['Calibration','CalibrationCapture','WizardFlow','ThumbTapCapture']
   subprocess.run(['javac','-d',str(d),*[str(R/'probe/touch-calibration'/f'{s}.java') for s in sources],str(d/'Check.java')],check=True)
   output=subprocess.check_output(['java','-cp',str(d),'Check'],text=True);print(output.splitlines()[0])
   profile=output[output.index('Version='):]
   subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(R/'probe/touch-calibration/profile-tool.cpp'),'-o',str(d/'tool')],check=True)
   p=subprocess.run([str(d/'tool'),'validate','a'*64],input=profile,text=True,capture_output=True);self.assertEqual(p.returncode,0,p.stderr)
   for bad in [profile.replace('Device='+'a'*64,'Device='+'b'*64),profile+'Unknown=1\n',profile.replace('CalibrationX=','CalibrationX=nan:0 '),profile.replace('ContactSizeMax=2.0','ContactSizeMax=99')]:
    self.assertNotEqual(subprocess.run([str(d/'tool'),'validate','a'*64],input=bad,text=True,capture_output=True).returncode,0)
 def test_noise_export_and_retest_use_actual_shell(self):
  # Reuse the existing shell harness with native-noise control simulated only at
  # the /run boundary; run the actual dispatch clauses, validator and publisher.
  source=(R/'tests/test_touch_calibration_boot.py').read_text()
  source=source.replace('class Boot(unittest.TestCase):','class NoiseBoot(unittest.TestCase):')
  source=source.replace("put('etc/iptsd.conf'", "put('sys/class/power_supply/ADP1/online','0\\n')\n            put('etc/iptsd.conf'",1)
  anchor="            script=(R/'probe/test2/touch-calibration-boot').read_text()"
  extra="""            subprocess.run(['g++','-std=c++17','-O2',str(R/'probe/touch-calibration/noise-tool.cpp'),'-o',str(w/'bin/touch-noise-profile')],check=True)
"""
  source=source.replace(anchor,extra+anchor)
  # Fixture turns native reload ACK into a file update, not an Android/device test.
  replacement='noise_reload() {\n    printf \'%s applied 100 0\\n\' "$1" > "$noise/status"; return 0\n'
  source=source.replace("runner=put('runner',script,0o755)","script=script.replace('noise_reload() {', "+repr(replacement)+")\n            runner=put('runner',script,0o755)")
  begin=source.index('                p=start();');end=source.index('            finally:',begin)
  source=source[:begin]+"""                p=start();noise=w/'run/iptsd-noise';(noise/'version').write_text('3\\n')
                patch=f'SPXNOISE2 {device} 1118 3105 46 68 R 1 3\\n'
                (noise/'candidate').write_text(patch);(noise/'status').write_text('fixture line_noise 100 0\\n')
                self.assertEqual(request('noise-export'),'OK');self.assertEqual((files/'noise-patch').read_text(),patch)
                self.assertFalse((noise/'active').exists());self.assertFalse((store/(device+'.noise')).exists())
                self.assertEqual(request('noise-apply'),'OK');active=(noise/'active').read_bytes()
                self.assertEqual(request('noise-retest','preview'),'OK');self.assertEqual((noise/'active').read_bytes(),active)
                self.assertTrue((noise/'command').read_text().startswith('learn '));self.assertFalse((store/(device+'.noise')).exists())
                self.assertEqual(request('noise-save'),'OK');self.assertEqual((store/(device+'.noise')).read_bytes(),active)
                # Real coordinate preview dispatch must replace stale learn with
                # reload and leave the already confirmed active/saved rule intact.
                (noise/'command').write_text('learn '+device+' stale-learn\\n')
                self.assertEqual(request('preview',profile),'OK')
                self.assertTrue((noise/'command').read_text().startswith('reload '))
                self.assertEqual((noise/'active').read_bytes(),active)
                self.assertEqual((store/(device+'.noise')).read_bytes(),active)

                self.assertEqual(request('noise-retest','preview'),'ERROR')
                stop(p)
"""+source[end:]
  scope={'__file__':str(R/'tests/test_touch_calibration_boot.py'),'__name__':'fixture'}
  exec(compile(source,'noise-helper-fixture','exec'),scope)
  test=scope['NoiseBoot']('test_first_run_preview_save_reboot_timeout_skip_and_wrong_device')
  result=unittest.TestResult();test.run(result);self.assertEqual(result.errors+result.failures,[],str(result.errors+result.failures))
if __name__=='__main__':unittest.main()
