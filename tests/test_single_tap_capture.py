"""The 15-point sampler must advance from actual capture, never idle time."""
from pathlib import Path
import subprocess,tempfile,unittest
R=Path(__file__).resolve().parents[1]
class TapPacing(unittest.TestCase):
 def test_wait_capture_one_second_release_and_duplicates(self):
  code=r"""
import org.surface.touchcalibration.SingleTapCapture;
public class Timing {
 static void ok(boolean b){if(!b)throw new AssertionError();}
 public static void main(String[] args){
  SingleTapCapture c=new SingleTapCapture(3000,42);
  c.tick(180000);ok(c.trial(180000)==0&&!c.finished(180000));
  c.event(1,1,180001);c.tick(190000);ok(c.trial(190000)==0); // orphan UP is not a sample
  c.event(0,1,200000);c.tick(205000);ok(c.trial(205000)==0); // held, no automatic advance
  c.event(1,1,205100);c.tick(206099);ok(c.trial(206099)==0);
  c.event(0,1,205300);c.event(5,2,205320);c.event(6,2,205350);c.event(1,1,205400);
  ok(c.starts[0]==3&&c.groups[0]==2&&c.peak[0]==2&&c.capturedAt[0]==205100);
  c.tick(206100);ok(c.trial(206100)==1&&c.completed()==1); // exactly >=1s after first complete sample
  c.tick(300000);ok(c.trial(300000)==1);
  c.event(0,1,300001);c.event(3,1,300020);c.tick(310000);ok(c.trial(310000)==1&&!c.captured());
  c.event(0,1,310001);c.event(1,1,310100);c.event(0,1,310900);c.tick(311100);ok(c.trial(311100)==1);
  c.event(1,1,311200);c.tick(311200);ok(c.trial(311200)==2); // held duplicate cannot leak to unseen target
  long t=312000;
  for(int i=2;i<15;i++){c.event(0,1,t);c.event(1,1,t+100);c.tick(t+1099);ok(c.trial(t+1099)==i);c.tick(t+1100);ok(c.completed()==i+1);t+=2000;}
  ok(c.finished(t)&&c.complete&&c.completed()==15);
  System.out.println("idle_never_advances=1 waits_full_click=1 delay_999ms_stays=1 delay_1000ms_advances=1 duplicates_same_trial=1 cancel_not_sample=1 held_touch_waits=1 completion_requires_15=1");
 }
}
"""
  with tempfile.TemporaryDirectory() as td:
   d=Path(td);(d/'Timing.java').write_text(code)
   subprocess.run(['javac','-d',str(d),str(R/'probe/touch-calibration/SingleTapCapture.java'),str(d/'Timing.java')],check=True)
   subprocess.run(['java','-cp',str(d),'Timing'],check=True)
if __name__=='__main__':unittest.main()
