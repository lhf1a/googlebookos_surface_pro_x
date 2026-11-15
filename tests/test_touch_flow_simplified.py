from pathlib import Path
import subprocess, tempfile, unittest
R=Path(__file__).resolve().parents[1]
class SimplifiedFlow(unittest.TestCase):
 def test_idle_outcomes_and_clean_taps(self):
  code=r'''
import org.surface.touchcalibration.*;
class Check {
 static void ok(boolean b){if(!b)throw new AssertionError();}
 public static void main(String[]args){
  for(String s:new String[]{"quiet","ready","line_noise","broad_signal","transient","unresolved"}){
   ok(WizardFlow.idleOutputClean(s,4506,0));
   ok(!WizardFlow.idleOutputClean(s,4506,1));
   ok(!WizardFlow.idleOutputClean(s,0,0));
  }
  for(String s:new String[]{"no_data","inconclusive","learning","error","expired","applied","unknown"})
   ok(!WizardFlow.idleOutputClean(s,4506,0));
  SingleTapCapture t=new SingleTapCapture(0,1);ok(!t.clean());
  for(int i=0;i<15;i++){long at=i*1200;t.event(0,1,at);t.event(1,1,at+100);t.tick(at+1100);}
  ok(t.clean());
  t.cancels[3]=1;ok(!t.clean());t.cancels[3]=0;
  t.starts[3]=2;ok(!t.clean());t.starts[3]=1;
  t.groups[3]=2;ok(!t.clean());t.groups[3]=1;
  t.peak[3]=2;ok(!t.clean());t.peak[3]=1;
  t.starts[3]=0;ok(!t.clean());
 }
}
'''
  with tempfile.TemporaryDirectory() as td:
   p=Path(td);(p/'Check.java').write_text(code)
   subprocess.run(['javac','-d',td,str(R/'probe/touch-calibration/WizardFlow.java'),str(R/'probe/touch-calibration/SingleTapCapture.java'),str(p/'Check.java')],check=True)
   subprocess.run(['java','-cp',td,'Check'],check=True)
 def test_no_automatic_refinement_and_confirm_before_save(self):
  s=(R/'probe/touch-calibration/MainActivity.java').read_text()
  review=s[s.index('  void noiseReview(){'):s.index('  void pointIntro(){')]
  self.assertNotIn('laterNoise(',review)
  self.assertNotIn('send("noise-save"',review)
  self.assertIn('flow.confirmNoise();slideIntro()',review)
  slide=s[s.index('  void slideReview(){'):s.index('  Runnable noiseStep;')]
  self.assertIn('!slideAfter',slide)
  self.assertIn('holdButton',slide)
if __name__=='__main__':unittest.main()
