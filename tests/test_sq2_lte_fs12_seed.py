"""Execute actual once-only helper, including coupled validation and repeat boot."""
import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Seed(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='sq2-fs12-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.zero = bytes(2097152)
        self.pair = {'modem_fs1': b'A'*2097152, 'modem_fs2': b'B'*2097152}
        fixtures = {
            'proc/cmdline': b'sq2.el2=1 nohibernate\n',
            'proc/device-tree/model': b'Microsoft Surface Pro X (SQ2)\0',
            'proc/mounts': b'tmpfs /run tmpfs rw 0 0\n',
            'sys/module/suspend/parameters/screen_only': b'Y\n',
            'sys/block/nvme0n1/device/serial': b'TESTSERIAL123\n',
            'media/esp/SQ2-SSD-ID.txt': b'SQ2-INTERNAL-512-20261027\n',
            'media/esp/EFI/SQ2LTE/fs12-seed.ARM': b'SQ1_FS12_RAM_ONCE_V1\n',
            'etc/sq2-lte/unit-serial': b'TESTSERIAL123\n',
            'etc/sq2-lte/FS12.sha256': ''.join(hashlib.sha256(d).hexdigest()+'  '+n+'.bin\n' for n,d in self.pair.items()).encode(),
            **{'etc/sq2-lte/'+n+'.bin': d for n,d in self.pair.items()},
            **{'run/rmtfs/'+n: self.zero for n in ['modem_fs1','modem_fs2','modem_fsg','modem_fsc']},
        }
        for n,d in fixtures.items():
            p=self.root/n; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(d)
        mock=self.root/'mock'; mock.mkdir()
        (mock/'pidof').write_text('#!/bin/sh\n[ -e "'+str(self.root/'ACTIVE')+'" ] && { echo 123; exit 0; }\nexit 1\n')
        (mock/'sync').write_text('#!/bin/sh\nexit 0\n')
        for p in mock.iterdir(): p.chmod(0o755)
        text=(ROOT/'probe/test2/lte-fs12-seed').read_text().replace('export PATH=/bin:/sbin', 'export PATH='+str(mock)+':/usr/bin:/bin')
        for p in ['/media/esp','/etc/sq2-lte','/run/rmtfs','/proc/cmdline','/proc/device-tree/model',
                  '/proc/mounts','/sys/module/suspend/parameters/screen_only','/sys/block/nvme0n1/device/serial']:
            text=text.replace(p,str(self.root/p.lstrip('/')))
        self.runner=self.root/'helper.sh'; self.runner.write_text(text)

    def run_seed(self):
        return subprocess.run(['sh',str(self.runner)],capture_output=True,text=True,timeout=10)

    def assert_zero(self):
        for n in ['modem_fs1','modem_fs2','modem_fsg','modem_fsc']:
            self.assertEqual((self.root/'run/rmtfs'/n).read_bytes(),self.zero)

    def test_absent_marker_silent(self):
        (self.root/'media/esp/EFI/SQ2LTE/fs12-seed.ARM').unlink()
        r=self.run_seed(); self.assertEqual((r.returncode,r.stdout,r.stderr),(0,'','')); self.assert_zero()

    def test_pair_once_and_other_inputs_preserved(self):
        r=self.run_seed(); self.assertEqual(r.returncode,0,r.stderr); self.assertIn('LTE_FS12_SEEDED',r.stdout)
        for n,d in self.pair.items(): self.assertEqual((self.root/'run/rmtfs'/n).read_bytes(),d)
        for n in ['modem_fsg','modem_fsc']: self.assertEqual((self.root/'run/rmtfs'/n).read_bytes(),self.zero)
        self.assertTrue((self.root/'media/esp/EFI/SQ2LTE/fs12-seed.ARM.used').exists())
        # Model a second boot: regenerate original RAM inputs, without re-arming.
        for n in ['modem_fs1','modem_fs2']: (self.root/'run/rmtfs'/n).write_bytes(self.zero)
        r=self.run_seed(); self.assertEqual((r.returncode,r.stdout),(0,'')); self.assert_zero()

    def test_either_bad_input_preserves_both_and_consumes(self):
        for n in self.pair:
            p=self.root/'etc/sq2-lte'/(n+'.bin'); old=p.read_bytes(); p.write_bytes(b'X'*2097152)
            r=self.run_seed(); self.assertNotEqual(r.returncode,0); self.assert_zero()
            used=self.root/'media/esp/EFI/SQ2LTE/fs12-seed.ARM.used'; self.assertTrue(used.exists())
            used.rename(used.with_suffix('')); p.write_bytes(old)

    def test_guards_refuse(self):
        for n,d in [('ACTIVE',b'1'),
                    ('etc/sq2-lte/unit-serial',b'WRONG'),('proc/mounts',b'/dev/foo /run ext4 rw 0 0\n')]:
            p=self.root/n; old=p.read_bytes() if p.exists() else None; p.write_bytes(d)
            self.assertNotEqual(self.run_seed().returncode,0); self.assert_zero()
            if old is None: p.unlink()
            else: p.write_bytes(old)

    def test_screen_only_N_does_not_block_valid_seed(self):
        (self.root/'sys/module/suspend/parameters/screen_only').write_text('N')
        r=self.run_seed();self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn('SEEDED',r.stdout)

    def test_rescue_noop(self):
        (self.root/'proc/cmdline').write_text('sq2.el2=1 sq2.rescue=1 nohibernate')
        self.assertEqual(self.run_seed().returncode,0); self.assert_zero()


if __name__ == '__main__':
    unittest.main()
