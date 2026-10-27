#!/usr/bin/env python3
"""Generate bounded, faded stereo probes; never boost the amplifier to run them."""
from pathlib import Path
import argparse
import json
import math
import struct
import wave

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, default=Path('out/audio-probes'))
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
rate, amplitude = 48000, 8192  # -12 dBFS peak, 50-ms fades and a 1-second silent tail
probes = {
    'stereo': [(440, 1, 0), (440, 0, 1)],
    'polarity': [(250, 1, 1), (250, 1, -1), (500, 1, 1), (500, 1, -1)],
    'frequency': [(1000, 1, 1), (500, 1, 1), (250, 1, 1), (125, 1, 1)],
    'bass-left': [(500, 1, 0), (250, 1, 0), (200, 1, 0), (160, 1, 0), (125, 1, 0)],
    'bass-right': [(500, 0, 1), (250, 0, 1), (200, 0, 1), (160, 0, 1), (125, 0, 1)],
    'bass-polarity': [(160, 1, 0), (160, 0, 1), (160, 1, 1), (160, 1, -1)],
    # Keep one PCM stream open: channel changes must not power-cycle the amps.
    'channel-continuity': [(440, 1, 0), (440, 0, 1), (440, 1, 0), (440, 0, 1)],
}
for name, segments in probes.items():
    data = bytearray()
    for frequency, left, right in segments:
        data.extend(b'\0' * (rate * 4 // 2))
        for i in range(2 * rate):
            t = i / rate
            fade = max(0, min(1, t / 0.05, (2 - t - 1/rate) / 0.05))
            sample = int(amplitude * fade * math.sin(2 * math.pi * frequency * t))
            data.extend(struct.pack('<hh', sample * left, sample * right))
    data.extend(b'\0' * (rate * 4))
    with wave.open(str(args.output / f'{name}.wav'), 'wb') as wav:
        wav.setparams((2, 2, rate, 0, 'NONE', 'not compressed'))
        wav.writeframes(data)
(args.output / 'manifest.json').write_text(json.dumps({
    'sample_rate': rate, 'channels': 2, 'bits': 16, 'peak_amplitude': amplitude,
    'segment_seconds': 2, 'gap_seconds': 0.5, 'tail_seconds': 1,
    'segments_frequency_left_right': probes,
    'note': 'Polarity comparison is diagnostic only; no inversion/EQ is applied to the system.',
}, indent=2) + '\n')
print(args.output)
