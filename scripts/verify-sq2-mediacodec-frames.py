#!/usr/bin/env python3
"""Compare tightly packed MediaCodec I420 frames to an independent NV12 reference.

Only the six-frame 320x240 laboratory fixture is covered. A passing pixel
comparison must be paired with the same run's transport/EOS and format log.
"""
import argparse
from pathlib import Path
import zlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    y_size = 320 * 240
    frame_size = y_size * 3 // 2
    reference = args.reference.read_bytes()
    if len(reference) != frame_size * 6:
        raise SystemExit("reference must contain exactly six 320x240 NV12 frames")
    for index in range(6):
        frame = (args.frames / f"sq2-ndk-frame-{index}.bin").read_bytes()
        if len(frame) != frame_size:
            raise SystemExit(f"frame {index}: unexpected size {len(frame)}")
        uv = bytearray(y_size // 2)
        uv[0::2] = frame[y_size:y_size * 5 // 4]
        uv[1::2] = frame[y_size * 5 // 4:]
        nv12 = frame[:y_size] + uv
        expected = reference[index * frame_size:(index + 1) * frame_size]
        if nv12 != expected:
            first = next(i for i, pair in enumerate(zip(nv12, expected)) if pair[0] != pair[1])
            raise SystemExit(f"frame {index}: mismatch at NV12 byte {first}")
        print(f"frame={index} bytes={len(frame)} match=1 "
              f"crc_y={zlib.crc32(nv12[:y_size]):08x} "
              f"crc_uv={zlib.crc32(nv12[y_size:]):08x}")
    print("ALL_6_FRAMES_BYTE_EXACT")


if __name__ == "__main__":
    main()
