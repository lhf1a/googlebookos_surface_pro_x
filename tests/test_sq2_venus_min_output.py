from pathlib import Path
import subprocess, tempfile, unittest
R=Path(__file__).resolve().parents[1]
class RealMinOutput(unittest.TestCase):
 def test_actual_control_helper_preserves_firmware_counts_and_errors(self):
  with tempfile.TemporaryDirectory() as td:
   exe=Path(td)/'check'
   p=subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',str(R/'tests/sq2_venus_min_output_test.c'),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(p.returncode,0,p.stderr)
   p=subprocess.run([str(exe)],capture_output=True,text=True)
   self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   self.assertIn('MIN_OUTPUT_PASS',p.stdout)
if __name__=='__main__': unittest.main()
