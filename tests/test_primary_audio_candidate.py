"""Regression for the opt-in donor adaptation; private ELF is never committed.

SQ2_AUDIO_DONOR points at the mic-corrected input ELF. QEMU executes the literal
patched ARM64 instructions with only the device-open leaves stubbed out.
"""
import importlib.util
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'primary_audio', ROOT/'scripts/prepare-sq2-primary-audio.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class InputTests(unittest.TestCase):
    def test_rejects_unrecognized_input(self):
        with self.assertRaises(ValueError):
            module.prepare(b'not an ELF')


@unittest.skipUnless(os.environ.get('SQ2_AUDIO_DONOR'),
                     'set SQ2_AUDIO_DONOR to the private mic-corrected donor')
class InstructionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for tool in ('aarch64-linux-gnu-as', 'aarch64-linux-gnu-objcopy',
                     'aarch64-linux-gnu-gcc', 'qemu-aarch64'):
            if not shutil.which(tool):
                raise RuntimeError('Required test tool missing: ' + tool)
        cls.original = Path(os.environ['SQ2_AUDIO_DONOR']).read_bytes()
        cls.candidate = module.prepare(cls.original)

    def execute(self, code, source):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p/'check.S').write_text('.text\n.global testfn\ntestfn:\n' + code)
            (p/'check.c').write_text(source)
            subprocess.run(['aarch64-linux-gnu-gcc', '-static', 'check.c',
                            'check.S', '-o', 'check'], cwd=p, check=True,
                           capture_output=True)
            result = subprocess.run(['qemu-aarch64', './check'], cwd=p,
                                    capture_output=True, text=True, check=True)
            self.assertEqual(result.stderr, '')
            return result.stdout

    @staticmethod
    def literal(data):
        return '.byte ' + ','.join(map(str, data)) + '\n'

    def test_only_two_executable_ranges_change(self):
        self.assertEqual(hashlib.sha256(self.candidate).hexdigest(), module.OUTPUT)
        self.assertEqual(len(self.original), len(self.candidate))
        restored = bytearray(self.candidate)
        for a, n in ((0x9bc0c, 48), (0xd6734, 72)):
            restored[a:a+n] = self.original[a:a+n]
        self.assertEqual(bytes(restored), self.original)

    def test_component_reuse_is_idempotent(self):
        self.assertEqual(module.prepare(self.candidate), self.candidate)

    def test_runtime_install_only_replaces_hal(self):
        bodies = {'etc/sq2-audio/audio-hal': self.original,
                  'etc/shadow': b'private fixture',
                  'bin/audio-framework-boot': b'unchanged boot helper'}
        before = dict(bodies)
        module.install(bodies)
        self.assertEqual(bodies['etc/sq2-audio/audio-hal'], self.candidate)
        self.assertEqual({k for k in bodies if bodies[k] != before[k]},
                         {'etc/sq2-audio/audio-hal'})
        module.install(bodies)
        self.assertEqual(bodies['etc/sq2-audio/audio-hal'], self.candidate)

    def test_latency_primary_only(self):
        source = r'''
        #include <stdio.h>
        extern int testfn(void*,void*);
        int main(void) {
            unsigned v[36]={0}; v[34]=1;
            unsigned t[][2]={{1,2},{1,4096},{1,0},{0,0},{0,16},{9,0}};
            for(int i=0;i<6;i++) {
                v[33]=t[i][0];v[32]=t[i][1];
                printf("%d\n",testfn(0,v));
            }return 0;
        }'''
        for data, primary in ((self.original, 85), (self.candidate, 10)):
            result = self.execute(self.literal(data[0x9bc00:0x9bc3c]), source)
            self.assertEqual(result, f'{primary}\n10\n85\n85\n10\n85\n')

    def test_physical_ring_capture_external_and_larger_requests(self):
        source = r'''
        #include <stdio.h>
        #include <string.h>
        extern long testfn(void*,void*);
        int main(void) {
            unsigned char d[16]={0},c[208]={0}; c[200]=1;
            long sizes[]={240,480,1920,4096,5760,8192,16384};
            for(int ext=0;ext<2;ext++)for(int in=0;in<2;in++)
            for(int j=0;j<7;j++) {
                d[12]=ext; unsigned dir=in?0x10000000:0;
                memcpy(d+8,&dir,4);memcpy(c+112,&sizes[j],8);
                printf("%ld\n",testfn(d,c));
            }
            c[200]=0;
            printf("%ld\n",testfn(d,c));
            return 0;
        }'''
        for data, modified in ((self.original, False), (self.candidate, True)):
            code = ('stp x20,x21,[sp,#-64]!\nstr x30,[sp,#16]\n'
                    'mov x20,x0\nmov x21,x1\nsite:\n'
                    + self.literal(data[0xd6734:0xd677c]) +
                    'ldr x30,[sp,#16]\nldp x20,x21,[sp],#64\nret\n'
                    '.org site+0x238\nmov x0,#-1\nb site+72\n'
                    '.org site+0x170c\nmov x0,x2\nret\n'
                    '.org site+0x1a3c\nmov x0,x2\nret\n')
            result = list(map(int, self.execute(code, source).splitlines()))
            expected = [
                1 if ext else max(n, 5760) if modified and not inp else n
                for ext in (0, 1) for inp in (0, 1)
                for n in (240, 480, 1920, 4096, 5760, 8192, 16384)]
            self.assertEqual(result, expected + [-1])


if __name__ == '__main__':
    unittest.main()
