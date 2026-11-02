"""Small synthetic archives; no device access, proprietary images or disks."""
import gzip, hashlib, importlib.util, json, pathlib, tempfile, unittest
from unittest.mock import patch

R = pathlib.Path(__file__).resolve().parents[1]
s = importlib.util.spec_from_file_location('pack_usb_network', R/'scripts/pack-sq2-usb-network.py')
m = importlib.util.module_from_spec(s)
s.loader.exec_module(m)


class NetworkPack(unittest.TestCase):
    def archive(self, entries):
        data = b''.join(m.c.entry(n,b,mode) for n,(mode,b) in entries.items())
        data += m.c.entry('TRAILER!!!', b'', 0)
        return gzip.compress(data+b'\0'*(-len(data)%512), mtime=0)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.d = pathlib.Path(self.temp.name)
        self.prefix = 'lib/modules/6.3.8-sq2-android1/'
        self.module = 'kernel/drivers/net/usb/usbnet.ko'
        self.init = b'#!/bin/sh\nmodprobe af_key\n    lab-input-posture > /logs/lab-input-posture.txt 2>&1 &\n'
        self.creds = self.archive({'etc/shadow':(0o100600,b'PRIVATE_FIXTURE'), 'bin/dropbear':(0o100755,b'SSH_FIXTURE')})
        self.base = self.archive({'init':(0o100755,self.init), 'etc/untouched':(0o100644,b'preserve')})+self.creds
        self.expected = m.sha(self.base)
        self.body = b'SYNTHETIC_MODULE_NOT_EXECUTED'
        p = self.d/'module-stage'/self.prefix/self.module
        p.parent.mkdir(parents=True)
        p.write_bytes(self.body)
        self.audit = dict(base_sha256=self.expected, missing={}, reference_vermagic='FIXTURE',
                          new_modules={self.module:dict(sha256=m.sha(self.body))})
        self.save_audit()

    def save_audit(self):
        (self.d/'module-audit.json').write_text(json.dumps(self.audit))

    def test_positive_preserves_effective_bytes_and_credentials_last(self):
        with patch.object(m.subprocess, 'check_output', return_value='usb:v0B95*\n'):
            result, audit = m.build(self.base,self.d,self.expected)
        after = m.c.entries(gzip.decompress(result))
        self.assertTrue(result.endswith(self.creds))
        self.assertEqual(after['etc/untouched'][1],b'preserve')
        self.assertEqual(after[self.prefix+self.module][1],self.body)
        self.assertIn(b'usb-input-watch >',after['init'][1])
        self.assertIn(b'usb-network-watch >',after['init'][1])
        self.assertIn(b'modules.dep.bb',after['init'][1])
        self.assertTrue(audit['password_member_last'])

    def test_wrong_base_and_wrong_audit(self):
        with self.assertRaisesRegex(ValueError,'Base hash'):
            m.build(self.base,self.d,'0'*64)
        self.audit['base_sha256']='0'*64
        self.save_audit()
        with self.assertRaisesRegex(ValueError,'different baseline'):
            m.build(self.base,self.d,self.expected)

    def test_unresolved_export_rejected(self):
        self.audit['missing']={'usbnet':['MISSING_SYMBOL']}
        self.save_audit()
        with self.assertRaisesRegex(ValueError,'Missing runtime'):
            m.build(self.base,self.d,self.expected)

    def test_tampered_module_rejected(self):
        (self.d/'module-stage'/self.prefix/self.module).write_bytes(b'TAMPERED')
        with self.assertRaisesRegex(ValueError,'Module hash'):
            m.build(self.base,self.d,self.expected)


if __name__ == '__main__':
    unittest.main()
