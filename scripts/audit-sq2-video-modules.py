#!/usr/bin/env python3
"""Check candidate decoder module symbol closure against captured live exports.

This is a preflight, not ABI or hardware validation. Never loads modules.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path('/root/.cache/sq2-video-foundation/build')
REL = [
    'drivers/media/v4l2-core/videodev.ko',
    'drivers/media/common/videobuf2/videobuf2-common.ko',
    'drivers/media/common/videobuf2/videobuf2-memops.ko',
    'drivers/media/common/videobuf2/videobuf2-v4l2.ko',
    'drivers/media/common/videobuf2/videobuf2-dma-contig.ko',
    'drivers/media/v4l2-core/v4l2-mem2mem.ko',
    'drivers/media/platform/qcom/venus/venus-core.ko',
    'drivers/media/platform/qcom/venus/venus-dec.ko',
    'drivers/clk/qcom/videocc-sm8150.ko',
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', type=Path, action='append',
                        help='audit only these modules instead of the foundation set')
    parser.add_argument('--capture', type=Path,
                        default=ROOT / 'out/video-decode/running-kallsyms.txt')
    parser.add_argument('--output', type=Path,
                        default=ROOT / 'out/video-decode/module-preflight.json')
    args = parser.parse_args()
    capture = args.capture
    live = capture.read_text(encoding='utf-8-sig').splitlines()
    assert live[0].strip() == 'Y', 'screen-only guard capture missing'
    exports = {line.split()[2].removeprefix('__ksymtab_') for line in live
               if len(line.split()) >= 3 and line.split()[2].startswith('__ksymtab_')}
    assert len(exports) > 1000, 'invalid live kernel export capture'
    modules = {}
    owners = {}
    for p in (args.module or [BUILD / rel for rel in REL]):
        if p.name in modules:
            raise ValueError('duplicate module name: ' + p.name)
        symbols = subprocess.check_output(['aarch64-linux-gnu-nm', str(p)], text=True).splitlines()
        exported = {s.split()[-1].removeprefix('__ksymtab_') for s in symbols
                    if s.split()[-1].startswith('__ksymtab_')}
        for s in exported:
            if s in owners:
                raise ValueError('duplicate candidate export: ' + s)
            owners[s] = p.name
        undefined = subprocess.check_output(['aarch64-linux-gnu-nm', '-u', str(p)], text=True)
        modules[p.name] = {'path': str(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest(),
                           'undefined': [s.split()[-1] for s in undefined.splitlines()],
                           'vermagic': subprocess.check_output(['modinfo', '-F', 'vermagic', str(p)], text=True).strip()}
    missing = set()
    for name, item in modules.items():
        item['missing'] = sorted(s for s in item['undefined'] if s not in exports and s not in owners)
        item['candidate_dependencies'] = sorted({owners[s] for s in item['undefined'] if s in owners})
        missing.update(item['missing'])
        print(name, 'missing=' + str(len(item['missing'])), 'dependencies=' + ','.join(item['candidate_dependencies']))
    output = args.output
    output.write_text(json.dumps({'status': 'missing-symbols' if missing else 'symbol-closure-only',
        'live_capture_sha256': hashlib.sha256(capture.read_bytes()).hexdigest(),
        'modules': modules, 'missing': sorted(missing)}, indent=2) + '\n')
    print('MISSING', ','.join(sorted(missing)))
    if missing:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
