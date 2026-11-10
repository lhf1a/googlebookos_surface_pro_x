"""Full-image assembly must not turn target baselines into deployment checksums."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import ast
import subprocess

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import full_image as m
import runtime_boot_policy as boot


def digest(data):
    return hashlib.sha256(data).hexdigest().encode()


def line(data, name):
    return digest(data) + b'  ' + name.encode() + b'\n'


class AssemblyChecksums(unittest.TestCase):
    def test_target_baseline_survives_real_assembly(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            runtime = root / 'full-image-runtime'
            baseline = line(b'original system library', 'libstagefright.so')
            libraries = line(b'replacement library', 'libstagefright.so')
            bodies = {
                'init': b'#!/bin/sh\n' + m.ANCHOR,
                'bin/dropbear': b'identity fixture', 'bin/start-ssh': b'script fixture',
                'etc/shadow': b'not a real credential',
                'etc/renderer/libstagefright.so': b'replacement library',
                'etc/renderer/BASELINE.sha256': baseline,
                'etc/renderer/LIBRARIES.sha256': libraries,
                'etc/renderer/SHA256SUMS': line(baseline, 'BASELINE.sha256') + line(libraries, 'LIBRARIES.sha256'),
            }
            for name, data in bodies.items():
                path = runtime / 'files' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            (runtime / 'records.json').write_text(json.dumps({n: m.new_record(0o100644) for n in bodies}))
            output = root / 'assembled.gz'
            m.assemble_runtime(root, output)
            _, actual = m.records_from_gzip(output)
            self.assertEqual(actual['etc/renderer/BASELINE.sha256'], baseline)
            self.assertEqual(actual['etc/renderer/SHA256SUMS'], bodies['etc/renderer/SHA256SUMS'])

    def test_p010_hash_commands_removed_real_errors_retained(self):
        before = (ROOT/'probe/test2/sq2-p010-renderer-stage').read_bytes()
        after = boot.without_hash_checks('bin/sq2-p010-renderer-stage', before)
        self.assertNotIn(b'sha256sum', after)
        self.assertIn(b'set -eu', after)
        self.assertIn(b'cp "$source/libsq2-p010-renderer.so"', after)
        self.assertIn(b'mount --bind "$stage/merged" "$target"', after)
        self.assertEqual(boot.without_hash_checks('bin/sq2-p010-renderer-stage',after),after)
        result = subprocess.run(['sh','-c',after.decode().replace('$(cat /proc/cmdline)','sq2.video.c2=experimental')],capture_output=True)
        # No real /newroot means genuine prerequisites still fail, not fake success.
        self.assertNotEqual(result.returncode,0)

    def test_known_source_helpers_are_syntactically_valid(self):
        names=['audio-framework-boot','framework-suspend-start','qti-system-display-stage',
               'touch-calibration-boot','lte-framework-boot','closed-vulkan-start',
               'video-decoder-lab-start','prepare-native-gpu','apply-security-overlay',
               'quest2-hidl-stage','usb-functions-stage','radio-stage','qualcomm-full-start']
        for name in names:
            with self.subTest(name=name):
                paths={'lte-framework-boot':'probe/lte/lte-framework-boot',
                       'radio-stage':'probe/lte/radio-stage',
                       'qualcomm-full-start':'probe/graphics/qualcomm-full-start'}
                if name=='qti-system-display-stage':
                    source=m.module('test_qti_stage_source','pack-sq2-qti-system-display.py').stage_script()
                    if isinstance(source,str): source=source.encode()
                else:
                    source=(ROOT/paths.get(name,'probe/test2/'+name)).read_bytes()
                result=boot.without_hash_checks('bin/'+name,source)
                for line in result.splitlines():
                    if b'sha256sum' in line:
                        self.assertEqual(name,'touch-calibration-boot')
                        self.assertTrue(line.startswith(b'device=$('))
                self.assertEqual(boot.without_hash_checks('bin/'+name,result),result)

    def test_unknown_hash_control_flow_is_not_blindly_deleted(self):
        with self.assertRaisesRegex(ValueError,'Unrecognized hash command'):
            boot.without_hash_checks('bin/unknown',b'#!/bin/sh\nif sha256sum -c file; then echo yes; fi\n')

    def test_universal_build_has_no_hash_calls_or_gates(self):
        tree=ast.parse((ROOT/'scripts/disk_image.py').read_text())
        build=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='build')
        for call in (n for n in ast.walk(build) if isinstance(n,ast.Call)):
            if isinstance(call.func,ast.Name):
                self.assertNotIn(call.func.id,('sha','region'))
            if isinstance(call.func,ast.Attribute):
                self.assertNotEqual(call.func.attr,'sha256')
        helper=(ROOT/'probe/test2/prepare-image-ssd').read_text()
        self.assertNotIn('sha256sum',helper)
        self.assertIn('ambiguous-image-disk',helper)
        self.assertIn('missing-partition-',helper)


if __name__ == '__main__':
    unittest.main()
