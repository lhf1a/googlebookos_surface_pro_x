"""Verify actual boot bundle selection and refusal of stale patch artifacts."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('boot', ROOT/'scripts/build-source-boot.py')
boot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(boot)


class TouchBundle(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ('scripts', 'configs'):
            (self.root/name).mkdir()
        (self.root/'probe/test2').mkdir(parents=True)
        (self.root/'probe/test2/sq2-touch-calibration.hpp').write_bytes(b'calibration header')
        (self.root/'configs/iptsd-sq2.conf').write_bytes(b'[Contacts]\nAspectMax = 2.0\n')
        (self.root/'configs/iptsd-sq2-calibration.conf').write_bytes(b'[Touchscreen]\nCalibrationX = 0:0 1:1\n')
        for name in ('patch-iptsd-sq2-touch.py', 'patch-iptsd-sq2-heatmap.py', 'patch-iptsd-sq2-calibration.py'):
            (self.root/'scripts'/name).write_bytes(name.encode())

    def bundle(self, mode, calibration='on'):
        p = self.root/'out/test2'/{'on':'iptsd','off':'iptsd-no-ghost-fix','edge-only':'iptsd-edge-only'}[mode]
        p.mkdir(parents=True)
        files = {'iptsd': mode.encode(), 'iptsd.conf': (self.root/'configs/iptsd-sq2.conf').read_bytes()
                 if mode != 'off' else b'[DFT]\nButtonMinMag = 4000\n'}
        if calibration == 'on':
            files['iptsd.conf'] += b'\n' + (self.root/'configs/iptsd-sq2-calibration.conf').read_bytes()
        patches = {'on':['patch-iptsd-sq2-touch.py', 'patch-iptsd-sq2-heatmap.py'],
                   'off':[], 'edge-only':['patch-iptsd-sq2-heatmap.py']}[mode]
        if calibration == 'on': patches += ['patch-iptsd-sq2-calibration.py']
        meta = dict(ghost_touch_fix=mode, patches=patches,
                    touch_calibration=calibration,
                    calibration_header_sha256=(boot.sha((self.root/'probe/test2/sq2-touch-calibration.hpp').read_bytes()) if calibration=='on' else None),
                    patch_sha256={n: boot.sha((self.root/'scripts'/n).read_bytes()) for n in patches},
                    sha256={n: boot.sha(b) for n, b in files.items()})
        for name, body in files.items():
            (p/name).write_bytes(body)
        (p/'build-options.json').write_text(json.dumps(meta))
        return p, meta

    def test_both_modes_select_distinct_payloads(self):
        for mode in ('on', 'off', 'edge-only'):
            self.bundle(mode)
            files, meta = boot.touch_bundle(self.root, mode)
            self.assertEqual(files['iptsd'], mode.encode())
            self.assertEqual(meta['ghost_touch_fix'], mode)

    def test_old_patch_binary_rejected(self):
        self.bundle('on')
        (self.root/'scripts/patch-iptsd-sq2-heatmap.py').write_text('new optimization')
        with self.assertRaisesRegex(ValueError, 'Stale'):
            boot.touch_bundle(self.root, 'on')

    def test_calibration_opt_out(self):
        self.bundle('on','off')
        files,meta=boot.touch_bundle(self.root,'on','off')
        self.assertNotIn(b'CalibrationX',files['iptsd.conf'])
        with self.assertRaisesRegex(ValueError,'Stale'):
            boot.touch_bundle(self.root,'on','on')

    def test_changed_calibration_header_rejected(self):
        self.bundle('on')
        (self.root/'probe/test2/sq2-touch-calibration.hpp').write_bytes(b'changed curve evaluator')
        with self.assertRaisesRegex(ValueError,'Stale'):
            boot.touch_bundle(self.root,'on')

    def test_corrupt_binary_rejected(self):
        p, _ = self.bundle('on')
        (p/'iptsd').write_bytes(b'corrupted')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            boot.touch_bundle(self.root, 'on')

    def test_wrong_mode_rejected(self):
        p, meta = self.bundle('off')
        meta['ghost_touch_fix'] = 'on'
        (p/'build-options.json').write_text(json.dumps(meta))
        with self.assertRaisesRegex(ValueError, 'mismatched'):
            boot.touch_bundle(self.root, 'off')

    def test_rehashed_wrong_config_rejected(self):
        p, meta = self.bundle('on')
        data = b'[Contacts]\nActivationThreshold = 100\n'
        (p/'iptsd.conf').write_bytes(data)
        meta['sha256']['iptsd.conf'] = boot.sha(data)
        (p/'build-options.json').write_text(json.dumps(meta))
        with self.assertRaisesRegex(ValueError, 'configuration'):
            boot.touch_bundle(self.root, 'on')


if __name__ == '__main__':
    unittest.main()
