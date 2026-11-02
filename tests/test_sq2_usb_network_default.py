"""Default-build integration uses small synthetic inputs, never device access."""
import gzip, hashlib, importlib.util, json, pathlib, shutil, tempfile, unittest
from unittest.mock import patch

R = pathlib.Path(__file__).resolve().parents[1]
s = importlib.util.spec_from_file_location('usb_default', R/'scripts/sq2-usb-network-boot-payload.py')
n = importlib.util.module_from_spec(s)
s.loader.exec_module(n)
s = importlib.util.spec_from_file_location('archive', R/'scripts/pack-sq2-vulkan.py')
c = importlib.util.module_from_spec(s)
s.loader.exec_module(c)
sha = lambda b: hashlib.sha256(b).hexdigest()


class DefaultNetwork(unittest.TestCase):
    def archive(self, entries):
        b = b''.join(c.entry(k,v,m) for k,(m,v) in entries.items())+c.entry('TRAILER!!!',b'',0)
        return gzip.compress(b+b'\0'*(-len(b)%512),mtime=0)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        for d in ('configs','scripts','probe/test2','bundle'):
            (self.root/d).mkdir(parents=True)
        for f in ('pack-sq2-usb-network.py','pack-sq2-vulkan.py'):
            shutil.copyfile(R/'scripts'/f,self.root/'scripts'/f)
        for f in n.HELPERS:
            shutil.copyfile(R/'probe/test2'/f,self.root/'probe/test2'/f)
        self.bundle = self.root/'bundle'
        self.entries = {'init':(0o100755,b'#!/bin/sh\nmodprobe af_key\n el2-terminal-autostart 10 > /logs/el2-terminal-autostart.txt 2>&1 &\n    lab-input-posture > /logs/lab-input-posture.txt 2>&1 &\n'),
                        'etc/untouched':(0o100644,b'PRESERVE'),
                        'etc/shadow':(0o100600,b'PRIVATE_FIXTURE'),
                        'bin/dropbear':(0o100755,b'SSH_FIXTURE')}
        self.cred = self.archive({k:self.entries[k] for k in ('etc/shadow','bin/dropbear')})
        self.raw = self.archive(self.entries)+self.cred
        self.module = 'kernel/drivers/net/usb/usbnet.ko'
        self.body = b'SYNTHETIC_MODULE'
        stage = self.bundle/'module-stage'/n.PREFIX
        (stage/self.module).parent.mkdir(parents=True)
        (stage/self.module).write_bytes(self.body)
        (stage/'modules.dep').write_text('kernel/drivers/net/usb/usbnet.ko:\n')
        self.policy = dict(schema=1,kernel_image_sha256='IMAGE_FIXTURE',baseline_payload_sha256=sha(self.raw),
                           baseline_semantic_sha256=n.semantic_digest(self.entries),reference_vermagic='FIXTURE',
                           modules={self.module:sha(self.body)},metadata={'modules.dep':sha((stage/'modules.dep').read_bytes())},
                           aliases_sha256=sha(b'usb:v0B95* usbnet\n'))
        (self.root/'configs/sq2-usb-network.json').write_text(json.dumps(self.policy))
        audit=dict(base_sha256=sha(self.raw),reference_vermagic='FIXTURE',missing={},
                   new_modules={self.module:{'sha256':sha(self.body)}})
        (self.bundle/'module-audit.json').write_text(json.dumps(audit))

    def integrate(self, raw=None):
        with patch('subprocess.check_output',return_value='usb:v0B95*\n'):
            return n.integrate(self.root,raw or self.raw,self.bundle,'IMAGE_FIXTURE')

    def test_actual_path_adds_modules_helpers_and_preserves_parent(self):
        raw, report = self.integrate()
        e = c.entries(gzip.decompress(raw))
        self.assertTrue(report['enabled'])
        self.assertTrue(raw.endswith(self.cred))
        self.assertEqual(e['etc/untouched'],self.entries['etc/untouched'])
        self.assertEqual(e['etc/shadow'],self.entries['etc/shadow'])
        self.assertEqual(e[n.PREFIX+self.module][1],self.body)
        n.verify(e,self.root)

    def test_explicit_or_no_passwordless_user_does_not_disable_network(self):
        for arg in (b'',b' 0',b' 10',b' 11'):
            e = dict(self.entries)
            e['init'] = (0o100755,e['init'][1].replace(b'autostart 10 ',b'autostart'+arg+b' '))
            self.assertTrue(self.integrate(self.archive(e)+self.cred)[1]['enabled'])

    def test_unrelated_runtime_change_rejected(self):
        e = dict(self.entries)
        e['etc/untouched'] = (0o100644,b'TAMPERED')
        with self.assertRaisesRegex(ValueError,'runtime changed'):
            self.integrate(self.archive(e)+self.cred)

    def test_image_module_and_index_tamper_rejected(self):
        with self.assertRaisesRegex(ValueError,'kernel Image'):
            n.validate_bundle(self.root,self.bundle,'OTHER_IMAGE')
        stage = self.bundle/'module-stage'/n.PREFIX
        (stage/'modules.dep').write_text('TAMPER')
        with self.assertRaisesRegex(ValueError,'index differs'):
            self.integrate()

    def test_default_builder_wires_shared_payload_before_both_entries(self):
        s = (R/'scripts/build-sq2-default.py').read_text()
        self.assertIn("'--usb-network', choices=('on', 'off'), default='on'",s)
        self.assertLess(s.index('normal_payload(raw,'),s.index('packed, report[\'usb_network\'] = usb_network.integrate('))
        self.assertLess(s.index('usb_network.integrate('),s.index("for directory, graphics in (('SQ2Default'"))


if __name__ == '__main__':
    unittest.main()
