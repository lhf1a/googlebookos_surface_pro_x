"""Metadata/tail preservation and fail-closed input tests; no private data."""
from pathlib import Path
import gzip
import importlib.util
import tempfile
import unittest

R = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('touch_pack', R/'scripts/pack-touch-runtime.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class TouchPackage(unittest.TestCase):
    def test_exact_scope_and_rejection(self):
        c = m.module('pack-sq2-vulkan')
        def archive(entries):
            return gzip.compress(b''.join(c.entry(n, b, mode) for n, b, mode in entries)+c.entry('TRAILER!!!', b'', 0), mtime=0)
        original = archive([('bin/iptsd', b'old', 0o100755), ('unrelated', b'keep', 0o100600)])
        identity = archive([('etc/shadow', b'fixture', 0o100600), ('bin/dropbear', b'fixture', 0o100755)])
        binary = b'\x7fELF\x02\x01' + b'\0'*12 + (183).to_bytes(2, 'little') + b'fixture'
        with tempfile.TemporaryDirectory() as td:
            d = Path(td);base=d/'parent.gz';daemon=d/'iptsd';out=d/'new.gz'
            base.write_bytes(original+identity);daemon.write_bytes(binary)
            report=m.build(base,daemon,out,m.sha(base.read_bytes()),m.sha(binary))
            self.assertEqual(report['changed'],['bin/iptsd'])
            self.assertTrue(out.read_bytes().startswith(original))
            self.assertTrue(out.read_bytes().endswith(identity))
            self.assertEqual(c.entries(gzip.decompress(out.read_bytes()))['unrelated'],(0o100600,b'keep'))
            with self.assertRaisesRegex(ValueError,'Fresh'):m.build(base,daemon,out,m.sha(base.read_bytes()),m.sha(binary))
            with self.assertRaisesRegex(ValueError,'SHA256'):m.build(base,daemon,d/'bad.gz','0'*64,m.sha(binary))
            with self.assertRaisesRegex(ValueError,'already'):m.build(out,daemon,d/'same.gz',m.sha(out.read_bytes()),m.sha(binary))
            base.write_bytes(original)
            with self.assertRaisesRegex(ValueError,'identity'):m.build(base,daemon,d/'anon.gz',m.sha(original),m.sha(binary))
            with self.assertRaisesRegex(ValueError,'Truncated'):m.final_member_offset(original[:-4])


if __name__ == '__main__':unittest.main()
