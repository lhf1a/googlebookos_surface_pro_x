"""Client lifecycle/ownership tests against an explicit synthetic host mock.

These do not decode AVC or establish vendor/kernel compatibility. No device
nodes, Android libraries, firmware, or services are used.
"""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HEADERS = Path(os.environ.get('QUEST2_OMX_INCLUDE',
    ROOT / 'out/quest2-video-618-20261107/agent-probe/include'))
CC = shutil.which(os.environ.get('CC', 'clang')) or shutil.which('cc')


@unittest.skipUnless(platform.system() == 'Linux' and CC, 'requires Linux host C compiler')
class OmxDecodeClient(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (HEADERS / 'OMX_Component.h').exists():
            raise unittest.SkipTest('set QUEST2_OMX_INCLUDE to official AOSP OMX headers')
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        cls.client, cls.library = cls.root / 'client', cls.root / 'mock.so'
        flags = ['-fsanitize=address,undefined', '-g'] if os.environ.get('QUEST2_SANITIZE') else []
        for source, output, extra in [
            (ROOT / 'probe/video/quest2-omx-decode.c', cls.client, []),
            (ROOT / 'tests/fixtures/quest2_video/omx-decode-core.c', cls.library, ['-shared', '-fPIC']),
        ]:
            result = subprocess.run([CC, '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror',
                *flags, '-I' + str(HEADERS), *extra, str(source), '-ldl', '-pthread', '-o', str(output)],
                capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(result.stderr)

    def test_hidl_port_wire_abi(self):
        binary = self.root / 'port-abi-test'
        subprocess.run([CC, '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
            '-I' + str(HEADERS), '-I' + str(ROOT / 'probe/video'),
            str(ROOT / 'tests/fixtures/quest2_video/omx-core-abi-test.c'),
            '-o', str(binary)], check=True, capture_output=True, text=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def setUp(self):
        self.temp_case = tempfile.TemporaryDirectory(dir=self.root)
        self.addCleanup(self.temp_case.cleanup)
        self.case = Path(self.temp_case.name)
        self.input, self.aus, self.output = self.case / 'bytes', self.case / 'aus', self.case / 'output'
        # Not AVC: the clearly named mock only exercises API transport.
        self.input.write_bytes(b'X' * 16)
        self.aus.write_text('6 0\n6 100000\n4 33333\n')

    def invoke(self, expected=0, args=(), **environment):
        env = {key: value for key, value in os.environ.items()
               if not key.startswith('MOCK_') and key not in ('LD_PRELOAD', 'LD_LIBRARY_PATH')}
        env.update(environment)
        result = subprocess.run([str(self.client), '--library', str(self.library),
            '--input', str(self.input), '--aus', str(self.aus), '--output', str(self.output),
            '--timeout-ms', '1000', *args], env=env, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        self.assertNotIn('AddressSanitizer', result.stderr)
        self.assertNotIn('runtime error:', result.stderr)
        return result

    def assert_linear_frames(self, stride):
        rows = (self.output / 'frames.tsv').read_text().splitlines()[1:]
        self.assertEqual(len(rows), 3)
        self.assertEqual([int(row.split('\t')[1]) for row in rows], [0, 33333, 100000])
        self.assertTrue(all(int(row.split('\t')[4]) == stride for row in rows))
        for index in range(3):
            # Verify row-padding removal, not a claimed real decoded image.
            wanted = bytes([0x10 + index]) * 16 + bytes([0x50 + index]) * 8
            self.assertEqual((self.output / f'frame-{index:06d}.nv12').read_bytes(), wanted)

    def test_states_fragmentation_eos_display_order_and_release(self):
        result = self.invoke()
        self.assertIn('MOCK_ETB bytes=4 flags=0x0 pts=0', result.stdout)
        self.assertIn('MOCK_ETB bytes=2 flags=0x10 pts=0', result.stdout)
        self.assertIn('output_eos=1 frames=3 linear_nv12=3', result.stdout)
        self.assertIn('MOCK_FREE_HANDLE buffers_remaining=0 state=1', result.stdout)
        self.assertIn('CLEANUP buffers_and_states=1 handle_released=1', result.stdout)
        self.assert_linear_frames(8)

    def test_color_and_hdr_events_do_not_flush_or_reallocate_frames(self):
        result = self.invoke(MOCK_METADATA_EVENTS='1')
        self.assertEqual(result.stdout.count('METADATA_CHANGED'), 3)
        self.assertNotIn('COMMAND command=2', result.stdout)
        self.assertNotIn('COMMAND command=3', result.stdout)
        self.assert_linear_frames(8)

    def test_requested_input_buffers_preserve_minimum_and_allocate_actual(self):
        result = self.invoke(args=('--input-buffers', '3', '--width', '4', '--height', '4'))
        self.assertEqual(result.stdout.count('MOCK_PORT_SET actual=3 min=2'), 1)
        self.assertIn('PORT index=0 direction=0 count=3 min=2', result.stdout)
        self.assertEqual(result.stdout.count('BUFFER port=0 index='), 3)
        self.assertIn('CLEANUP buffers_and_states=1 handle_released=1', result.stdout)

    def test_input_count_below_minimum_never_allocates_or_submits(self):
        result = self.invoke(expected=5, args=('--input-buffers', '1'))
        self.assertIn('INPUT_BUFFERS_BELOW_MIN', result.stderr)
        self.assertNotIn('MOCK_PORT_SET', result.stdout)
        self.assertNotIn('MOCK_ETB', result.stdout)
        self.assertNotIn('API AllocateBuffer', result.stdout)

    def test_input_count_vendor_rejection_or_ignored_request_stops(self):
        for mode in ('MOCK_COUNT_FAIL', 'MOCK_COUNT_IGNORE'):
            with self.subTest(mode=mode):
                self.output = self.case / mode
                result = self.invoke(expected=5, args=('--input-buffers', '3'), **{mode: '1'})
                self.assertNotIn('MOCK_ETB', result.stdout)
                self.assertNotIn('API AllocateBuffer', result.stdout)
                if mode == 'MOCK_COUNT_FAIL':
                    self.assertIn('SetParameter(InputPortDefinition)=0x80001019', result.stdout)
                else:
                    self.assertIn('INPUT_BUFFERS_NOT_ACCEPTED', result.stderr)
                self.assertIn('CLEANUP buffers_and_states=1 handle_released=1', result.stdout)

    def test_input_count_invalid_cli_never_initializes_core(self):
        for value in ('0', '-1', '4294967296', 'invalid'):
            with self.subTest(value=value):
                result = self.invoke(expected=64, args=('--input-buffers', value))
                self.assertNotIn('MOCK_INIT', result.stdout)

    def test_selected_codec_port_must_match(self):
        for suffix in ('hevc', 'vp8', 'vp9', 'mpeg2'):
            with self.subTest(codec=suffix):
                self.output=self.case/('output-'+suffix)
                result=self.invoke(args=('--component','OMX.qcom.video.decoder.'+suffix))
                self.assertIn('output_eos=1 frames=3',result.stdout)
        self.output=self.case/'output-wrong'
        result=self.invoke(expected=5,args=('--component','OMX.qcom.video.decoder.hevc'),MOCK_WRONG_CODEC='1')
        self.assertNotIn('MOCK_ETB',result.stdout)

    def test_dynamic_output_port_disable_free_enable_reallocate(self):
        result = self.invoke(MOCK_RECONFIGURE='1')
        self.assertIn('OUTPUT_RECONFIGURE_BEGIN', result.stdout)
        self.assertIn('OUTPUT_RECONFIGURE_DONE', result.stdout)
        self.assertIn('allocated=144', result.stdout)
        self.assertIn('MOCK_FREE_HANDLE buffers_remaining=0 state=1', result.stdout)
        self.assert_linear_frames(12)

    def test_asynchronous_command_callbacks_do_not_deadlock(self):
        result = self.invoke(MOCK_ASYNC='1', MOCK_RECONFIGURE='1')
        self.assertIn('CLEANUP buffers_and_states=1 handle_released=1', result.stdout)
        self.assert_linear_frames(12)

    def test_etb_error_still_returns_idle_loaded_and_frees(self):
        result = self.invoke(expected=5, MOCK_ETB_FAIL='1')
        self.assertIn('MOCK_FREE_HANDLE buffers_remaining=0 state=1', result.stdout)
        self.assertIn('frames=0', result.stdout)

    def test_allocation_failure_reports_incomplete_transition_and_releases_handle(self):
        result = self.invoke(expected=6, MOCK_ALLOC_FAIL='1')
        self.assertIn('CLEANUP_PENDING_COMMAND', result.stderr)
        self.assertIn('CLEANUP buffers_and_states=0 handle_released=1', result.stdout)
        self.assertIn('MOCK_DEINIT', result.stdout)

    def test_out_of_bounds_output_is_not_read_or_saved(self):
        self.invoke(expected=5, MOCK_BAD_BOUNDS='1')
        self.assertEqual(list(self.output.glob('frame-*')), [])

    def test_non_linear_output_is_raw_only_not_fabricated_nv12(self):
        result = self.invoke(MOCK_NONLINEAR='1')
        self.assertIn('frames=3 linear_nv12=0', result.stdout)
        self.assertEqual(len(list(self.output.glob('*.raw'))), 3)
        self.assertEqual(list(self.output.glob('*.nv12')), [])
        self.assertIn('FRAME_NOT_LINEAR_NV12', result.stderr)

    def test_venus_nv12_requests_exact_vendor_format_and_checks_geometry(self):
        result = self.invoke(args=('--venus-nv12',))
        self.assertIn('MOCK_FORMAT color=0x7fa30c04', result.stdout)
        self.assertIn('frames=3 linear_nv12=3', result.stdout)
        self.assertEqual(len(list(self.output.glob('*.raw'))), 3)
        self.assert_linear_frames(8)

    def test_venus_format_rejection_is_not_replaced_by_another_format(self):
        result = self.invoke(expected=5, args=('--venus-nv12',), MOCK_FORMAT_FAIL='1')
        self.assertIn('SetParameter(OutputVenusNV12)=0x80001019', result.stdout)

    def test_format_rejection_retains_original_error_and_cleans_up(self):
        result = self.invoke(expected=5, MOCK_FORMAT_FAIL='1')
        self.assertIn('SetParameter(OutputLinearNV12)=0x80001019', result.stdout)
        self.assertIn('MOCK_FREE_HANDLE buffers_remaining=0 state=1', result.stdout)

    def test_manifest_size_mismatch_never_initializes_core(self):
        self.aus.write_text('15 0\n')
        result = self.invoke(expected=65)
        self.assertNotIn('MOCK_INIT', result.stdout)
        self.assertFalse(self.output.exists())

    def test_existing_output_not_overwritten(self):
        self.output.mkdir()
        marker = self.output / 'keep'
        marker.write_bytes(b'unchanged')
        result = self.invoke(expected=66)
        self.assertNotIn('MOCK_INIT', result.stdout)
        self.assertEqual(marker.read_bytes(), b'unchanged')


    def test_reusable_session_transport_and_failures(self):
        binary = self.root / 'session'
        flags = ['-fsanitize=address,undefined', '-g', '-fno-pie', '-no-pie'] if os.environ.get('QUEST2_SANITIZE') else []
        result = subprocess.run([CC, '-std=gnu11', '-O1', '-Wall', '-Wextra',
            '-Werror', '-Wno-unused-function', *flags, '-I' + str(HEADERS),
            '-I' + str(ROOT / 'probe/video'),
            str(ROOT / 'tests/fixtures/quest2_video/omx-session-test.c'),
            str(ROOT / 'probe/video/quest2-omx-session.c'), '-ldl', '-pthread', '-o', str(binary)],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        for mode in ('', 'MOCK_RECONFIGURE', 'MOCK_ASYNC', 'MOCK_ETB_FAIL',
                     'MOCK_FORMAT_FAIL', 'MOCK_ALLOC_FAIL', 'MOCK_BAD_BOUNDS'):
            with self.subTest(mode=mode):
                env = {k: v for k, v in os.environ.items() if not k.startswith('MOCK_')}
                if mode: env[mode] = '1'
                run = subprocess.run([str(binary), str(self.library)], env=env,
                    capture_output=True, text=True, timeout=15)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertIn('MOCK_DEINIT', run.stdout)


if __name__ == '__main__':
    unittest.main()
