"""Bounded speaker default: execute the real route with mixer I/O mocked."""
import gzip
import importlib.util
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
ROUTE = ROOT / 'probe/test2/audio-speaker-route'
HARNESS = r'''
set -eu
script=$1
action=$2
card=$3
grep() {
    [ "$#" = 3 ] && [ "$1" = -q ] && [ "$2" = Surface-Pro-X ] && [ "$3" = /proc/asound/cards ] || exit 77
    [ "$card" = present ]
}
tinymix() {
    [ "$1" = -D ] && [ "$2" = 0 ] && [ "$3" = set ] || exit 78
    shift 3
    printf '%s\t%s\n' "$1" "$2"
}
set -- "$action"
. "$script"
'''


class SpeakerDefault(unittest.TestCase):
    def run_route(self, action='enable', card='present'):
        return subprocess.run(['sh', '-c', HARNESS, 'speaker-test', str(ROUTE), action, card],
                              text=True, capture_output=True)

    def test_enable_unity_rx_and_unchanged_paired_ports(self):
        result = self.run_route()
        self.assertEqual(result.returncode, 0, result.stderr)
        controls = dict(line.split('\t') for line in result.stdout.splitlines())
        self.assertEqual(controls['RX7 Digital Volume'], '84')
        self.assertEqual(controls['RX8 Digital Volume'], '84')
        self.assertEqual(int(controls['RX7 Digital Volume']) - 84, 0)
        self.assertEqual(controls['SLIMBUS_0_RX Audio Mixer MultiMedia1'], '1')
        self.assertEqual(controls['SLIM RX0 MUX'], 'AIF1_PB')
        self.assertEqual(controls['SLIM RX1 MUX'], 'AIF1_PB')
        for side in ('SpkrLeft', 'SpkrRight'):
            self.assertEqual(controls[side + ' VISENSE Switch'], '0')
            for port in ('DAC', 'COMP', 'BOOST'):
                self.assertEqual(controls[f'{side} {port} Switch'], '1')
        self.assertFalse(any('PA Volume' in key for key in controls))
        self.assertEqual(len(controls), 17)

    def test_disable_does_not_rewrite_gains(self):
        result = self.run_route('disable')
        self.assertEqual(result.returncode, 0, result.stderr)
        controls = dict(line.split('\t') for line in result.stdout.splitlines())
        self.assertEqual(len(controls), 7)
        self.assertTrue(all(value == '0' for value in controls.values()))
        self.assertFalse(any('Volume' in key for key in controls))

    def test_invalid_mode_and_wrong_card_are_nonmutating(self):
        for action, card, status in [('invalid', 'present', 2), ('enable', 'missing', 1)]:
            result = self.run_route(action, card)
            self.assertEqual(result.returncode, status)
            self.assertEqual(result.stdout, '')

    def test_default_builder_explicitly_refreshes_and_audits_route(self):
        code = (ROOT / 'scripts/build-source-boot.py').read_text()
        self.assertIn("'audio-speaker-route','audio-microphone-route'", code)
        self.assertIn("after['bin/audio-speaker-route']==(0o100755,audio_route)", code)
        self.assertIn('speaker_rx_raw=[84,84]', code)
        self.assertIn('speaker_route_sha256=sha(audio_route)', code)

    @unittest.skipUnless(os.environ.get('SQ2_AUDIO_BOOT_IMAGE'), 'set SQ2_AUDIO_BOOT_IMAGE for real packed boot test')
    def test_actual_boot_archive_contains_current_default(self):
        spec = importlib.util.spec_from_file_location('cpio', ROOT / 'scripts/pack-sq2-vulkan.py')
        cpio = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cpio)
        packed = Path(os.environ['SQ2_AUDIO_BOOT_IMAGE']).read_bytes()
        entries = cpio.entries(gzip.decompress(packed))
        self.assertEqual(entries['bin/audio-speaker-route'],
                         (0o100755, ROUTE.read_bytes().replace(b'\r\n', b'\n')))
        self.assertIn(b'audio-speaker-route enable', entries['bin/audio-framework-boot'][1])


if __name__ == '__main__':
    unittest.main()
