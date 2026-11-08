"""Builder contracts with a synthetic compiler; actual NDK builds are separate.

No donor library, device, service or network is accessed by these tests.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('quest2_builder', ROOT/'scripts/build-quest2-video-probe.py')
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)
HEADERS = json.loads((ROOT/'configs/quest2-video-omx-headers.json').read_text())['sha256']
BASE = {'quest2-omx-probe', 'libquest2-io-trace.so'}


class Quest2VideoBuilder(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.headers = self.root/'headers'
        self.headers.mkdir()
        (self.root/'configs').mkdir()
        (self.root/'probe/video').mkdir(parents=True)
        pins = {}
        for name in HEADERS:
            data = ('synthetic header fixture: '+name).encode()
            (self.headers/name).write_bytes(data)
            pins[name] = hashlib.sha256(data).hexdigest()
        (self.root/'configs/quest2-video-omx-headers.json').write_text(json.dumps({'sha256': pins}))
        for name in ('quest2-omx-probe.c', 'quest2-video-io-trace.c',
                     'quest2-omx-decode.c', 'quest2-vidc-bridge.c', 'quest2-ion-probe.c', 'sq2-mediacodec-file.c'):
            (self.root/'probe/video'/name).write_text('synthetic source fixture '+name)
        self.calls = []
        self.failure = None

    def compiler(self, command, **kwargs):
        self.assertTrue(kwargs['capture_output'])
        self.assertIn('--target=aarch64-linux-android35', command)
        self.assertIn('-Werror', command)
        self.calls.append(command)
        source = next(Path(x).name for x in command if x.endswith('.c'))
        if source == self.failure:
            return subprocess.CompletedProcess(command, 19, 'literal compiler stdout', 'literal compiler failure')
        Path(command[command.index('-o')+1]).write_bytes(('synthetic compiler output '+source).encode())
        return subprocess.CompletedProcess(command, 0, '', '')

    def invoke(self, *flags, output=None):
        output = output or self.root/'output'
        argv = ['build-quest2-video-probe.py', '--cc', str(self.root/'fixture-clang'),
                '--headers', str(self.headers), '--output', str(output), *flags]
        with patch.object(BUILDER, 'ROOT', self.root), patch.object(sys, 'argv', argv), \
             patch.object(BUILDER.subprocess, 'run', side_effect=self.compiler), \
             contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            BUILDER.main()
        return output, json.loads((output/'BUILD.json').read_text())

    def test_default_still_builds_exact_original_two(self):
        output, report = self.invoke()
        self.assertEqual(set(report['sha256']), BASE)
        self.assertEqual(len(self.calls), 2)
        self.assertEqual(report['optional_features'], {'decode_client': False, 'vidc_bridge': False, 'media_file': False})
        self.assertEqual(len(report['header_sha256']), 9)
        self.assertEqual(report['target'], 'aarch64-linux-android35')
        self.assertEqual(report['status'], 'compiled-not-decoding-verified')
        for name, digest in report['sha256'].items():
            self.assertEqual(hashlib.sha256((output/name).read_bytes()).hexdigest(), digest)

    def test_independent_explicit_options_and_combination(self):
        for i, (flags, additions) in enumerate([
            (['--with-decode-client'], {'quest2-omx-decode'}),
            (['--with-vidc-bridge'], {'libquest2-vidc-bridge.so', 'quest2-ion-probe'}),
            (['--with-decode-client', '--with-vidc-bridge'], {'quest2-omx-decode', 'libquest2-vidc-bridge.so', 'quest2-ion-probe'}),
        ]):
            with self.subTest(flags=flags):
                self.calls.clear()
                _, report = self.invoke(*flags, output=self.root/f'output-{i}')
                self.assertEqual(set(report['sha256']), BASE | additions)
                self.assertEqual(len(report['source_sha256']), len(BASE | additions))
                for command in self.calls:
                    if any(x.endswith('quest2-omx-decode.c') for x in command):
                        self.assertIn('-pthread', command)
                        self.assertIn('-I', command)
                        self.assertNotIn('-shared', command)
                    if any(x.endswith('quest2-vidc-bridge.c') for x in command):
                        self.assertIn('-pthread', command)
                        self.assertIn('-fPIC', command)
                        self.assertIn('-shared', command)

    def test_container_probe_requires_and_records_real_platform_dependency(self):
        binder = self.root/'libbinder_ndk.so'
        with self.assertRaises(SystemExit):
            self.invoke('--media-file-binder', str(binder))
        self.assertFalse((self.root/'output').exists())
        binder.write_bytes(b'synthetic platform link library')
        _, report = self.invoke('--media-file-binder', str(binder))
        command = self.calls[-1]
        self.assertEqual(set(report['sha256']), BASE | {'sq2-mediacodec-file'})
        self.assertIn(str(binder), command)
        self.assertIn('-lmediandk', command)
        self.assertEqual(report['media_file_binder_sha256'], hashlib.sha256(binder.read_bytes()).hexdigest())

    def test_header_mismatch_fails_before_output_or_compiler(self):
        (self.headers/'OMX_Core.h').write_text('corrupted fixture')
        with self.assertRaises(SystemExit) as error:
            self.invoke('--with-decode-client', '--with-vidc-bridge')
        self.assertEqual(error.exception.code, 1)
        self.assertFalse((self.root/'output').exists())
        self.assertFalse(self.calls)

    def test_existing_output_is_not_overwritten(self):
        output = self.root/'output'
        output.mkdir()
        (output/'keep').write_bytes(b'unchanged')
        with self.assertRaises(SystemExit):
            self.invoke('--with-decode-client')
        self.assertEqual((output/'keep').read_bytes(), b'unchanged')
        self.assertFalse(self.calls)

    def test_compile_failure_keeps_literal_ledger_without_success_manifest(self):
        self.failure = 'quest2-omx-decode.c'
        with self.assertRaises(SystemExit):
            self.invoke('--with-decode-client', '--with-vidc-bridge')
        records = json.loads((self.root/'output/COMMANDS.json').read_text())
        self.assertEqual(len(records), 3)
        self.assertEqual(records[-1]['exit'], 19)
        self.assertEqual(records[-1]['stdout'], 'literal compiler stdout')
        self.assertEqual(records[-1]['stderr'], 'literal compiler failure')
        self.assertFalse((self.root/'output/BUILD.json').exists())


if __name__ == '__main__':
    unittest.main()
