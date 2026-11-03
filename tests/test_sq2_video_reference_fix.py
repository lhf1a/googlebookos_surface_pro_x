from pathlib import Path
import gzip
import hashlib
import importlib.util
import unittest

ROOT=Path(__file__).resolve().parents[1]
def load(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'scripts'/name)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
m=load('pack-sq2-video-reference-fix.py');c=load('pack-sq2-vulkan.py')


class ReferenceFix(unittest.TestCase):
    def archive(self,e):
        return gzip.compress(b''.join(c.entry(n,b,mode) for n,(mode,b) in e.items())+c.entry('TRAILER!!!',b'',0),mtime=0)

    def fixture(self):
        helper=b'#!/bin/sh\n# RESIDENT_REFUSED=\ntest ! -e /tmp/sq2-native-pas-attempted\n'+m.OLD_GUARD+m.CORE+b'insmod venus-dec.ko\n'+m.PAS_MARK
        e={'bin/video-decoder-lab-start':(0o100755,helper),
           'etc/sq2-video-native/sq2_hfi_overlay.ko':(0o100644,b'old'),
           'etc/sq2-video-native/SHA256SUMS':(0o100644,(hashlib.sha256(b'old').hexdigest()+'  sq2_hfi_overlay.ko\n').encode()),
           'Image':(0o100644,b'kernel'), 'audio-hal':(0o100644,b'keep-audio')}
        identity=self.archive({'etc/shadow':(0o100600,b'fixture'),'bin/dropbear':(0o100755,b'fixture')})
        return self.archive(e)+identity,identity

    def test_changed_set_identity_and_marker_before_firmware(self):
        raw,identity=self.fixture();packed,audit=m.integrate(raw,b'new',c);e=c.entries(gzip.decompress(packed))
        self.assertTrue(packed.endswith(identity));self.assertEqual(e['Image'][1],b'kernel');self.assertEqual(e['audio-hal'][1],b'keep-audio')
        self.assertEqual(len(audit['changed']),3)
        helper=e['bin/video-decoder-lab-start'][1]
        self.assertIn(m.NEW_GUARD,helper);self.assertLess(helper.index(m.PAS_MARK),helper.index(m.CORE))
        again,_=m.integrate(packed,b'new',c);self.assertEqual(c.entries(gzip.decompress(again)),e)

    def test_unknown_helper_and_manifest_tamper_rejected(self):
        with self.assertRaises(ValueError):m.patch_helper(b'#!/bin/sh\nexit 0\n')
        raw,_=self.fixture();e=c.entries(gzip.decompress(raw));e['etc/sq2-video-native/sq2_hfi_overlay.ko']=(0o100644,b'tampered')
        with self.assertRaisesRegex(ValueError,'manifest mismatch'):m.integrate(self.archive(e),b'new',c)


if __name__=='__main__':unittest.main()
