#!/usr/bin/env python3
"""Port the SQ2 audio DT series reported working by ajacocks on 2026-10-06.

Source: https://github.com/linux-surface/surface-pro-x/issues/21
Original patch bytes and checksums are retained under probe/audio.
0008 targets the newer shared micbias parser, absent from our 6.3 codec;
6.3 already reads the individual micbias properties in its own probe.
"""
from pathlib import Path
import subprocess
import hashlib
import json

root = Path(__file__).resolve().parents[1]
kernel = Path('/root/.cache/sq2-probe-build/kernel')
source = root / 'probe/audio'
marker = kernel / '.sq2-audio-dt-v1'
manifest = json.loads((source / 'upstream-hashes.json').read_text(encoding='utf-8-sig'))
hashes = {x['file']: x['sha256'] for x in manifest}
if not marker.exists():
    for prefix in ('0000', '0001', '0004', '0002', '0005'):
        patch = next((source / 'patches').glob(prefix + '*'))
        assert hashlib.sha256(patch.read_bytes()).hexdigest() == hashes[patch.name]
        args = ['patch', '-p1', '--batch', '--forward', '-i', str(patch)]
        subprocess.run(args + ['--dry-run'], cwd=kernel, check=True)
        subprocess.run(args, cwd=kernel, check=True)
    dtsi = source / 'sc8180x-wcd9340.dtsi'
    assert hashlib.sha256(dtsi.read_bytes()).hexdigest() == hashes['sc8180x-wcd9340.dtsi.txt']
    (kernel / 'arch/arm64/boot/dts/qcom/sc8180x-wcd9340.dtsi').write_bytes(dtsi.read_bytes())
    marker.write_text('ajacocks SQ2 DT series, issue 21, 2026-10-06\n')
codec = (kernel / 'sound/soc/codecs/wcd934x.c').read_text()
assert '"qcom,micbias1-microvolt"' in codec
assert '"qcom,micbias2-microvolt"' in codec
print('SQ2 audio DT ready; kernel 6.3 already has independent micbias parsing.')
