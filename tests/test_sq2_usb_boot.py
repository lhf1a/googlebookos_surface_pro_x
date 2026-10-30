"""Run the real early USB stage in isolated fixtures; no device/service writes."""
import fnmatch
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

R = Path(__file__).resolve().parents[1]


class UsbStage(unittest.TestCase):
    def exercise(self, case):
        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            (t/'vendor/etc/vintf/manifest').mkdir(parents=True)
            (t/'source').mkdir()
            pristine = (R/'probe/test2/fstab.sq2-native').read_bytes().replace(b'\r\n', b'\n')
            policy = (R/'configs/sq2-usb-audio-policy.xml').read_bytes().replace(b'\r\n', b'\n')
            original = policy.replace(b'        <!-- USB Audio HAL -->\n        <xi:include href="usb_audio_policy_configuration.xml"/>\n\n', b'')
            self.assertEqual(hashlib.sha256(original).hexdigest(),
                             '7b5981137bcf324d1f66d331a134a4facbba758622c4567ec310eb7b26719022')
            (t/'fstab').write_bytes(pristine)
            (t/'vendor/etc/audio_policy_configuration.xml').write_bytes(original)
            sources = {'storage.fstab': 'sq2-usb-storage.fstab',
                       'audio_policy_configuration.xml': 'sq2-usb-audio-policy.xml',
                       'manifest.xml': 'sq2-usb-audio-manifest.xml'}
            files = {n: (R/'configs'/s).read_bytes().replace(b'\r\n', b'\n') for n, s in sources.items()}
            for n, b in files.items():
                (t/'source'/n).write_bytes(b)
            (t/'source/SHA256SUMS').write_text(''.join(
                hashlib.sha256(b).hexdigest()+'  '+n+'\n' for n, b in files.items()))
            (t/'cmdline').write_text({'no-flag': '', 'rescue': 'sq2.usb=experimental sq2.rescue=1',
                                     'off': 'sq2.usb=experimental sq2.usb=0'}.get(case, 'sq2.usb=experimental'))
            (t/'guard').write_text('N' if case == 'guardN' else 'Y')
            (t/'mounts').write_text('' if case == 'no-overlay' else 'overlay /newroot/vendor overlay rw 0 0\n')
            if case == 'tamper':
                (t/'source/storage.fstab').write_text('TAMPER')
            if case == 'unknown-policy':
                (t/'vendor/etc/audio_policy_configuration.xml').write_text('UNKNOWN')
            mock = t/'modprobe'
            mock.write_text('#!/bin/sh\nprintf "%s\\n" "$*" > "'+str(t/'module-args')+'"\nexit '+('1' if case == 'module-fail' else '0')+'\n')
            mock.chmod(0o755)
            text = (R/'probe/test2/usb-functions-stage').read_text()
            for old, new in {
                '/proc/cmdline': str(t/'cmdline'),
                '/sys/module/suspend/parameters/screen_only': str(t/'guard'),
                '/proc/mounts': str(t/'mounts'),
                'source=/etc/sq2-usb': 'source='+str(t/'source'),
                'policy=/newroot/vendor/etc/audio_policy_configuration.xml': 'policy='+str(t/'vendor/etc/audio_policy_configuration.xml'),
                'manifest=/newroot/vendor/etc/vintf/manifest/sq2-usb-audio.xml': 'manifest='+str(t/'vendor/etc/vintf/manifest/sq2-usb-audio.xml'),
                '/etc/fstab.sq2-native': str(t/'fstab'),
                '/bin/modprobe': str(mock),
            }.items():
                text = text.replace(old, new)
            script = t/'stage.sh'
            script.write_text(text)
            result = subprocess.run(['sh', str(script)], capture_output=True, text=True)
            print(case, 'stdout=', repr(result.stdout), 'stderr=', repr(result.stderr), 'exit=', result.returncode)
            enabled = case == 'enabled'
            self.assertEqual(result.returncode == 0, case in ('enabled', 'no-flag', 'rescue', 'off'))
            self.assertEqual((t/'vendor/etc/vintf/manifest/sq2-usb-audio.xml').exists(), enabled)
            self.assertEqual((t/'fstab').read_bytes(), files['storage.fstab'] if enabled else pristine)
            if enabled:
                args = (t/'module-args').read_text().strip()
                self.assertEqual(args, 'snd_usb_audio index='+','.join(['-2']*32))
                self.assertEqual((t/'vendor/etc/audio_policy_configuration.xml').read_bytes(), policy)

    def test_stage_cases(self):
        for case in ('enabled', 'no-flag', 'rescue', 'off', 'guardN', 'no-overlay',
                     'tamper', 'unknown-policy', 'module-fail'):
            with self.subTest(case=case):
                self.exercise(case)

    def test_discovery_is_usb_only_and_portable(self):
        rows = [l.split() for l in (R/'configs/sq2-usb-storage.fstab').read_text().splitlines()
                if l.strip() and not l.startswith('#')]
        baseline = [l.split() for l in (R/'probe/test2/fstab.sq2-native').read_text().splitlines()
                    if l.strip() and not l.startswith('#')]
        self.assertEqual(rows[:2], baseline)
        self.assertEqual(len(rows), 5)
        for row in rows[2:]:
            self.assertEqual(row[1:4], ['auto', 'auto', 'defaults'])
            self.assertRegex(row[4], r'^voldmanaged=usb[0-2]:auto$')
            for disk in ('/devices/virtual/block/loop0', '/devices/platform/soc@0/1c18000.pci/nvme0n1'):
                self.assertFalse(fnmatch.fnmatch(disk, row[0]))

    def test_audio_pair(self):
        policy = ET.parse(R/'configs/sq2-usb-audio-policy.xml')
        self.assertEqual(sum(e.get('href') == 'usb_audio_policy_configuration.xml' for e in policy.iter()), 1)
        manifest = ET.parse(R/'configs/sq2-usb-audio-manifest.xml')
        self.assertEqual(manifest.findtext('.//name'), 'android.hardware.audio.core')
        self.assertEqual(manifest.findtext('.//version'), '4')
        self.assertEqual(manifest.findtext('.//fqname'), 'IModule/usb')

    def test_packer_rejects_unknown_inputs(self):
        spec = importlib.util.spec_from_file_location('usbpack', R/'scripts/pack-sq2-usb-functions.py')
        m = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(m)
        with self.assertRaisesRegex(ValueError, 'verified SQ2'):
            m.build(b'unknown', b'unknown')


if __name__ == '__main__':
    unittest.main()
