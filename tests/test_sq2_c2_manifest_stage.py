"""Execute the actual early-init helper in an isolated path fixture."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import hashlib
import xml.etree.ElementTree as ET

SOURCE = Path(__file__).resolve().parents[1] / 'probe/test2/video-codec-manifest-stage'

class ManifestStageTest(unittest.TestCase):
    def run_case(self, command, guard='Y', overlay=True, existing=False):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name, data in [('proc/cmdline', command),
                               ('proc/mounts', 'overlay /newroot/vendor overlay rw 0 0\n' if overlay else ''),
                               ('sys/module/suspend/parameters/screen_only', guard)]:
                p = root / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(data)
            directory = root / 'newroot/vendor/etc/vintf/manifest'
            directory.mkdir(parents=True)
            (root / 'newroot/vendor/bin').mkdir(parents=True)
            (root / 'newroot/vendor/etc/init').mkdir(parents=True)
            bundle = root / 'etc/sq2-c2-experimental'
            bundle.mkdir(parents=True)
            for name, content in [('service', b'fixture-only'), ('service.rc', b'fixture-rc')]:
                (bundle/name).write_bytes(content)
            (bundle/'SHA256SUMS').write_text(''.join(
                hashlib.sha256((bundle/name).read_bytes()).hexdigest()+'  '+name+'\n'
                for name in ['service', 'service.rc']))
            target = directory / 'sq2-c2-experimental.xml'
            if existing:
                target.write_text('existing')
            code = SOURCE.read_text().replace('/proc/', tmp+'/proc/').replace('/sys/', tmp+'/sys/')
            # Preserve the literal mount-table match while redirecting file writes.
            code = code.replace('target=/newroot/', 'target='+tmp+'/newroot/')
            code = code.replace('source=/etc/', 'source='+tmp+'/etc/')
            code = code.replace('binary=/newroot/', 'binary='+tmp+'/newroot/')
            code = code.replace('rc=/newroot/', 'rc='+tmp+'/newroot/')
            code = code.replace('test -d /newroot/', 'test -d '+tmp+'/newroot/')
            result = subprocess.run(['sh', '-c', code], capture_output=True, text=True)
            return result.returncode, target.read_text() if target.exists() else None

    def test_default_and_rescue_are_noops(self):
        for cmd in ['', 'sq2.video.c2=0', 'sq2.video.c2=experimental sq2.rescue=1']:
            self.assertEqual(self.run_case(cmd), (0, None))

    def test_opt_in_writes_valid_instance(self):
        rc, data = self.run_case('sq2.video.c2=experimental')
        self.assertEqual(rc, 0)
        tree = ET.fromstring(data)
        self.assertEqual(tree.findtext('hal/fqname'), 'IComponentStore/default1')
        self.assertEqual(tree.findtext('hal/version'), '2')

    def test_guards_do_not_write(self):
        for args in [dict(guard='N'), dict(overlay=False)]:
            rc, data = self.run_case('sq2.video.c2=experimental', **args)
            self.assertNotEqual(rc, 0)
            self.assertIsNone(data)
        rc, data = self.run_case('sq2.video.c2=experimental', existing=True)
        self.assertNotEqual(rc, 0)
        self.assertEqual(data, 'existing')

if __name__ == '__main__':
    unittest.main()
