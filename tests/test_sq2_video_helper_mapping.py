import importlib.util
from pathlib import Path
import unittest

R=Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('video',R/'scripts/sq2-video-boot-payload.py')
v=importlib.util.module_from_spec(s);s.loader.exec_module(v)

class RuntimeHelpers(unittest.TestCase):
    def setUp(self):
        self.files={'bin/'+dst:(0o100755,(R/'probe/test2'/src).read_bytes().replace(b'\r\n',b'\n'))
                    for dst,src in v.HELPERS.items()}
    def test_runtime_helpers_pass(self):
        v.verify_helpers(self.files,R)
    def test_legacy_same_name_cannot_replace_runtime_stage(self):
        self.files['bin/video-codec-manifest-stage']=(0o100755,(R/'probe/test2/video-codec-manifest-stage').read_bytes())
        with self.assertRaisesRegex(ValueError,'overwritten'):
            v.verify_helpers(self.files,R)
    def test_missing_helper_rejected(self):
        del self.files['bin/video-runtime-framework-boot']
        with self.assertRaises(ValueError):v.verify_helpers(self.files,R)

if __name__=='__main__':unittest.main()
