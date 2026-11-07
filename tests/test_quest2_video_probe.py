"""Host-only fixtures for the actual tracked OMX probe and transparent I/O tracer.

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
CC = shutil.which('clang')
HOST_OK = platform.system() == 'Linux' and platform.machine() in ('x86_64', 'aarch64') and bool(CC)


def execute(command, env=None):
    return subprocess.run([str(arg) for arg in command], capture_output=True,
                          text=True, env=env, timeout=30)


def build(output, source, *flags):
    result = execute([CC, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                      *flags, source, '-ldl', '-o', output])
    if result.returncode:
        raise AssertionError(f'Host fixture compilation failed: {result.stderr}')


@unittest.skipUnless(HOST_OK, 'requires Linux x86_64/aarch64 and host clang')
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


@unittest.skipUnless(HOST_OK, 'requires Linux x86_64/aarch64 and host clang')
class Quest2IoTrace(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # These exact path names are required to test the opt-in remapping. Never
        # open an existing hardware node on a host that happens to expose them.
        for name in ('/dev/ion', '/dev/video32', '/dev/video99999999'):
            if os.path.lexists(name):
                raise unittest.SkipTest('hardware-like fixture path exists: ' + name)
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.directory = Path(cls.tmp.name)
        cls.library = cls.directory / 'trace.so'
        cls.fixture = cls.directory / 'io-calls'
        build(cls.library, ROOT / 'probe/video/quest2-video-io-trace.c', '-shared', '-fPIC')
        build(cls.fixture, FIXTURES / 'io-calls.c')
        cls.base_env = dict(os.environ)
        cls.base_env.pop('LD_PRELOAD', None)
        cls.base_env.pop('QUEST2_VIDEO_NODE', None)
        cls.baseline = execute([cls.fixture], cls.base_env)
        if cls.baseline.returncode:
            raise AssertionError(cls.baseline.stderr)

    def invoke(self, mapping=None):
        env = dict(self.base_env, LD_PRELOAD=str(self.library))
        if mapping is not None:
            env['QUEST2_VIDEO_NODE'] = mapping
        result = execute([self.fixture], env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, self.baseline.stdout)
        return result

    def test_forwarding_preserves_open_ioctl_results_and_errno(self):
        result = self.invoke()
        for token in ('open result=0 errno=77', 'open64 result=0 errno=77',
                      'openat result=0 errno=77', '__open_2 result=0 errno=77',
                      'ioctl-read result=0 errno=77 count=3',
                      'ioctl-noarg result=0 errno=77 cloexec=1',
                      'ioctl-error result=-1 errno=25'):
            self.assertIn(token, result.stdout)
        self.assertIn('request=0x4020565a return=-1 errno=25', result.stderr)
        self.assertIn('map_video32=disabled', result.stderr)
        self.assertIn('path=/dev/video32 effective=/dev/video32', result.stderr)

    def test_explicit_video_mapping_never_maps_ion(self):
        result = self.invoke('/dev/video99999999')
        self.assertIn('path=/dev/video32 effective=/dev/video99999999', result.stderr)
        self.assertIn('path=/dev/ion effective=/dev/ion', result.stderr)

    def test_invalid_mapping_does_not_change_paths(self):
        result = self.invoke('/dev/ion')
        self.assertIn('invalid QUEST2_VIDEO_NODE ignored', result.stderr)
        self.assertIn('path=/dev/video32 effective=/dev/video32', result.stderr)


if __name__ == '__main__':
    unittest.main()