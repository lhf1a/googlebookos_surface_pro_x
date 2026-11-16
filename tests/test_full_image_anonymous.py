import gzip
import sys
import unittest
from pathlib import Path
from unittest.mock import patch
from contextlib import redirect_stdout, redirect_stderr
import io
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import full_image as f
import full_image_anonymous as a
import disk_image as d

class Anonymous(unittest.TestCase):
    def fixture(self):
        bodies={'init':b'#!/bin/sh\n    if [ -x /bin/lab-local-setup ]; then\n        lab-local-setup > /logs/setup 2>&1 &\n    fi\n'+d.HOOK+d.ANCHOR,
                'etc/shadow':b'root:old-password-hash:0::::::\n','root/.ssh/authorized_keys':b'old-public-key',
                'etc/dropbear/dropbear_ed25519_host_key':b'old-private-key',
                'etc/sq2-android-wifi.conf':b'old-network\nold-wifi-secret\n',
                'etc/wpa_supplicant.conf':b'network={psk="old-wifi-secret"}\n',
                'bin/lab-local-setup':b'skip','bin/skip-google-setup':b'skip',
                'etc/spx-camera/userspace/provider':b'camera',
                'etc/spx-sensors/userspace/sensors.spx.so':b'sensors',
                'bin/iptsd':b'touch','bin/user0-home-boot':b'wait-for-setup'}
        return {n:f.new_record(0o100644) for n in bodies}, bodies
    def test_clear_identity_keep_all_features_and_setup(self):
        records,bodies=self.fixture();original=dict(bodies)
        result=a.apply(records,bodies,f.new_record)
        for name in ['etc/spx-camera/userspace/provider','etc/spx-sensors/userspace/sensors.spx.so','bin/iptsd','bin/user0-home-boot']:
            self.assertEqual(bodies[name],original[name])
        raw=b''.join(bodies.values())
        for secret in [b'old-private-key',b'old-public-key',b'old-wifi-secret',b'old-password-hash',b'lab-local-setup']:
            self.assertNotIn(secret,raw)
        self.assertEqual(bodies['etc/shadow'],b'root:!:0:0:99999:7:::\n')
        self.assertTrue(result['first_setup_preserved'])
    def test_secret_copy_rejected(self):
        records,bodies=self.fixture();bodies['etc/copy']=b'old-private-key'
        with self.assertRaisesRegex(ValueError,'credential'):a.apply(records,bodies,f.new_record)
    def test_anonymous_single_member_accepts_storage_delta(self):
        c=d.cpio_module();bodies={'init':b'#!/bin/sh\nsq2-loop-slice "${ssd_part}3" "$offset" "$bytes"\n'+d.HOOK+d.ANCHOR,'bin/apply-single-user':b'user0','etc/shadow':b'root:!','bin/dropbear':b'ELF'}
        base=gzip.compress(b''.join(c.entry(n,b,0o100755) for n,b in bodies.items())+c.entry('TRAILER!!!',b'',0))
        payload,audit,_=d.storage_payload(base,[dict(name='super',index=3,bytes=512)],anonymous=True)
        result=c.entries(gzip.decompress(payload));self.assertIn(b'/dev/block/by-name/super',result['init'][1]);self.assertFalse(audit['credential_member_last'])
    def test_cli_default_and_reject_other_graphics(self):
        entry=d.module('anonymous_entry','build-lts.py')
        with patch.object(f,'build',return_value={}) as build,redirect_stdout(io.StringIO()):
            entry.main(['--output','/fixture'])
            args=build.call_args.args[0];self.assertEqual(args.graphics,'hybrid');self.assertFalse(args.without_camera);self.assertFalse(args.without_sensors)
        for value in ['mesa','baseline']:
            with redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):entry.main(['full-image','--graphics',value,'--output','/fixture'])

if __name__=='__main__':unittest.main()
