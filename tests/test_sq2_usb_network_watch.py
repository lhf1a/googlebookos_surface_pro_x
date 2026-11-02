"""Exercise alias-only USB loading without real modules or network changes."""
import os, pathlib, subprocess, tempfile, unittest

R = pathlib.Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == 'posix', 'POSIX shell fixture')
class USBNetworkWatch(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.r = pathlib.Path(self.temp.name)
        release = os.uname().release
        for d in ('sys/bus/usb/devices/1-1:2.0', 'sys/bus/usb/devices/1-1',
                  'run', 'proc', 'etc', 'bin', 'lib/modules/'+release):
            (self.r/d).mkdir(parents=True)
        (self.r/'proc/cmdline').write_text('suspend.screen_only=1')
        (self.r/'sys/bus/usb/devices/1-1:2.0/modalias').write_text('usb:v0B95p1790ic02isc0D\n')
        (self.r/'sys/bus/usb/devices/1-1/devnum').write_text('4\n')
        (self.r/'etc/sq2-usb-network.aliases').write_text('usb:v0B95p1790* cdc_ncm\n')
        (self.r/('lib/modules/'+release+'/modules.dep')).write_text('kernel/cdc_ncm.ko: kernel/cdc_ether.ko kernel/usbnet.ko\n')
        for name, body in [('nsenter', 'echo 1'), ('modprobe', 'echo "$1" >> "'+str(self.r/'calls')+'"')]:
            p = self.r/'bin'/name
            p.write_text('#!/bin/sh\n'+body+'\n')
            p.chmod(0o755)
        source = (R/'probe/test2/usb-network-watch').read_text()
        for p in ('/sys/', '/run/', '/proc/', '/etc/', '/lib/'):
            source = source.replace(p, str(self.r)+p)
        source = source.replace('export PATH=/bin:/sbin', 'export PATH='+str(self.r/'bin')+':/usr/bin:/bin')
        self.script = self.r/'watch.sh'
        self.script.write_text(source)

    def run_watch(self):
        return subprocess.run(['sh', str(self.script), '--once'], capture_output=True, text=True)

    def test_matching_dependency_order_idempotence_and_replug(self):
        p = self.run_watch()
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual((self.r/'calls').read_text().splitlines(), ['usbnet', 'cdc_ether', 'cdc_ncm'])
        self.assertIn('USB_NETWORK_MODULE=cdc_ncm', p.stdout)
        self.assertEqual(self.run_watch().stdout, '')
        (self.r/'sys/bus/usb/devices/1-1/devnum').write_text('9\n')
        self.assertIn('USB_NETWORK_MODULE=cdc_ncm', self.run_watch().stdout)

    def test_unknown_alias_does_not_load(self):
        (self.r/'sys/bus/usb/devices/1-1:2.0/modalias').write_text('usb:vDEADpBEEF\n')
        self.assertEqual(self.run_watch().returncode, 0)
        self.assertFalse((self.r/'calls').exists())

    def test_already_bound_is_untouched(self):
        (self.r/'sys/bus/usb/devices/1-1:2.0/driver').mkdir()
        self.assertEqual(self.run_watch().returncode, 0)
        self.assertFalse((self.r/'calls').exists())

    def test_optout_and_missing_bundle(self):
        for flag in ('sq2.rescue=1', 'sq2.usb=0'):
            (self.r/'proc/cmdline').write_text(flag)
            self.assertEqual(self.run_watch().returncode, 0)
            self.assertFalse((self.r/'calls').exists())
        (self.r/'proc/cmdline').write_text('')
        (self.r/'etc/sq2-usb-network.aliases').unlink()
        self.assertEqual(self.run_watch().returncode, 0)


if __name__ == '__main__':
    unittest.main()
