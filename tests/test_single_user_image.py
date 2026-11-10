import gzip
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import single_user_payload as policy


def archive(entries):
    cpio = policy.cpio_module()
    return gzip.compress(b''.join(cpio.entry(n, b, m) for n, (m, b) in entries.items()) +
                         cpio.entry('TRAILER!!!', b'', 0), mtime=0)


def fixture(identity=True):
    base = archive({
        'init': (0o100755, b'#!/bin/sh\nfail() { exit 1; }\n'
                 b' el2-terminal-autostart 10 > /logs/login 2>&1 &\n'
                 b'echo other-stage\nexec native-handoff /newroot\n'),
        'bin/keep': (0o100755, b'keep'),
        'lib/firmware/keep': (0o100644, b'firmware'),
    })
    tail = archive({'etc/shadow': (0o100600, b'fixture-only'),
                    'bin/dropbear': (0o100755, b'fixture-only')}) if identity else b''
    return base + tail, tail


class SingleUserImage(unittest.TestCase):
    def test_effective_delta_anonymous_and_private_and_repeat(self):
        for identity in (False, True):
            with self.subTest(identity=identity):
                base, tail = fixture(identity)
                result, report = policy.integrate(base, hashlib.sha256(base).hexdigest())
                c = policy.cpio_module()
                before, after = c.entries(gzip.decompress(base)), c.entries(gzip.decompress(result))
                self.assertEqual(report['changed'], ['bin/apply-single-user', 'init'])
                self.assertEqual({n for n in before if before[n] != after[n]}, {'init'})
                self.assertEqual(after['init'][1].count(policy.HOOK), 1)
                self.assertNotIn(b'el2-terminal-autostart 10', after['init'][1])
                self.assertTrue(after['init'][1].endswith(policy.HOOK + policy.ANCHOR))
                if identity:
                    self.assertTrue(result.endswith(tail))
                    self.assertTrue(result.startswith(base[:-len(tail)]))
                else:
                    self.assertTrue(result.startswith(base))
                twice, _ = policy.integrate(result, hashlib.sha256(result).hexdigest())
                self.assertEqual(c.entries(gzip.decompress(twice)), after)
                self.assertFalse(report['hardware_boot_tested'])
                self.assertFalse(report['userdata_written'])

    def test_bad_parent_anchor_identity_and_collision(self):
        base, _ = fixture()
        with self.assertRaisesRegex(ValueError, 'SHA256'):
            policy.integrate(base, '0' * 64)
        wrong = archive({'init': (0o100755, b'#!/bin/sh\nexit 0\n')})
        with self.assertRaisesRegex(ValueError, 'handoff'):
            policy.integrate(wrong, hashlib.sha256(wrong).hexdigest())
        unknown = archive({'init': (0o100755, b'#!/bin/sh\nel2-terminal-autostart "$target"\n' + policy.ANCHOR)})
        with self.assertRaisesRegex(ValueError, 'Unknown Terminal'):
            policy.integrate(unknown, hashlib.sha256(unknown).hexdigest())
        bad = base + archive({'etc/shadow': (0o100600, b'fixture'),
                             'bin/dropbear': (0o100755, b'fixture'),
                             'init': (0o100755, policy.ANCHOR)})
        with self.assertRaisesRegex(ValueError, 'Identity member'):
            policy.integrate(bad, hashlib.sha256(bad).hexdigest())
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            parent = p / 'base.gz'
            parent.write_bytes(base)
            output = p / 'candidate.gz'
            report = policy.build(parent, output, hashlib.sha256(base).hexdigest())
            self.assertEqual(hashlib.sha256(output.read_bytes()).hexdigest(), report['sha256'])
            with self.assertRaisesRegex(ValueError, 'fresh'):
                policy.build(parent, output, hashlib.sha256(base).hexdigest())
            self.assertEqual(parent.read_bytes(), base)

    def test_real_helper_property_precedence_idempotence_and_data_untouched(self):
        helper = (ROOT / 'probe/test2/apply-single-user').read_text()
        # Only substitute mount for an unprivileged fixture; execute real awk,
        # property filtering and shell control flow, not a model of that code.
        helper = helper.replace('mount --bind "$target" "$source"', 'cp "$target" "$source"')
        values = {'ro.fw.mu.headless_system_user': 'false',
                  'ro.fw.headless_system_user': 'false',
                  'ro.fw.system_user_split': 'false',
                  'fw.max_users': '1', 'fw.show_multiuserui': 'false'}
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            files = ['prop.default', 'default.prop', 'system/etc/prop.default', 'vendor/default.prop',
                     'odm/default.prop', 'system/build.prop', 'system/etc/build.prop', 'vendor/build.prop',
                     'vendor/etc/build.prop', 'product/build.prop', 'product/etc/build.prop',
                     'odm/build.prop', 'odm/etc/build.prop', 'system_ext/build.prop',
                     'system_ext/etc/build.prop', 'system_dlkm/etc/build.prop', 'vendor_dlkm/etc/build.prop']
            for rel in files:
                p = root / rel
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text('fixture.keep=yes\nro.hardware.vulkan=adreno\n' +
                            ''.join(' \t' + k + ' = wrong\n' + k + '=wrong-again\n' for k in values))
            sentinels = ['data/user/10/app/files/keep', 'data/system/users/10.xml',
                         'data/system/users/userlist.xml', 'data/misc/keep', 'data/user/0/keep']
            for name in sentinels:
                p = root / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(b'keep-original-user-credentials-settings')
            before = {str(p.relative_to(root)): p.read_bytes() for p in (root / 'data').rglob('*') if p.is_file()}
            for _ in range(2):
                result = subprocess.run(['sh', '-c', helper, 'policy', str(root)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                properties = [line for rel in files for line in (root / rel).read_text().splitlines()]
                for key, value in values.items():
                    self.assertEqual([s for s in properties if s.startswith(key + '=')], [key + '=' + value])
                for rel in files:
                    self.assertIn('fixture.keep=yes', (root / rel).read_text())
                    self.assertIn('ro.hardware.vulkan=adreno', (root / rel).read_text())
                self.assertEqual({str(p.relative_to(root)): p.read_bytes() for p in (root / 'data').rglob('*') if p.is_file()}, before)

    def test_helper_failure_propagates(self):
        helper = (ROOT / 'probe/test2/apply-single-user').read_text()
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            p = root / 'system/build.prop'
            p.parent.mkdir()
            p.write_text('ro.fw.mu.headless_system_user=true\n')
            helper = helper.replace('mount --bind "$target" "$source"', 'false')
            result = subprocess.run(['sh', '-c', helper, 'policy', str(root)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn(b'SURFACE_SINGLE_USER:', result.stdout)
            self.assertIn('=true', p.read_text())

    def test_cli_creates_candidate_without_touching_parent(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            base, _ = fixture()
            (p / 'base.gz').write_bytes(base)
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/build.py'), 'single-user',
                                     '--base', str(p / 'base.gz'), '--output', str(p / 'candidate.gz'),
                                     '--expected-base-sha256', hashlib.sha256(base).hexdigest()],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('fresh-image-single-user-candidate', result.stdout)
            self.assertEqual((p / 'base.gz').read_bytes(), base)


if __name__ == '__main__':
    unittest.main()
