"""Host-only fixtures for the actual tracked OMX probe .

Set QUEST2_OMX_INCLUDE to official OpenMAX headers if the private downloaded
headers are absent. No donor library or Android device is invoked by these tests.
"""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'tests/fixtures/quest2_video'
HEADERS = Path(os.environ.get('QUEST2_OMX_INCLUDE',
    ROOT / 'out/quest2-video-618-20261107/agent-probe/include'))
CC = shutil.which(os.environ.get('CC', 'clang'))
HOST_OK = platform.system() == 'Linux' and platform.machine() in ('x86_64', 'aarch64') and bool(CC)


def execute(command, env=None):
    return subprocess.run([str(arg) for arg in command], capture_output=True,
                          text=True, env=env, timeout=30)


def build(output, source, *flags):
    result = execute([CC, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                      *flags, source, '-ldl', '-o', output])
    if result.returncode:
        raise AssertionError(f'Host fixture compilation failed: {result.stderr}')


@unittest.skipUnless(HOST_OK, 'requires Linux x86_64/aarch64 and a host C compiler')
class Quest2OmxProbe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (HEADERS / 'OMX_Component.h').is_file():
            raise unittest.SkipTest('set QUEST2_OMX_INCLUDE to official OpenMAX headers')
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.directory = Path(cls.tmp.name)
        cls.probe = cls.directory / 'probe'
        cls.library = cls.directory / 'fixture-libOmxCore.so'
        build(cls.probe, ROOT / 'probe/video/quest2-omx-probe.c', '-I' + str(HEADERS))
        build(cls.library, FIXTURES / 'omx-core.c', '-I' + str(HEADERS), '-shared', '-fPIC')

    def invoke(self, *arguments, expected=0):
        result = execute([self.probe, self.library, *arguments])
        self.assertEqual(result.returncode, expected, result.stderr)
        return result.stdout

    def test_default_load_does_not_initialize_or_create_handle(self):
        text = self.invoke()
        self.assertIn('RESULT load_only=ok hardware_handle_requested=0', text)
        self.assertNotIn('FIXTURE_INIT', text)
        self.assertNotIn('FIXTURE_GET_HANDLE', text)

    def test_explicit_init_is_paired_with_deinit_only(self):
        text = self.invoke('--init')
        self.assertIn('FIXTURE_INIT', text)
        self.assertIn('FIXTURE_DEINIT', text)
        self.assertNotIn('FIXTURE_GET_HANDLE', text)

    def test_enumerate_stops_at_no_more_without_handle(self):
        text = self.invoke('--enum')
        self.assertIn('name=OMX.fixture.decoder.avc', text)
        self.assertIn('OMX_ENUM index=1 status=0x8000100e', text)
        self.assertNotIn('FIXTURE_GET_HANDLE', text)

    def test_explicit_handle_loaded_state_and_cleanup(self):
        text = self.invoke('--get-handle', 'OMX.fixture.decoder.avc')
        for token in ('FIXTURE_GET_HANDLE', 'state=1', 'FIXTURE_FREE_HANDLE', 'FIXTURE_DEINIT'):
            self.assertIn(token, text)
        self.assertIn('decode_attempted=0 service_registered=0', text)

    def test_native_parameter_abi_and_failures_keep_cleanup(self):
        text = self.invoke('--native-buffers', 'OMX.fixture.decoder.avc')
        self.assertIn('NATIVE_USAGE result=0x00000000 usage=0x20000000', text)
        self.assertIn('NATIVE_PORT result=0x00000000 width=320 height=256 stride=384 scan=256 size=159744', text)
        for env_name, expected in [('FIXTURE_NO_NATIVE', 10), ('FIXTURE_REFUSE_LINEAR', 12)]:
            result = execute([self.probe, self.library, '--native-buffers', 'OMX.fixture.decoder.avc'],
                             dict(os.environ, **{env_name: '1'}))
            self.assertEqual(result.returncode, expected, result.stderr)
            self.assertIn('FIXTURE_FREE_HANDLE', result.stdout)
            self.assertIn('FIXTURE_DEINIT', result.stdout)
            self.assertNotIn('NATIVE_USAGE', result.stdout)

    def test_unknown_component_retains_failure(self):
        text = self.invoke('--get-handle', 'OMX.fixture.unknown', expected=6)
        self.assertIn('FIXTURE_DEINIT', text)
        self.assertNotIn('FIXTURE_FREE_HANDLE', text)

    def test_invalid_flag_does_not_load_library(self):
        text = self.invoke('--play', expected=64)
        self.assertNotIn('DLOPEN_BEGIN', text)

    def test_missing_library_fails_before_omx(self):
        result = execute([self.probe, self.directory / 'missing.so'])
        self.assertEqual(result.returncode, 2)
        self.assertIn('DLOPEN_ERROR', result.stderr)
        self.assertNotIn('OMX_INIT_BEGIN', result.stdout)


if __name__ == '__main__':
    unittest.main()
