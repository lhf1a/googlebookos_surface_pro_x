#!/usr/bin/env python3
"""Offline inspection of Quest 2 v77 uncompressed layout-2 RGBA8 buffers.

This never opens a device or edits a driver. Supply bytes captured read-only from
image+16, with byte pitch from image+24. In the observed single-plane capture,
u64 image+32 equals pitch * u32 image+208 (allocated/padded height). Validate the
mapping and cap the read before capturing; this script cannot validate a pointer.

The address function transcribes original libGLESv2_adreno.so scalar detiler
0x2bbd38, RGBA branch 0x2bc340/0x2bc5a4, and bank operations 0x2bc7b8..0x2bc8c8.
The 16x4 fast path is 0x2bf718 with row helper 0x2c0478. SHA256 of the audited
original: a0d4d099a67d4a0db54ee6b5cd69e15e1787484128667120c7a90d6ae333b316.
Only format 28, one sample, layout 2, four bytes/pixel is modeled. No UBWC decode.

mode is copy-context+312 (8 selects eight-channel; 4 represents other branch),
bank_bit is copy-context+316, and bank_extra is copy-context+320 == 1.
Their names describe observed arithmetic; they do not authorize changing GPU
capability fields. In particular do not equate bank_bit numerically with Mesa's
highest_bank_bit without comparing the differing implementation conventions.

Example:
  python3 scripts/inspect-quest2-tiled-readback.py --raw private/tiled.bin \
      --width 640 --height 360 --pitch 2560 --padded-height 368 --phase 0
The default expected pixels match quest2-readpixels.c's exact coordinate oracle.
--expected-rgba accepts a tightly packed linear CPU relay capture instead.
"""

import argparse
import hashlib
import json
from pathlib import Path


def rgba_offset(x, y, pitch, mode=8, bank_bit=16, bank_extra=False,
                chip=0x5002A8):
    """Return byte offset for one RGBA pixel using donor scalar arithmetic.

    Before bank XORs, offset = (y & ~15) * pitch + 4 * P, with P bits:
      0:x0, 1:y0, 2:x1, 3:y1, 4:x2, 5:x3, 6:x4^y3,
      7:x4^x5^y2^y3, 8:x4^y2^[mode8: x6^y4], 9:x5, 10+:x6+.
    Here xn/yn denote coordinate bits, and ^ denotes XOR.
    """
    if min(x, y) < 0 or pitch <= 0 or pitch % 256:
        raise ValueError("nonnegative coordinates and 256-byte aligned pitch required")
    if mode not in (4, 8) or bank_bit not in range(13, 17):
        raise ValueError("audited modes are 4/8 and bank_bit 13..16")
    xb = [(x >> i) & 1 for i in range(7)]
    yb = [(y >> i) & 1 for i in range(6)]
    p = (xb[0] | (yb[0] << 1) | (xb[1] << 2) | (yb[1] << 3)
         | (xb[2] << 4) | (xb[3] << 5) | ((xb[4] ^ yb[3]) << 6)
         | ((xb[4] ^ xb[5] ^ yb[2] ^ yb[3]) << 7)
         | ((xb[4] ^ yb[2] ^ ((xb[6] ^ yb[4]) if mode == 8 else 0)) << 8)
         | (xb[5] << 9) | ((x >> 6) << 10))
    offset = ((y & ~15) * pitch + 4 * p) & 0xFFFFFFFF
    stride16 = (pitch << 4) & 0xFFFFFFFF
    if bank_extra and bank_bit >= 14 and not stride16 & ((1 << (bank_bit - 1)) - 1):
        offset ^= (xb[5] ^ yb[3]) << (bank_bit - 2)
    if chip != 0x500297 and not stride16 & ((1 << bank_bit) - 1):
        offset ^= yb[4] << (bank_bit - 1)
    if not stride16 & ((1 << (bank_bit + 1)) - 1):
        offset ^= yb[5] << bank_bit
    return offset


def expected_pixel(x, y, phase):
    return bytes(((x * 17 + y * 3 + phase * 13) & 255,
                  (x * 7 + y * 29 + phase * 71) & 255,
                  (x * 23 + y * 11 + phase * 47) & 255,
                  64 + (x * 5 + y * 11 + phase * 37) % 192))


