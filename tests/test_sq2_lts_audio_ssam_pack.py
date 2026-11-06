import gzip
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('pack', ROOT/'scripts/pack-sq2-lts-audio-ssam.py')
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)

def archive(files):
    return gzip.compress(b''.join(p.cpio.entry(n, v, 0o100600 if n == 'etc/shadow' else 0o100755)
                                 for n, v in files.items()) + p.cpio.entry('TRAILER!!!', b'', 0), mtime=0)

class CredentialTail(unittest.TestCase):
    files = {'bin/dropbear': b'fixture-binary', 'bin/start-ssh': b'fixture-script',
             'etc/shadow': b'fixture-not-a-real-credential'}

    def test_merged_base_does_not_revert_policy(self):
        base = archive(self.files | {'etc/android-native-test-policy': b'old'})
        policy = archive({'etc/android-native-test-policy': b'new'})
        tail = p.credential_tail(base+policy)
        result = p.cpio.entries(gzip.decompress(base+policy+tail))
        self.assertEqual(result['etc/android-native-test-policy'][1], b'new')
        self.assertEqual(set(p.cpio.entries(gzip.decompress(tail))), set(self.files))

    def test_separate_addon_preserved_exactly(self):
        addon = archive(self.files)
        self.assertEqual(p.credential_tail(archive({'init': b'init'})+addon), addon)

    def test_effective_credentials_and_permissions(self):
        data = archive(self.files)+archive({'etc/shadow': b'new-fixture'})
        files = p.cpio.entries(gzip.decompress(p.credential_tail(data)))
        self.assertEqual(files['etc/shadow'], (0o100600, b'new-fixture'))

    def test_no_credential_addon_for_public_input(self):
        self.assertEqual(p.credential_tail(archive({'init': b'init'})), b'')

    def test_incomplete_private_payload_rejected(self):
        with self.assertRaises(AssertionError):
            p.credential_tail(archive({'etc/shadow': b'fixture'}))
