#!/usr/bin/env python3
"""Preserve the native SC8180X video clock provider in the 6.18 boot helper.

Transforms a verified helper copy only; does not load modules or start firmware.
"""
import argparse
import hashlib
import json
from pathlib import Path

CLOCK_BRANCH = r'''# SQ2_LTS_VIDEO_CLOCK: native DT already supplies the clock provider.
clock_dt=/sys/firmware/devicetree/base/soc@0/clock-controller@ab00000
if [ -d "$clock_dt" ]; then
    tr '\000' '\n' < "$clock_dt/compatible" | grep -qx 'qcom,sc8180x-videocc'
    tr '\000' '\n' < "$clock_dt/compatible" | grep -qx 'qcom,sm8150-videocc'
    if [ -f "$clock_dt/status" ]; then
        state=$(tr -d '\000' < "$clock_dt/status")
        case "$state" in okay|ok) ;; *) echo "VIDEO_CLOCK_DISABLED=$state"; exit 1;; esac
    fi
    echo SQ2_VIDEO_CLOCK_NATIVE
else
    # Older DTs need the retained clock overlay. Never replace a resident one.
    test ! -d /sys/module/videocc_sm8150
    modprobe sq2_iris1_identify allow=1
fi
# SQ2_LTS_VIDEO_CLOCK_END
'''


def transform(data):
    assert b'6.18.' in data and b'sq2-native-pas-attempted' in data
    assert b'SQ2_LTS_VIDEO_CLOCK' not in data
    site = b'modprobe sq2_iris1_identify allow=1\n'
    guard = b'for module in sq2_iris1_identify sq2_hfi_overlay videocc_sm8150 venus_core venus_dec sq2_iris1_phase; do'
    assert data.count(site) == 1 and data.count(guard) == 1
    data = data.replace(guard, guard.replace(b' videocc_sm8150', b''))
    return data.replace(site, CLOCK_BRANCH.encode())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--expected-sha256', required=True)
    a = p.parse_args()
    before = a.input.read_bytes()
    sha = lambda b: hashlib.sha256(b).hexdigest()
    assert sha(before) == a.expected_sha256
    result = transform(before)
    with a.output.open('xb') as f:
        f.write(result)
    assert a.output.read_bytes() == result and a.input.read_bytes() == before
    print(json.dumps(dict(before_sha256=sha(before), sha256=sha(result),
                          changed_branch='native SC8180X video clock provider reuse',
                          firmware_retry=False, device_writes=False), indent=2))


if __name__ == '__main__':
    main()
