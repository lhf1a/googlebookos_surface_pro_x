#!/usr/bin/env python3
"""Generate a bounded, attenuated stereo test: left 440 Hz, right 660 Hz."""
from pathlib import Path
import math
import struct
import wave
root = Path(__file__).resolve().parents[1]
path = root / 'out/audio-lte/speaker-test.wav'
rate = 48000
frames = bytearray()
for i in range(rate * 2):
    t = i / rate
    left = right = 0
    if 0.2 <= t < 0.7 or 1.1 <= t < 1.6:
        start, frequency = (0.2, 440) if t < 0.7 else (1.1, 660)
        elapsed = t - start
        envelope = min(1, elapsed / 0.02, (0.5 - elapsed) / 0.02)
        sample = round(4096 * envelope * math.sin(2 * math.pi * frequency * elapsed))
        if t < 0.7: left = sample
        else: right = sample
    frames.extend(struct.pack('<hh', left, right))
with wave.open(str(path), 'wb') as output:
    output.setnchannels(2)
    output.setsampwidth(2)
    output.setframerate(rate)
    output.writeframes(frames)
print(path)
