from pathlib import Path
import subprocess,sys,tempfile,unittest
R=Path(__file__).resolve().parents[1]
HARNESS='from pathlib import Path\nimport subprocess,tempfile,sys,tarfile\narg=Path(sys.argv[1])\nif arg.suffix==\'.tar\':\n with tarfile.open(arg) as t:s=t.extractfile(\'probe/touch-calibration/MainActivity.java\').read().decode()\nelse:s=arg.read_text()\nmethods=s[s.index(\'  void detectionResult(){\'):s.index(\'  void exportPatch(){\')]\ncode=\'import java.util.*;\\nclass Check {\\n static class LinearLayout {}\\n boolean patchAvailable=true,noiseDetected=true,noisePassed=false,noiseAuto=false;\\n String exportNotice="",lastText="",lastCommand="";\\n List<String> labels=new ArrayList<>();List<Runnable> actions=new ArrayList<>();\\n LinearLayout layout(String s){lastText=s;labels.clear();actions.clear();return new LinearLayout();}\\n void message(String s){layout(s);}\\n void button(LinearLayout l,String s,Runnable r){labels.add(s);actions.add(r);}\\n void holdButton(LinearLayout l,String s,Runnable r){button(l,s,r);}\\n void send(String s,String b){lastCommand=s;}\\n void prepareRetest(){} void noiseIntro(){} void finish(){} void exportPatch(){}\\n\'+methods+\'\\n public static void main(String[]a){Check c=new Check();c.detectionResult();\\n if(c.lastCommand.equals("noise-export"))c.patchReady();\\n System.out.println("result="+c.lastText.replace("\\\\n"," ").trim());\\n System.out.println("button="+c.labels.get(0));c.actions.get(0).run();\\n System.out.println("command="+c.lastCommand+" auto_retest="+c.noiseAuto);\\n c.patchAvailable=false;c.noisePassed=false;c.noiseDetected=true;c.detectionResult();\\n System.out.println("no_safe_candidate="+c.lastText);\\n }\\n}\\n\'\nwith tempfile.TemporaryDirectory() as td:\n d=Path(td);(d/\'Check.java\').write_text(code)\n subprocess.run([\'javac\',\'-encoding\',\'UTF-8\',\'-d\',str(d),str(d/\'Check.java\')],check=True)\n subprocess.run([\'java\',\'-cp\',str(d),\'Check\'],check=True)\n'
class IdleResult(unittest.TestCase):
 def test_real_result_methods(self):
  with tempfile.TemporaryDirectory() as td:
   f=Path(td)/'check.py';f.write_text(HARNESS)
   p=subprocess.run([sys.executable,str(f),str(R/'probe/touch-calibration/MainActivity.java')],capture_output=True,text=True)
   self.assertEqual(p.returncode,0,p.stderr);print(p.stdout)
   self.assertIn('result=已生成修复方案。',p.stdout)
   self.assertIn('button=应用修复并复测',p.stdout)
   self.assertIn('command=noise-apply auto_retest=true',p.stdout)
   self.assertIn('no_safe_candidate=已检测到异常，暂未生成可安全应用的方案。',p.stdout)
if __name__=='__main__':unittest.main()