def self_test():
    """Check range/uniqueness and the 4/8-channel permutation independently."""
    checked = 0
    for pitch, height in ((256, 64), (1280, 144), (2560, 368), (2560, 384),
                          (4096, 64), (8192, 128)):
        for mode in (4, 8):
            offsets = {rgba_offset(x, y, pitch, mode)
                       for y in range(height) for x in range(pitch // 4)}
            assert len(offsets) == pitch // 4 * height, (pitch, height, mode, "collision")
            assert min(offsets) == 0 and max(offsets) + 4 <= pitch * height
            checked += len(offsets)
    # For these pitches bank-extra is off and the fixed permutation preserves
    # all bank XOR coordinate bits. It is an address identity, not a device test.
    for pitch in (256, 1280, 2560, 4096, 8192):
        for y in range(64):
            for x in range(pitch // 4):
                swap = ((x >> 6) ^ (y >> 4)) & 1
                xx, yy = (x ^ 16, y ^ 8) if swap else (x, y)
                assert rgba_offset(x, y, pitch, 8) == rgba_offset(xx, yy, pitch, 4)
    return {"status": "PASS", "bounded_unique_addresses_checked": checked,
            "permutation": "if ((x>>6)^(y>>4))&1: (x^16,y^8), else (x,y)",
            "limit": "offline arithmetic check; raw capture needed to identify physical layout"}


def inspect(raw, width, height, pitch, mode, bank_bit, bank_extra, chip,
            phase, expected):
    bad_bytes = bad_pixels = out_of_range = 0
    first = []
    minimum = len(raw)
    maximum = 0
    for y in range(height):
        for x in range(width):
            offset = rgba_offset(x, y, pitch, mode, bank_bit, bank_extra, chip)
            minimum = min(minimum, offset)
            maximum = max(maximum, offset + 4)
            if offset + 4 > len(raw):
                out_of_range += 1
                if len(first) < 12:
                    first.append({"x": x, "y": y, "offset": offset, "error": "outside capture"})
                continue
            actual = raw[offset:offset + 4]
            i = (y * width + x) * 4
            want = expected[i:i + 4] if expected is not None else expected_pixel(x, y, phase)
            count = sum(a != b for a, b in zip(actual, want))
            bad_bytes += count
            if count:
                bad_pixels += 1
                if len(first) < 12:
                    first.append({"x": x, "y": y, "offset": offset,
                                  "actual": list(actual), "expected": list(want)})
    return {"mode": mode, "bank_bit": bank_bit, "bank_extra": bank_extra,
            "bad_bytes": bad_bytes, "bad_pixels": bad_pixels,
            "out_of_range_pixels": out_of_range, "minimum_offset": minimum,
            "required_capture_bytes": maximum, "first_mismatches": first,
            "status": "PASS" if not (bad_bytes or out_of_range) else "FAIL"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--raw", type=Path)
    ap.add_argument("--width", type=int)
    ap.add_argument("--height", type=int)
    ap.add_argument("--pitch", type=int, help="source image+24, bytes")
    ap.add_argument("--padded-height", type=int, help="source image+208; checks captured span")
    ap.add_argument("--phase", type=int, default=0)
    ap.add_argument("--expected-rgba", type=Path)
    ap.add_argument("--mode", choices=("4", "8", "both"), default="both")
    ap.add_argument("--bank-bit", type=int, choices=range(13, 17), default=16)
    ap.add_argument("--bank-extra", action="store_true", help="copy-context+320 equals 1")
    ap.add_argument("--chip", type=lambda x: int(x, 0), default=0x5002A8)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        print(json.dumps({"self_test": self_test()}, indent=2))
        if args.raw is None:
            return
    if args.raw is None or any(v is None for v in (args.width, args.height, args.pitch)):
        ap.error("provide --raw, --width, --height, --pitch (or --self-test)")
    if min(args.width, args.height) <= 0 or args.pitch < args.width * 4 or args.pitch % 256:
        ap.error("positive dimensions and pitch >= width*4 aligned to 256 bytes required")
    if args.width > 8192 or args.height > 8192 or args.raw.stat().st_size > 256 * 1024 * 1024:
        ap.error("inspection is bounded to 8192x8192 and 256 MiB input")
    raw = args.raw.read_bytes()
    if args.padded_height is not None:
        if args.padded_height < args.height or len(raw) != args.pitch * args.padded_height:
            ap.error("capture length must equal pitch*padded-height, and padded-height >= height")
    expected = args.expected_rgba.read_bytes() if args.expected_rgba else None
    if expected is not None and len(expected) != args.width * args.height * 4:
        ap.error("--expected-rgba must be tightly packed width*height*4 bytes")
    modes = (4, 8) if args.mode == "both" else (int(args.mode),)
    report = {"raw_sha256": hashlib.sha256(raw).hexdigest(), "raw_bytes": len(raw),
              "width": args.width, "height": args.height, "pitch": args.pitch,
              "oracle": "linear relay capture" if expected is not None else "quest2-readpixels coordinate pattern",
              "phase": args.phase, "results": [inspect(raw, args.width, args.height, args.pitch,
                  m, args.bank_bit, args.bank_extra, args.chip, args.phase, expected) for m in modes]}
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
