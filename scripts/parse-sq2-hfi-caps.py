#!/usr/bin/env python3
"""Sequentially parse SQ2_HFI_CAPS_RAW lines from a bounded kmsg capture.

Walks the SYS_INIT_DONE body as [property id][payload] records using the
pinned Linux 6.3 Venus HFI 4xx payload layouts. Stops at the first unknown
property instead of guessing its size. Offline only; never contacts a device.
"""
import argparse
import json
import re
import sys
from pathlib import Path

CODECS = {0x2: 'H264', 0x4: 'H263', 0x8: 'MPEG1', 0x10: 'MPEG2', 0x20: 'MPEG4',
          0x40: 'DIVX311', 0x80: 'DIVX', 0x100: 'VC1', 0x200: 'SPARK',
          0x1000: 'VP8', 0x2000: 'HEVC', 0x4000: 'VP9', 0x80000000: 'HEVC_HYBRID'}
DOMAINS = {0x1: 'encoder', 0x2: 'decoder'}
CAPS = {0x1: 'frame_width', 0x2: 'frame_height', 0x3: 'mbs_per_frame',
        0x4: 'mbs_per_second', 0x5: 'framerate', 0x6: 'scale_x', 0x7: 'scale_y',
        0x8: 'bitrate', 0x9: 'bframe', 0xa: 'peak_bitrate', 0x10: 'hier_p_layers',
        0x11: 'ltr_count', 0x12: 'cp_output2_thresh', 0x13: 'hier_b_layers',
        0x14: 'lcu_size', 0x15: 'hier_p_hybrid_layers', 0x16: 'mbs_per_second_powersave',
        0x20: 'i_frame_qp', 0x21: 'p_frame_qp', 0x22: 'b_frame_qp',
        0x23: 'rate_control_modes', 0x24: 'blur_width', 0x25: 'blur_height',
        0x27: 'slice_byte', 0x28: 'slice_mb', 0x2b: 'max_videocores',
        0x2c: 'max_workmodes', 0x2f: 'rotation', 0x30: 'color_space_conversion'}
FORMATS = {0x2: 'NV12', 0x3: 'NV21', 0x4: 'NV12_4x4TILE', 0x4002: 'YUV420_TP10',
           0x4003: 'P010', 0x8002: 'NV12_UBWC', 0xc002: 'YUV420_TP10_UBWC',
           0xc003: 'P010_UBWC', 0x8010: 'RGBA8888_UBWC'}
BUFFERS = {0x1: 'input', 0x2: 'output', 0x3: 'output2'}


def bits(mask, names):
    out = [n for b, n in names.items() if mask & b]
    rest = mask & ~sum(names)
    return out + ([f'0x{rest:x}'] if rest else [])


