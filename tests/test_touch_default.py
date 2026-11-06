from pathlib import Path
import ast,unittest
R=Path(__file__).resolve().parents[1]
class DefaultTouch(unittest.TestCase):
 def test_standard_and_backend_defaults(self):
     # Current boot is preserved as a checked 6.18 bundle, not rebuilt through old overlays.
     for name in ['build.py', 'build-surface-pro-x.py', 'build-source-boot.py', 'build-source-disk.py', 'build-sq2-default.py']:
         text = (R/"scripts"/name).read_text()
         self.assertIn("build-lts.py", text)
         self.assertNotIn("6.3.8", text)
if __name__=='__main__':unittest.main()
