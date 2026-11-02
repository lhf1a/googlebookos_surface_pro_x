"""Run the actual watcher in an isolated Linux sysfs/dev fixture (root needed)."""
import os, pathlib, subprocess, tempfile, unittest

R = pathlib.Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == 'posix' and os.geteuid() == 0, 'Linux root/mknod fixture')
class USBInputWatch(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.r = pathlib.Path(self.temp.name)
        for d in ('dev/input', 'sys/class/input', 'run', 'bin', 'proc'):
            (self.r/d).mkdir(parents=True)
        (self.r/'proc/cmdline').write_text('suspend.screen_only=1')
        (self.r/'ready').write_text('1')
        (self.r/'label_ok').write_text('0')
        fake = self.r/'bin/nsenter'
        fake.write_text('#!/bin/sh\ncase "$*" in *getprop*) cat "'+str(self.r/'ready')+'";; *chcon*) echo LABEL >> "'+str(self.r/'calls')+'"; exit "$(cat "'+str(self.r/'label_ok')+'")";; *) exit 1;; esac\n')
        fake.chmod(0o755)
        source = (R/'probe/test2/usb-input-watch').read_text()
        for p in ('/dev/', '/sys/', '/run/', '/proc/'):
            source = source.replace(p, str(self.r)+p)
        source = source.replace('export PATH=/bin:/sbin', 'export PATH='+str(self.r/'bin')+':/usr/bin:/bin')
        self.script = self.r/'watch.sh'
        self.script.write_text(source)

    def device(self, event, bus='0003', generation='input32'):
        path = self.r/f'sys/devices/platform/usb1/1-1/0003:046D:C539.0010/input/{generation}'
        path.mkdir(parents=True)
        (path/'id').mkdir()
        (path/'id/bustype').write_text(bus+'\n')
        (path/'name').write_text('USB fixture mouse\n')
        d = self.r/f'sys/class/input/{event}'
        d.mkdir(exist_ok=True)
        (d/'dev').write_text('13:87\n')
        if (d/'device').is_symlink(): (d/'device').unlink()
        (d/'device').symlink_to(path)

    def run_watch(self):
        return subprocess.run(['sh', str(self.script), '--once'], capture_output=True, text=True)

    def test_usb_permissioned_publish_idempotent_replug(self):
        self.device('event23')
        p = self.run_watch()
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn('USB_INPUT_REPUBLISHED=event23', p.stdout)
        node = self.r/'dev/input/event23'
        self.assertEqual(node.stat().st_mode & 0o777, 0o660)
        self.assertEqual(node.stat().st_gid, 1004)
        self.assertEqual((self.r/'calls').read_text(), 'LABEL\n')
        inode = node.stat().st_ino
        self.assertEqual(self.run_watch().stdout, '')
        self.assertEqual(node.stat().st_ino, inode)
        self.device('event23', generation='input99')
        self.assertIn('USB_INPUT_REPUBLISHED', self.run_watch().stdout)

    def test_non_usb_excluded(self):
        self.device('event23', bus='0005')
        self.assertEqual(self.run_watch().stdout, '')
        self.assertFalse((self.r/'dev/input/event23').exists())

    def test_android_not_ready(self):
        self.device('event23')
        (self.r/'ready').write_text('0')
        self.assertEqual(self.run_watch().returncode, 1)
        self.assertFalse((self.r/'dev/input/event23').exists())

    def test_label_failure_never_publishes(self):
        self.device('event23')
        (self.r/'label_ok').write_text('1')
        self.assertEqual(self.run_watch().stdout, '')
        self.assertFalse((self.r/'dev/input/event23').exists())

    def test_rescue_optout(self):
        self.device('event23')
        for option in ('sq2.rescue=1', 'sq2.usb=0'):
            (self.r/'proc/cmdline').write_text(option)
            self.assertEqual(self.run_watch().returncode, 0)
            self.assertFalse((self.r/'dev/input/event23').exists())


if __name__ == '__main__':
    unittest.main()
