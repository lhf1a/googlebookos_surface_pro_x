from pathlib import Path
import ast,unittest
R=Path(__file__).resolve().parents[1]
class DefaultTouch(unittest.TestCase):
 def test_standard_and_backend_defaults(self):
  for n in ['build.py','build-surface-pro-x.py']:
   calls=[c for c in ast.walk(ast.parse((R/'scripts'/n).read_text())) if isinstance(c,ast.Call) and c.args and isinstance(c.args[0],ast.Constant)]
   repair=next(c for c in calls if c.args[0].value=='--touch-repair');self.assertEqual(next(k.value.value for k in repair.keywords if k.arg=='default'),'on')
   bundle=next(c for c in calls if c.args[0].value=='--touch-bundle');default=next(k.value for k in bundle.keywords if k.arg=='default');self.assertEqual(default.right.value,'private/build-inputs/touch')
  self.assertIn('TOUCH-CALIBRATION.md',(R/'README.md').read_text());self.assertIn('TOUCH-CALIBRATION.md',(R/'README.zh-CN.md').read_text())
if __name__=='__main__':unittest.main()
