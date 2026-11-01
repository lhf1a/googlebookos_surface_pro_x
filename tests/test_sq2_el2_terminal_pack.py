"""Verify the narrow lab delta without real firmware or credentials."""
import gzip
import hashlib
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'pack', Path(__file__).resolve().parents[1] / 'scripts/pack-sq2-el2-terminal-autostart.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)
sha = lambda data: hashlib.sha256(data).hexdigest()


def archive(files):
    raw = b''.join(pack.cpio.entry(n, body, mode) for n, (mode, body) in files.items())
    return gzip.compress(raw + pack.cpio.entry('TRAILER!!!', b'', 0), mtime=0)


class TerminalPackTest(unittest.TestCase):
    def setUp(self):
        self.payload = archive({
            'init': (0o100755, b'#!/bin/sh\n el2-kmsg-collect /logs/kernel-live.txt &\n'),
            'bin': (0o40755, b''), 'preserved': (0o100644, b'keep')})
        self.credentials = archive({'etc/shadow': (0o100600, b'fixture'),
                                    'bin/dropbear': (0o100755, b'fixture')})
        self.base = self.payload + self.credentials

    def test_only_requested_delta_with_original_credentials_last(self):
        for user in (None, '10'):
            result, report = pack.build(self.base, sha(self.base), user)
            self.assertTrue(result.startswith(self.base))
            self.assertTrue(result.endswith(self.credentials))
            self.assertEqual(report['changed'], ['bin/el2-terminal-autostart', 'init'])
            final = pack.cpio.entries(gzip.decompress(result))
            self.assertEqual(final['preserved'], (0o100644, b'keep'))
            argument = '' if user is None else ' 10'
            self.assertIn((' el2-terminal-autostart' + argument + ' >').encode(), final['init'][1])
            with self.assertRaises(ValueError):
                pack.build(result, sha(result), user)

    def test_refuse_bad_inputs(self):
        for base, digest, user in [(self.base, '0' * 64, '10'),
                                   (self.payload, sha(self.payload), '10'),
                                   (self.base, sha(self.base), '10;bad')]:
            with self.subTest(user=user, digest=digest), self.assertRaises(ValueError):
                pack.build(base, digest, user)


if __name__ == '__main__':
    unittest.main()
