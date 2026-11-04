from pathlib import Path
import subprocess,sys,tempfile,unittest
R=Path(__file__).resolve().parents[1]
HARNESS='from pathlib import Path\nimport sys,tarfile,tempfile,subprocess\nwith tempfile.TemporaryDirectory() as td:\n d=Path(td);p=Path(sys.argv[1])\n if p.suffix==\'.tar\':\n  with tarfile.open(p) as t:body=t.extractfile(\'probe/touch-calibration/SlideCapture.java\').read()\n else:body=p.read_bytes()\n (d/\'SlideCapture.java\').write_bytes(body)\n (d/\'Check.java\').write_text(\'import org.surface.touchcalibration.*;\\nclass Check {public static void main(String[]x){\\n SlideCapture s=new SlideCapture();s.first=0;s.last=12000;s.moves=12;s.path=.2756460301128977;s.span=.7875709367790906;s.peak=2;s.multiFrames=75;\\n System.out.println("observed_case_accepted="+s.enough()+" ghost_not_clean="+!s.clean());\\n SlideCapture e=new SlideCapture();System.out.println("empty_rejected="+!e.enough());\\n e.first=0;e.last=20000;e.peak=1;System.out.println("stationary_rejected="+!e.enough());\\n s.cancels=1;System.out.println("cancel_rejected="+!s.enough());\\n}}\\n\')\n subprocess.run([\'javac\',\'-encoding\',\'UTF-8\',\'-d\',str(d),str(d/\'SlideCapture.java\'),str(d/\'Check.java\')],check=True)\n subprocess.run([\'java\',\'-cp\',str(d),\'Check\'],check=True)\n'
class SlideLimits(unittest.TestCase):
 def test_observed_case_and_safety(self):
  with tempfile.TemporaryDirectory() as td:
   f=Path(td)/'check.py';f.write_text(HARNESS)
   p=subprocess.run([sys.executable,str(f),str(R/'probe/touch-calibration/SlideCapture.java')],capture_output=True,text=True)
   self.assertEqual(p.returncode,0,p.stderr);print(p.stdout)
   for s in ['observed_case_accepted=true ghost_not_clean=true','empty_rejected=true','stationary_rejected=true','cancel_rejected=true']:self.assertIn(s,p.stdout)
if __name__=='__main__':unittest.main()
