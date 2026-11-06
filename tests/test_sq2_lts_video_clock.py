import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

R=Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('fix', R/'scripts/fix-sq2-lts-video-clock.py')
fix=importlib.util.module_from_spec(s);s.loader.exec_module(fix)

class ClockProvider(unittest.TestCase):
    def check(self, compatible=None, status=None):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td);clock=root/'soc@0/clock-controller@ab00000'
            if compatible is not None:
                clock.mkdir(parents=True);(clock/'compatible').write_bytes(compatible)
            if status is not None:(clock/'status').write_bytes(status)
            body=fix.CLOCK_BRANCH.replace('/sys/firmware/devicetree/base', td)
            body=body.replace('/sys/module/videocc_sm8150', td+'/no-resident-clock')
            return subprocess.run(['sh','-c','set -eu\nmodprobe() { echo "LOAD=$*"; }\n'+body],capture_output=True,text=True)

    def test_native_clock_not_republished(self):
        p=self.check(b'qcom,sc8180x-videocc\0qcom,sm8150-videocc\0')
        self.assertEqual(p.returncode,0);self.assertIn('SQ2_VIDEO_CLOCK_NATIVE',p.stdout)
        self.assertNotIn('LOAD=',p.stdout)

    def test_legacy_clock_overlay_retained(self):
        p=self.check();self.assertEqual(p.returncode,0)
        self.assertIn('LOAD=sq2_iris1_identify allow=1',p.stdout)

    def test_wrong_clock_not_accepted(self):
        p=self.check(b'qcom,other-videocc\0')
        self.assertNotEqual(p.returncode,0);self.assertNotIn('LOAD=',p.stdout)

    def test_disabled_clock_not_accepted(self):
        p=self.check(b'qcom,sc8180x-videocc\0qcom,sm8150-videocc\0',b'disabled\0')
        self.assertNotEqual(p.returncode,0);self.assertIn('VIDEO_CLOCK_DISABLED',p.stdout)
