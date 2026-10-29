#!/usr/bin/env python3
"""Build private VIDEOCC candidate with software-managed codec domains.

No clock/status-check bypass, rate change, installation or firmware start.
Intended for Iris1 bring-up before firmware takes hardware-collapse control.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--stage', type=Path, default=Path('/root/.cache/sq2-videocc-sw-domains'))
a = p.parse_args()
k = Path('/root/.cache/sq2-thermal/kernel')
src = k / 'drivers/clk/qcom/videocc-sm8150.c'
raw = src.read_bytes()
assert hashlib.sha256(raw).hexdigest() == '119d847a17eef4ef86586a5ba7ade7cd8da6fb0e695277e4a56f56e9fda6ab53'
before = raw.decode()
assert before.count('.flags = HW_CTRL,') == 2
after = before.replace('.flags = HW_CTRL,', '.flags = 0, /* Iris1 bring-up: handoff deferred until clocks/firmware ready. */')
a.stage.mkdir(parents=True, exist_ok=False)
dest = ROOT / 'out/video-decode/videocc-sw-domains'
dest.mkdir(exist_ok=True)
(a.stage / src.name).write_text(after)
(a.stage / 'Makefile').write_text('obj-m += videocc-sm8150.o\nccflags-y += -I$(srctree)/drivers/clk/qcom\n')
(dest / 'source.diff').write_text(''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                                                           fromfile='original', tofile='candidate')))
subprocess.run(['make', '-s', '-C', str(k), 'O=/root/.cache/sq2-video-foundation/build',
    'ARCH=arm64', 'CROSS_COMPILE=aarch64-linux-gnu-', f'M={a.stage}', 'KBUILD_MODPOST_WARN=1', 'modules'],
    check=True, stdout=(dest / 'build.log').open('w'), stderr=subprocess.STDOUT)
blob = (a.stage / 'videocc-sm8150.ko').read_bytes()
(dest / 'videocc-sm8150.ko').write_bytes(blob)
report = {'status': 'private-candidate-not-hardware-tested',
          'input_source_sha256': hashlib.sha256(raw).hexdigest(),
          'module_sha256': hashlib.sha256(blob).hexdigest(),
          'changed_domains': ['vcodec0_gdsc', 'vcodec1_gdsc'],
          'clock_status_check_unchanged': True, 'clock_rates_unchanged': True,
          'hardware_collapse_handoff_implemented': False}
(dest / 'candidate.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
