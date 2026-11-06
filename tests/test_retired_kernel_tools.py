"""Retired 6.3.8 entry points stay retired; optional compilers share the LTS ABI."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHELL_BUILDERS = (
    'build-sq2-ipa-gsi-state-read.sh', 'build-sq2-ipa-iort-read.sh',
    'build-sq2-ipa-version-read.sh', 'build-sq2-rmnet.sh',
    'build-sq2-ipa41-lab.sh', 'build-sq2-ipa41-reference.sh',
)
PYTHON_BUILDERS = {
    'build-sq2-hfi-overlay.py': ('--videocc-phandle', '1'),
    'build-sq2-iris1-identify.py': (),
    'build-sq2-venus-fw-overlay.py': ('--smmu-phandle', '1'),
}


class RetiredKernelTools(unittest.TestCase):
    def test_retired_files_absent_and_reference_patches_retained(self):
        manifest = json.loads((ROOT/'configs/retired-kernel-tools.json').read_text())
        self.assertEqual(manifest['active_kernel_series'], '6.18')
        records = manifest['retired']
        self.assertEqual(len(records), len({r['path'] for r in records}))
        for row in records:
            with self.subTest(path=row['path']):
                self.assertFalse((ROOT/row['path']).exists())
                self.assertEqual(len(row['sha256']), 64)
                self.assertGreater(row['bytes'], 0)
        for path in manifest['retained_reference_patches']:
            self.assertTrue((ROOT/path).is_file(), path)

    def test_module_compilers_reject_legacy_source_before_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            source, build = root/'source', root/'build'
            source.mkdir(); build.mkdir()
            (source/'Makefile').write_text('VERSION = 6\nPATCHLEVEL = 3\nSUBLEVEL = 8\n')
            (build/'include/config').mkdir(parents=True)
            (build/'include/config/kernel.release').write_text('6.3.8-sq2-android1\n')
            for version, expected in [('3', 'Unsupported kernel source 6.3.8'),
                                      ('18', 'Unsupported module ABI 6.3.8-sq2-android1')]:
                (source/'Makefile').write_text('VERSION = 6\nPATCHLEVEL = '+version+'\nSUBLEVEL = '+('8' if version == '3' else '60')+'\n')
                for name in SHELL_BUILDERS + tuple(PYTHON_BUILDERS):
                    with self.subTest(builder=name):
                        output = root/name
                        if name in PYTHON_BUILDERS:
                            command = [sys.executable, str(ROOT/'scripts'/name),
                                       '--kernel', str(source), '--kernel-output', str(build),
                                       '--stage', str(output), *PYTHON_BUILDERS[name]]
                        else:
                            command = ['bash', str(ROOT/'scripts'/name), str(source), str(build), str(output)]
                        result = subprocess.run(command, capture_output=True, text=True)
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn(expected, result.stderr)
                        self.assertFalse(output.exists())

    def test_reference_preparer_has_no_implicit_kernel(self):
        result = subprocess.run([sys.executable, str(ROOT/'scripts/prepare-sq2-venus-iris1.py'),
                                 '--output', 'unused-reference-output'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn('--kernel', result.stderr)
        self.assertFalse((ROOT/'unused-reference-output').exists())

    def test_deploy_rejects_retired_display_policy_before_device_io(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location('deploy_retirement_test', ROOT/'scripts/deploy-experimental-usb.py')
        deploy = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(deploy)
        deploy.verify_display_profiles({}, {}, Path('unused-image'))
        with self.assertRaisesRegex(ValueError, 'retired'):
            deploy.verify_display_profiles({'display_profiles': {}}, {}, Path('unused-image'))


if __name__ == '__main__':
    unittest.main()