def load(path):
    rows = {}
    for line in path.read_text(errors='replace').splitlines():
        m = re.search(r'SQ2_HFI_CAPS_RAW off=(\d+) ((?:[0-9a-f]{8} ?){8})', line)
        if m:
            rows[int(m.group(1))] = [int(x, 16) for x in m.group(2).split()]
    if not rows or sorted(rows) != list(range(0, 8 * len(rows), 8)):
        sys.exit('missing or non-contiguous SQ2_HFI_CAPS_RAW rows')
    words = [w for off in sorted(rows) for w in rows[off]]
    size = words[0]
    if size % 4 or size // 4 > len(words) or words[1] != 0x20001:
        sys.exit('bad SYS_INIT_DONE header')
    pad = words[size // 4:]
    if any(pad) or len(pad) >= 8:
        sys.exit('nonzero padding after declared packet size')
    return words[:size // 4]


def parse(words):
    body, i, records, groups = words[4:], 0, [], []
    current = None
    def take(n):
        nonlocal i
        if i + n > len(body):
            raise ValueError(f'truncated payload at body word {i}')
        v = body[i:i + n]
        i += n
        return v
    while i < len(body):
        at, pid = i, take(1)[0]
        if pid == 0x1009:
            dec, enc = take(2)
            rec = {'property': 'codec_supported', 'decoders': bits(dec, CODECS),
                   'encoders': bits(enc, CODECS)}
        elif pid == 0x100e:
            codecs, domains = take(2)
            current = {'codecs': bits(codecs, CODECS), 'domains': bits(domains, DOMAINS)}
            groups.append(current)
            rec = {'property': 'codec_mask', **current}
        elif pid == 0x1004:
            buftype, entries = take(2)
            fmts = []
            for _ in range(entries):
                fmt, planes = take(2)
                if planes > 4:
                    raise ValueError(f'{planes} planes at body word {i}')
                constraints = [dict(zip(('stride_multiple', 'max_stride',
                                         'min_height_multiple', 'alignment'), take(4)))
                               for _ in range(planes)]
                fmts.append({'format': FORMATS.get(fmt, f'0x{fmt:x}'), 'planes': constraints})
            rec = {'property': 'uncompressed_formats',
                   'buffer': BUFFERS.get(buftype, f'0x{buftype:x}'), 'formats': fmts}
        elif pid == 0x1007:
            (n,) = take(1)
            caps = []
            for _ in range(n):
                t, lo, hi, step = take(4)
                caps.append({'type': CAPS.get(t, f'0x{t:x}'), 'min': lo, 'max': hi, 'step': step})
            rec = {'property': 'capabilities', 'entries': caps}
        elif pid == 0x1006:
            (n,) = take(1)
            pl = [dict(zip(('profile', 'level'), take(2))) for _ in range(n)]
            rec = {'property': 'profile_levels', 'entries': pl}
        elif pid == 0x1010:
            rec = {'property': 'max_sessions', 'value': take(1)[0]}
        elif pid == 0x100a:
            (mask,) = take(1)
            rec = {'property': 'nal_stream_formats', 'mask': f'0x{mask:x}'}
        elif pid == 0x1016 and len(body) - i == 1:
            # Downstream HFI_PROPERTY_TME_VERSION_SUPPORTED. Its payload layout is
            # not in the pinned headers; accept only a single trailing word.
            rec = {'property': 'tme_version_supported', 'value': take(1)[0],
                   'size_inferred_from_packet_end': True}
        elif pid == 0x20100b:
            buftype, n = take(2)
            rec = {'property': 'buffer_alloc_modes', 'buffer': BUFFERS.get(buftype, buftype),
                   'modes': take(n)}
        else:
            records.append({'body_word': at, 'property': f'0x{pid:x}', 'stopped': 'unknown property'})
            return records, groups, i - 1
        rec['body_word'] = at
        if current is not None and rec['property'] != 'codec_mask':
            current.setdefault('records', []).append(rec['property'])
            current.setdefault('data', []).append(rec)
        records.append(rec)
    return records, groups, i


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('kmsg', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    a = ap.parse_args()
    if a.output.exists():
        sys.exit('refusing to overwrite output')
    words = load(a.kmsg)
    records, groups, consumed = parse(words)
    result = {
        'status': 'offline-sequential-parse',
        'packet_bytes': words[0], 'error': words[2], 'num_properties': words[3],
        'body_words': len(words) - 4, 'consumed_body_words': consumed,
        'records': len(records), 'complete': consumed == len(words) - 4,
        'groups': groups, 'all_records': records,
    }
    a.output.write_text(json.dumps(result, indent=1) + '\n')
    print(f"bytes={result['packet_bytes']} properties={result['num_properties']} "
          f"records={len(records)} consumed={consumed}/{result['body_words']} "
          f"complete={result['complete']}")
    for r in records:
        if r['property'] == 'codec_supported':
            print('codec_supported decoders=%s encoders=%s' % (r['decoders'], r['encoders']))
    for g in groups:
        if 'H264' in g['codecs'] and 'decoder' in g['domains']:
            for d in g.get('data', []):
                if d['property'] == 'capabilities':
                    print('H264-dec caps: ' + ', '.join(
                        f"{c['type']}={c['min']}..{c['max']}" for c in d['entries']))
                elif d['property'] == 'uncompressed_formats':
                    print(f"H264-dec {d['buffer']} formats: " +
                          ', '.join(f['format'] for f in d['formats']))
                elif d['property'] == 'profile_levels':
                    print('H264-dec profile/level: ' + ', '.join(
                        f"{e['profile']:#x}/{e['level']:#x}" for e in d['entries']))


if __name__ == '__main__':
    main()
