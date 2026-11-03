"""Execute the real seed helper in an isolated path-rewritten filesystem fixture."""
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT/'probe/test2/lte-fsg-seed'


class Seed(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='sq2-lte-seed-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.fsg = bytes(range(256))*8192
        self.zero = bytes(2097152)
        for name, data in {
            'proc/cmdline': b'sq2.el2=1 nohibernate\n',
            'proc/device-tree/model': b'Microsoft Surface Pro X (SQ2)\0',
            'proc/mounts': b'tmpfs /run tmpfs rw 0 0\n',
            'sys/module/suspend/parameters/screen_only': b'Y\n',
            'sys/block/nvme0n1/device/serial': b' TESTSERIAL123 \n',
            'media/esp/SQ2-SSD-ID.txt': b'SQ2-INTERNAL-512-20261027\n',
            'etc/sq2-lte/unit-serial': b'TESTSERIAL123\n',
            'etc/sq2-lte/modem_fsg.bin': self.fsg,
            'etc/sq2-lte/FSG.sha256': (hashlib.sha256(self.fsg).hexdigest()+'  modem_fsg.bin\n').encode(),
            'run/rmtfs/modem_fsg': self.zero,
            'run/rmtfs/modem_fs1': self.zero,
            'run/rmtfs/modem_fs2': self.zero,
            'media/esp/EFI/SQ2LTE/fsg-seed.ARM': b'explicit fixture',
        }.items():
            p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        mock=self.root/'mock';mock.mkdir()
        (mock/'pidof').write_text('#!/bin/sh\n[ "$1" = rmtfs ] || exit 2\n[ -e "'+str(self.root/'ACTIVE')+'" ] && { echo 123; exit 0; }\nexit 1\n')
        (mock/'sync').write_text('#!/bin/sh\nexit 0\n')
        for p in mock.iterdir():p.chmod(0o755)
        text=SOURCE.read_text().replace('export PATH=/bin:/sbin','export PATH='+str(mock)+':/usr/bin:/bin')
        for path in ['/media/esp','/etc/sq2-lte','/run/rmtfs','/proc/cmdline','/proc/device-tree/model',
                     '/proc/mounts','/sys/module/suspend/parameters/screen_only','/sys/block/nvme0n1/device/serial']:
            text=text.replace(path,str(self.root/path.lstrip('/')))
        self.runner=self.root/'seed.sh';self.runner.write_text(text)

    def run_seed(self):
        return subprocess.run(['sh',str(self.runner)],capture_output=True,text=True,timeout=10)

    def unchanged(self):
        for name in ['modem_fsg','modem_fs1','modem_fs2']:
            self.assertEqual((self.root/'run/rmtfs'/name).read_bytes(),self.zero)

    def test_default_no_marker_silent(self):
        (self.root/'media/esp/EFI/SQ2LTE/fsg-seed.ARM').unlink()
        r=self.run_seed();self.assertEqual(r.returncode,0);self.assertEqual(r.stdout,'');self.unchanged()

    def test_success_only_fsg_and_consumed(self):
        r=self.run_seed();self.assertEqual(r.returncode,0,r.stderr);self.assertIn('LTE_FSG_SEEDED',r.stdout)
        self.assertEqual((self.root/'run/rmtfs/modem_fsg').read_bytes(),self.fsg)
        for name in ['modem_fs1','modem_fs2']:self.assertEqual((self.root/'run/rmtfs'/name).read_bytes(),self.zero)
        self.assertTrue((self.root/'media/esp/EFI/SQ2LTE/fsg-seed.ARM.used').exists())
        r=self.run_seed();self.assertEqual(r.returncode,0);self.assertEqual(r.stdout,'')

    def test_rescue_noop(self):
        (self.root/'proc/cmdline').write_text('sq2.el2=1 sq2.rescue=1 nohibernate')
        self.assertEqual(self.run_seed().returncode,0);self.unchanged()

    def test_guard_unit_ram_or_active_refused(self):
        for name, data in [('sys/module/suspend/parameters/screen_only',b'N\n'),
                           ('etc/sq2-lte/unit-serial',b'OTHER_UNIT\n'),
                           ('proc/mounts',b'/dev/invalid /run ext4 rw 0 0\n'),
                           ('ACTIVE',b'1')]:
            p=self.root/name;original=p.read_bytes() if p.exists() else None;p.write_bytes(data)
            self.assertNotEqual(self.run_seed().returncode,0);self.unchanged()
            if original is None:p.unlink()
            else:p.write_bytes(original)

    def test_corrupt_input_consumed_but_dummy_unchanged(self):
        (self.root/'etc/sq2-lte/modem_fsg.bin').write_bytes(b'X'*2097152)
        self.assertNotEqual(self.run_seed().returncode,0);self.unchanged()
        self.assertTrue((self.root/'media/esp/EFI/SQ2LTE/fsg-seed.ARM.used').exists())


if __name__ == '__main__':
    unittest.main()
