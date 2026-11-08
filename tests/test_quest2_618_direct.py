"""Synthetic ELF boundary/transaction checks; no proprietary bytes or device access."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('direct', Path(__file__).resolve().parents[1]/'scripts/prepare-quest2-618-direct.py')
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)

class DirectRuntime(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root/'parent'
        self.source.mkdir()
        self.output = self.root/'candidate'
        data = bytearray(512)
        data[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<H', data, 18, 183)
        struct.pack_into('<Q', data, 32, 64)
        struct.pack_into('<HH', data, 54, 56, 1)
        struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 256, M.VA, 0, 256, 256, 4096)
        data[256:264] = M.BEFORE
        self.original = bytes(data)
        self.candidate = self.original[:256]+M.AFTER+self.original[264:]
        for key, value in [('PARENT_SHA', M.sha(self.original)), ('OUTPUT_SHA', M.sha(self.candidate))]:
            mock = patch.object(M, key, value)
            mock.start()
            self.addCleanup(mock.stop)
        (self.source/M.LIB).write_bytes(self.original)
        for i in range(14):
            (self.source/('fixture%d.so' % i)).write_bytes(b'synthetic fixture')
        (self.source/'qgl_config.txt').write_bytes(b'config')
        files = {p.name: {'sha256': M.sha(p.read_bytes())} for p in self.source.iterdir()}
        sums = ''.join(v['sha256']+'  '+n+'\n' for n,v in files.items()).encode()
        (self.source/'SHA256SUMS').write_bytes(sums)
        (self.source/'RUNTIME.json').write_text(json.dumps({'files':files}))
        mock = patch.object(M, 'PARENT_SUMS_SHA', M.sha(sums))
        mock.start()
        self.addCleanup(mock.stop)

    def test_exact_patch_and_parent_preserved(self):
        before = {p.name:p.read_bytes() for p in self.source.iterdir()}
        M.prepare(self.source, self.output)
        self.assertEqual((self.output/M.LIB).read_bytes(), self.candidate)
        self.assertEqual(before, {p.name:p.read_bytes() for p in self.source.iterdir()})
        for name, data in before.items():
            if name not in (M.LIB, 'SHA256SUMS', 'RUNTIME.json'):
                self.assertEqual((self.output/name).read_bytes(), data)
        self.assertEqual(json.loads(self.output.with_suffix('.patch.json').read_text())['offset'],256)
        self.assertFalse(json.loads((self.output/'RUNTIME.json').read_text())['direct_rendering']['gmem_root_cause_fixed'])

    def test_input_and_output_hashes(self):
        with self.assertRaisesRegex(ValueError, 'input hash'):
            M.patch_gles(self.original+b'x')
        with patch.object(M, 'OUTPUT_SHA', '0'*64):
            with self.assertRaisesRegex(ValueError, 'candidate hash'):
                M.patch_gles(self.original)

    def test_malformed_elf_and_instruction_ranges(self):
        variants = []
        for offset, value in [(18, b'\x3e\x00'), (54,b'\x01\x00'), (68,(7).to_bytes(4,'little')),
                              (96,(4).to_bytes(8,'little')), (256,b'badbytes')]:
            data = bytearray(self.original)
            data[offset:offset+len(value)] = value
            variants.append(bytes(data))
        data = bytearray(self.original)
        struct.pack_into('<H', data, 56, 2)
        data[120:176] = data[64:120]
        variants.extend([bytes(data), b'short', self.original[:300]])
        for data in variants:
            with self.subTest(length=len(data)), patch.object(M,'PARENT_SHA',M.sha(data)):
                with self.assertRaises(ValueError):
                    M.patch_gles(data)

    def test_manifest_and_file_tampering_precedes_output(self):
        for name in ['SHA256SUMS', M.LIB, 'RUNTIME.json']:
            path = self.source/name
            before = path.read_bytes()
            path.write_bytes(b'{}' if name=='RUNTIME.json' else before+b'x')
            with self.subTest(name=name), self.assertRaises((ValueError,KeyError)):
                M.prepare(self.source,self.output)
            self.assertFalse(self.output.exists())
            path.write_bytes(before)

    def test_symlink_input_rejected(self):
        path=self.source/'fixture0.so'
        path.unlink()
        path.symlink_to('fixture1.so')
        with self.assertRaisesRegex(ValueError,'regular input'):
            M.prepare(self.source,self.output)
        self.assertFalse(self.output.exists())

    def test_existing_output_or_patch_preserved(self):
        for target in [self.output,self.output.with_suffix('.patch.json')]:
            target.write_bytes(b'keep')
            with self.assertRaisesRegex(ValueError,'Fresh'):
                M.prepare(self.source,self.output)
            self.assertEqual(target.read_bytes(),b'keep')
            target.unlink()
        self.output.symlink_to('absent')
        with self.assertRaisesRegex(ValueError,'Fresh'):
            M.prepare(self.source,self.output)

if __name__ == '__main__':
    unittest.main()
