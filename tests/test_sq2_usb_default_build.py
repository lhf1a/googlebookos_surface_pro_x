"""Exercise default boot integration, display menus and packed deployment checks."""
import copy
import gzip
import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest import mock

R = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), R/'scripts'/(name+'.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


u = load('sq2-usb-boot-payload')
c = load('pack-sq2-vulkan')
d = load('sq2-display-boot-payload')
v = load('sq2-video-boot-payload')
deploy = load('deploy-experimental-usb')
INIT = (b'resource-monitor-start > /logs/resource-monitor.txt\n'
        b'cp /etc/fstab.sq2-native /newroot/debug_ramdisk/fstab.sq2-native\n'
        b'bluetooth-framework-boot > /logs/bluetooth-framework-boot.txt &\n')


class DefaultUsbBluetooth(unittest.TestCase):
    def fixture(self):
        entries = {}
        def put(n, b, m):
            entries[n] = (m, b)
        init = u.integrate(R, put, INIT)
        entries['init'] = (0o100755, init)
        return entries

    def test_actual_integration_payload_and_tamper_rejection(self):
        entries = self.fixture()
        self.assertEqual(u.verify(entries, R), {n: u.sha(b) for n, (_, b) in u.payload(R).items()})
        self.assertLess(entries['init'][1].index(b'usb-functions-stage >'),
                        entries['init'][1].index(b'cp /etc/fstab'))
        for name in ('bin/bluetooth-input-watch', 'bin/bluetooth-framework-boot',
                     'bin/usb-functions-stage', 'etc/sq2-usb/storage.fstab'):
            bad = dict(entries)
            del bad[name]
            with self.assertRaisesRegex(ValueError, 'payload'):
                u.verify(bad, R)
        for init in (INIT, entries['init'][1]):
            with self.assertRaisesRegex(ValueError, 'hook'):
                u.integrate(R, lambda *args: None, init.replace(b'resource-monitor-start >', b'changed >'))

    def test_display_profiles_normal_rescue_and_optout(self):
        source = (R/'configs/grub-source.cfg').read_text()
        for profile in d.PROFILES:
            menu = d.default_menu(u.default_menu(v.default_menu(source)), profile).encode()
            d.validate_menu(menu, profile)
            u.validate_menu(menu)
            deploy.validate_video_menu(menu, dict(schema=1, enabled=True, menu_sha256=u.sha(menu)))
            self.assertIn(b'sq2.usb=experimental', menu)
            for line in menu.decode().splitlines():
                if 'sq2.rescue=1' in line:
                    self.assertIn('sq2.usb=0', line)
                    self.assertNotIn('sq2.usb=experimental', line)
        text = 'linux /Image\nlinux /Image sq2.usb=0\nlinux /Image sq2.rescue=1\n'
        menu = u.default_menu(text)
        self.assertEqual(u.default_menu(menu), menu)
        self.assertIn('sq2.usb=0', menu.splitlines()[1])
        for bad in ('linux /Image\n', 'linux /Image\nlinux /Image sq2.rescue=1 sq2.usb=experimental\n',
                    'linux /Image sq2.usb=0 sq2.usb=experimental\nlinux /Image sq2.rescue=1\n'):
            with self.assertRaises(ValueError):
                u.default_menu(bad)

    def test_packed_menu_payload_and_manifest_gate(self):
        entries = self.fixture()
        raw = b''.join(c.entry(n, b, m) for n, (m, b) in entries.items())+c.entry('TRAILER!!!', b'', 0)
        base = gzip.compress(raw, mtime=0)
        menu = u.default_menu('linux /Image\nlinux /Image sq2.rescue=1\n').encode()
        policy = u.policy(menu, base, entries, R)
        manifest = dict(usb_bluetooth=policy, boot_payload_sha256={u.BASE: u.sha(base)})
        layout = {'partitions': [{'name': 'SQ2PROBE', 'offset': 1048576}]}
        def packed(cmd):
            self.assertEqual(cmd[:2], ['mtype', '-i'])
            return menu if cmd[-1].endswith('grub.cfg') else base
        with mock.patch.object(deploy.subprocess, 'check_output', side_effect=packed):
            deploy.verify_usb_bluetooth(manifest, layout, Path('fixture.img'), True)
            for field in ('menu_sha256', 'initramfs_sha256', 'source_sha256'):
                bad = copy.deepcopy(manifest)
                bad['usb_bluetooth'][field] = {} if field == 'source_sha256' else '0'*64
                with self.assertRaises(ValueError):
                    deploy.verify_usb_bluetooth(bad, layout, Path('fixture.img'), True)
        with self.assertRaisesRegex(ValueError, 'no default'):
            deploy.verify_usb_bluetooth({}, layout, Path('fixture.img'), True)

    def test_builder_calls_gate_before_disk_build_and_selection(self):
        boot = (R/'scripts/build-source-boot.py').read_text()
        self.assertIn('init=usb.integrate(R,put,init)', boot)
        self.assertIn('usb_hashes=usb.verify(after,R)', boot)
        self.assertIn('usb_functions_included=True,bluetooth_input_included=True', boot)
        disk = (R/'scripts/build-source-disk.py').read_text()
        self.assertLess(disk.index('usb.verify(entries,R)'), disk.index('b.build()'))
        cli = (R/'scripts/deploy-experimental-usb.py').read_text()
        self.assertLess(cli.index('verify_usb_bluetooth(m, layout'), cli.index('disks = [d for d in inventory()'))


if __name__ == '__main__':
    unittest.main()
