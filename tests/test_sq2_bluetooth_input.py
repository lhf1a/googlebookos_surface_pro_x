import gzip
import hashlib
import importlib.util
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('btpack', ROOT/'scripts/pack-sq2-bluetooth-input.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


class BluetoothInputTests(unittest.TestCase):
    def test_boot_and_all_packaging_paths(self):
        helper = (ROOT/'probe/test2/bluetooth-framework-boot').read_text()
        self.assertLess(helper.index('\nstart_input_watch\n'), helper.index('# Do not bring'))
        self.assertIn("grep -q '/bin/bluetooth-input-watch'", helper)
        self.assertNotIn('settings put', helper)
        for name in ('scripts/build-source-boot.py', 'scripts/pack-sq2-power-connectivity.sh',
                     'scripts/pack-googlebook-internal-ssd.sh'):
            self.assertIn('bluetooth-input-watch', (ROOT/name).read_text())

    def test_addon_exact_delta_and_credentials(self):
        c = pack.cpio
        helper = (ROOT/'probe/test2/bluetooth-framework-boot').read_bytes()
        begin = helper.index(b'# The BLE HID stack disconnects')
        end = helper.index(b'# Do not bring an already-owned controller')
        old = helper[:begin]+helper[end:]
        def member(items):
            raw = b''.join(c.entry(n, body, mode) for n, (mode, body) in items.items())
            raw += c.entry('TRAILER!!!', b'', 0)
            return gzip.compress(raw, mtime=0)
        base = member({'bin': (0o40755, b''), 'init': (0o100755, b'pristine'),
                       'bin/bluetooth-framework-boot': (0o100755, old)})
        cred = member({'etc/shadow': (0o100600, b'fixture-not-real'),
                       'bin/dropbear': (0o100755, b'fixture-not-real')})
        base += cred
        addon, audit = pack.build(base, hashlib.sha256(base).hexdigest())
        self.assertTrue(addon.endswith(cred))
        self.assertEqual(len(audit['changed']), 2)
        self.assertEqual(c.entries(gzip.decompress(base+addon))['init'][1], b'pristine')
        with self.assertRaises(ValueError):
            pack.build(base, '0'*64)
        with self.assertRaises(ValueError):
            pack.build(base+member({'unexpected': (0o100644, b'')}),
                       hashlib.sha256(base+member({'unexpected': (0o100644, b'')})).hexdigest())

    @unittest.skipUnless(hasattr(os, 'geteuid') and os.geteuid() == 0,
                         'root Linux filesystem fixture uses mknod/chown')
    def test_actual_watcher_filters_permissions_labels_and_reuse(self):
        for provenance, label_ok, expected in (
                ('virtual/misc/uhid/0005:046D:B034.0001', True, True),
                ('virtual/misc/uhid/0003:046D:B034.0001', True, False),
                ('platform/serial1-0/keyboard', True, False),
                ('virtual/misc/uhid/0005:046D:B034.0001', False, False)):
            with self.subTest(provenance=provenance, label_ok=label_ok), tempfile.TemporaryDirectory() as td:
                t = Path(td)
                ev = t/'sys/class/input/event22'
                ev.mkdir(parents=True)
                device = t/'devices'/provenance/'input/input30'
                device.mkdir(parents=True)
                (device/'name').write_text('MX Master 3S Mouse\n')
                (ev/'device').symlink_to(device)
                (ev/'dev').write_text('13:90\n')
                (t/'dev/input').mkdir(parents=True)
                (t/'run').mkdir()
                node = t/'dev/input/event22'
                os.mknod(node, stat.S_IFCHR | 0o600, os.makedev(13, 90))
                original_inode = node.stat().st_ino
                s = (ROOT/'probe/test2/bluetooth-input-watch').read_text()
                for a, b in (('/sys/class/input', str(t/'sys/class/input')),
                             ('/dev/input', str(t/'dev/input')),
                             ('/dev/sq2-', str(t/'dev/sq2-')), ('/run/', str(t/'run')+'/')):
                    s = s.replace(a, b)
                s = s.replace('set -u', 'nsenter() { return '+('0' if label_ok else '1')+'; }\nset -u', 1)
                script = t/'watch.sh'
                script.write_text(s)
                result = subprocess.run(['sh', str(script), '--once'], capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual('BT_INPUT_REPUBLISHED=' in result.stdout, expected)
                st = node.stat()
                self.assertEqual((stat.S_IMODE(st.st_mode), st.st_gid),
                                 (0o660, 1004) if expected else (0o600, 0))
                if expected:
                    inode = st.st_ino
                    result = subprocess.run(['sh', str(script), '--once'], capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.stdout, '')
                    self.assertEqual(node.stat().st_ino, inode)
                else:
                    self.assertEqual(st.st_ino, original_inode)


if __name__ == '__main__':
    unittest.main()
